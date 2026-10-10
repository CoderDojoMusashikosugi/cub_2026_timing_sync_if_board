#include "rtc_keep.h"

#include <string.h>

#include "board.h"
#include "stm32g0xx.h"
#include "ts_crc32c.h"
#include "ts_time.h"

#define SIGNATURE 0x5453u
#define VERSION 1u
#define FLAG_FREQ_VERIFIED 0x01u

/* LSE 32768 Hz → ck_apre 256 Hz → ck_spre 1 Hz。PREDIV_Aを大きくしてVBAT中の消費を抑える（RM0444） */
#define PREDIV_A 127u
#define PREDIV_S 255u

#define BDCR_CONFIG_BITS (RCC_BDCR_LSEON | RCC_BDCR_LSEBYP | RCC_BDCR_LSEDRV_Msk | RCC_BDCR_LSECSSON | \
                          RCC_BDCR_RTCSEL_Msk | RCC_BDCR_RTCEN)
#define WAKEUP_CR (RTC_CR_WUCKSEL_2 | RTC_CR_WUTE | RTC_CR_WUTIE) /* ck_spre（1 Hz）、WUTR=0で毎秒 */

static rtck_boot_t boot = {.reason = "未検査"};
static bool wakeup_configured;
static volatile bool in_init;
static bool config_changed; /* configuration CRCの対象を変えた（backupを書き直す必要がある） */

/* ---- 小物 ---- */

static void unlock(void)
{
    RTC->WPR = 0xCAu;
    RTC->WPR = 0x53u;
}

static void lock(void)
{
    RTC->WPR = 0xFFu;
}

static bool wait_set(volatile uint32_t *reg, uint32_t mask, bool set, uint32_t timeout_ms)
{
    const uint32_t start = board_millis();
    while (((*reg & mask) != 0u) != set) {
        if ((uint32_t)(board_millis() - start) > timeout_ms) {
            return false;
        }
    }
    return true;
}

static uint32_t bcd2(uint32_t v)
{
    return ((v / 10u) << 4) | (v % 10u);
}

static uint32_t config_crc(void)
{
    const uint32_t v[5] = {RCC->BDCR & BDCR_CONFIG_BITS, RTC->PRER, RTC->CR, RTC->CALR, RTC->WUTR};
    return ts_crc32c(v, sizeof(v));
}

static uint32_t app_crc(uint32_t b0, uint32_t b1, uint32_t b2, uint32_t b4)
{
    const uint32_t v[4] = {b0, b1, b2, b4};
    return ts_crc32c(v, sizeof(v));
}

/* shadow registerの同期を待つ（RSFをclearしてから立つのを待つ）。ICSRは書込み保護の対象なので解除して書く */
static bool resync_shadow(void)
{
    unlock();
    RTC->ICSR &= ~RTC_ICSR_RSF;
    return wait_set(&RTC->ICSR, RTC_ICSR_RSF, true, 10u);
}

/* SSR→TR→DRの順に読む（BYPSHAD=0ではSSR/TRを読むとDRを読むまで上位が固定される） */
static bool read_raw(int64_t *unix_sec, uint32_t *sub256)
{
    const uint32_t ssr = RTC->SSR;
    const uint32_t tr = RTC->TR;
    const uint32_t dr = RTC->DR;
    ts_datetime_t dt;

    if (tr & RTC_TR_PM) {
        return false;
    }
    dt.hour = (uint8_t)(((tr >> 20) & 3u) * 10u + ((tr >> 16) & 0xFu));
    dt.minute = (uint8_t)(((tr >> 12) & 7u) * 10u + ((tr >> 8) & 0xFu));
    dt.second = (uint8_t)(((tr >> 4) & 7u) * 10u + (tr & 0xFu));
    dt.year = (uint16_t)(2000u + ((dr >> 20) & 0xFu) * 10u + ((dr >> 16) & 0xFu));
    dt.month = (uint8_t)(((dr >> 12) & 1u) * 10u + ((dr >> 8) & 0xFu));
    dt.day = (uint8_t)(((dr >> 4) & 3u) * 10u + (dr & 0xFu));
    if (!ts_datetime_to_unix(&dt, unix_sec) || ssr > PREDIV_S) {
        return false;
    }
    if (sub256 != NULL) {
        *sub256 = PREDIV_S - ssr;
    }
    return true;
}

/* ---- 起動時の検証 ---- */

static const char *check_domain(void)
{
    const uint32_t bdcr = RCC->BDCR;

    if (!(bdcr & RCC_BDCR_LSEON)) {
        return "LSEON=0（電池なしで電源が切れた、または初回）";
    }
    if (!(bdcr & RCC_BDCR_LSERDY)) {
        return "LSERDY=0";
    }
    if ((bdcr & (RCC_BDCR_LSEBYP | RCC_BDCR_LSECSSON)) != 0u ||
        (bdcr & RCC_BDCR_LSEDRV_Msk) != RCC_BDCR_LSEDRV_1) {
        return "LSE設定（LSEBYP/LSECSSON/LSEDRV）不一致";
    }
    if ((bdcr & RCC_BDCR_RTCSEL_Msk) != RCC_BDCR_RTCSEL_0 || !(bdcr & RCC_BDCR_RTCEN)) {
        return "RTCSEL/RTCEN不一致";
    }
    const uint32_t b0 = TAMP->BKP0R;
    const uint32_t b1 = TAMP->BKP1R;
    const uint32_t b2 = TAMP->BKP2R;
    const uint32_t b3 = TAMP->BKP3R;
    const uint32_t b4 = TAMP->BKP4R;
    if ((b0 >> 16) != SIGNATURE || ((b0 >> 8) & 0xFFu) != VERSION) {
        return "signature/version不一致";
    }
    if (app_crc(b0, b1, b2, b4) != b3) {
        return "application CRC不一致";
    }
    if (config_crc() != b4) {
        return "configuration CRC不一致";
    }
    if (!(RTC->ICSR & RTC_ICSR_INITS)) {
        return "INITS=0";
    }
    if (!resync_shadow()) {
        return "RSFが立たない";
    }
    int64_t t1;
    uint32_t s1;
    if (!read_raw(&t1, &s1) || !ts_unix_in_rtc_range(t1)) {
        return "calendar範囲外";
    }
    if (t1 + 1 < (int64_t)b2) {
        return "calendarが最後の同期時刻より前";
    }
    board_delay_ms(15u);
    int64_t t2;
    uint32_t s2;
    if (!resync_shadow() || !read_raw(&t2, &s2)) {
        return "calendar範囲外";
    }
    const int64_t d = (t2 - t1) * 256 + (int64_t)s2 - (int64_t)s1;
    if (d <= 0 || d > 64) {
        return "SSRが単調に進まない";
    }
    boot.freq_verified = (b0 & FLAG_FREQ_VERIFIED) != 0u;
    boot.freq_q16 = (int32_t)b1;
    boot.sync_unix = b2;
    boot.boot_unix = t2;
    return NULL;
}

void rtck_boot_check(void)
{
    RCC->APBENR1 |= RCC_APBENR1_PWREN | RCC_APBENR1_RTCAPBEN;
    (void)RCC->APBENR1;
    PWR->CR1 |= PWR_CR1_DBP;
    (void)PWR->CR1;

    /* BDRSTで消える前に、tamperの記録と電源断を越えたcalendarを残す（backup registerが消えたときの切分け） */
    boot.tamp_sr = TAMP->SR;
    boot.tamp_cr1 = TAMP->CR1;
    if (rtck_clock_ready() && (RTC->ICSR & RTC_ICSR_INITS) && resync_shadow()) {
        boot.pre_calendar = read_raw(&boot.pre_unix, NULL);
    }

    const char *why = check_domain();
    lock();
    if (why == NULL) {
        boot.valid = true;
        boot.reason = "OK";
    } else {
        /* 異常: backup domainをsoftwareで初期化する（set→read-back→clear）。時刻は無効 */
        boot.valid = false;
        boot.reason = why;
        RCC->BDCR |= RCC_BDCR_BDRST;
        boot.did_bdrst = (RCC->BDCR & RCC_BDCR_BDRST) != 0u;
        RCC->BDCR &= ~RCC_BDCR_BDRST;
        (void)RCC->BDCR;
    }
    /*
     * TAMP_CR1の初期値（0xFFFF0000）では内部tamper 3～6が有効で、ITAMP4（HSE monitoring）はHSE CSSの検出で
     * backup registerを消す。USBを抜くと3V3が下がる途中でMCUより先にTCXOが止まるため、電源を切るたびに
     * backupが消えていた（2026-10-10実測: 起動時TAMP_SR=ITAMP4F、calendarは電源断を越えて正しかった）。
     * HSEの異常はCSS（NMI）で扱うので、backupは消させない。BDRSTで初期値へ戻るため毎回書く
     */
    TAMP->CR1 &= ~TAMP_CR1_ITAMP4E;
    TAMP->SCR = TAMP_SCR_CITAMP4F;
}

const rtck_boot_t *rtck_boot(void)
{
    return &boot;
}

/* ---- 運用中 ---- */

bool rtck_clock_ready(void)
{
    const uint32_t bdcr = RCC->BDCR;
    return (bdcr & RCC_BDCR_LSERDY) && (bdcr & RCC_BDCR_RTCSEL_Msk) == RCC_BDCR_RTCSEL_0 && (bdcr & RCC_BDCR_RTCEN);
}

bool rtck_calendar_valid(void)
{
    return rtck_clock_ready() && (RTC->ICSR & RTC_ICSR_INITS) && !in_init;
}

static void configure_wakeup(void)
{
    if ((RTC->CR & (RTC_CR_WUCKSEL_Msk | RTC_CR_WUTE | RTC_CR_WUTIE)) != WAKEUP_CR || RTC->WUTR != 0u) {
        unlock();
        RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
        if (!wait_set(&RTC->ICSR, RTC_ICSR_WUTWF, true, 10u)) {
            lock();
            return;
        }
        RTC->WUTR = 0u;
        RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL_Msk) | WAKEUP_CR;
        lock();
        config_changed = true;
    }
    RTC->SCR = RTC_SCR_CWUTF;
    EXTI->IMR1 |= EXTI_IMR1_IM19; /* RTCの割込みはEXTI line 19（direct） */
    NVIC_SetPriority(RTC_TAMP_IRQn, 1);
    NVIC_EnableIRQ(RTC_TAMP_IRQn);
    wakeup_configured = true;
}

void rtck_task(void)
{
    const uint32_t bdcr = RCC->BDCR;

    if (!(bdcr & RCC_BDCR_LSERDY)) {
        return;
    }
    /* BDRSTの後はRTCSEL=00。LSEが起動したら一度だけLSEを選ぶ */
    if ((bdcr & RCC_BDCR_RTCSEL_Msk) == 0u) {
        RCC->BDCR |= RCC_BDCR_RTCSEL_0;
        config_changed = true;
    }
    if (!(RCC->BDCR & RCC_BDCR_RTCEN)) {
        RCC->BDCR |= RCC_BDCR_RTCEN;
        config_changed = true;
    }
    if (!wakeup_configured && rtck_clock_ready()) {
        configure_wakeup();
    }
}

bool rtck_read(int64_t *unix_sec, uint32_t *sub256)
{
    if (!rtck_calendar_valid() || !(RTC->ICSR & RTC_ICSR_RSF)) {
        return false;
    }
    return read_raw(unix_sec, sub256);
}

bool rtck_config_changed(void)
{
    const bool changed = config_changed;
    config_changed = false;
    return changed;
}

bool rtck_write_begin(int64_t unix_sec)
{
    ts_datetime_t dt;

    if (!rtck_clock_ready() || in_init || !ts_unix_in_rtc_range(unix_sec) || !ts_unix_to_datetime(unix_sec, &dt)) {
        return false;
    }
    const int64_t days = unix_sec / 86400;
    const uint32_t weekday = (uint32_t)((days + 3) % 7) + 1u; /* 1=月曜（1970-01-01は木曜） */
    const uint32_t tr = (bcd2(dt.hour) << 16) | (bcd2(dt.minute) << 8) | bcd2(dt.second);
    const uint32_t dr = (bcd2(dt.year % 100u) << 16) | (weekday << RTC_DR_WDU_Pos) | (bcd2(dt.month) << 8) |
                        bcd2(dt.day);

    unlock();
    /* ES0548 §2.9.1: 一度INITを抜けた後に再びINITへ入るときは、BYPSHAD=0でRSFの立上りを待つ */
    if (RTC->ICSR & RTC_ICSR_INITS) {
        RTC->CR &= ~RTC_CR_BYPSHAD;
        if (!resync_shadow()) {
            lock();
            return false;
        }
    }
    RTC->ICSR |= RTC_ICSR_INIT;
    if (!wait_set(&RTC->ICSR, RTC_ICSR_INITF, true, 10u)) {
        RTC->ICSR &= ~RTC_ICSR_INIT;
        lock();
        return false;
    }
    /* 1回のINIT sessionでprescaler（同期→非同期の順の2回）とcalendar全registerを書く */
    RTC->PRER = PREDIV_S;
    RTC->PRER = (PREDIV_A << RTC_PRER_PREDIV_A_Pos) | PREDIV_S;
    RTC->TR = tr;
    RTC->DR = dr;
    RTC->CR &= ~(RTC_CR_FMT | RTC_CR_BYPSHAD);
    in_init = true;
    return true;
}

void rtck_write_exit(void)
{
    if (in_init) {
        RTC->ICSR &= ~RTC_ICSR_INIT;
        in_init = false;
    }
}

void rtck_write_end(void)
{
    if (in_init) {
        rtck_write_exit();
    }
    lock();
}

bool rtck_in_init(void)
{
    return in_init;
}

void rtck_commit(uint32_t sync_unix, int32_t freq_q16, bool freq_verified)
{
    const uint32_t b0 = (SIGNATURE << 16) | (VERSION << 8) | (freq_verified ? FLAG_FREQ_VERIFIED : 0u);
    const uint32_t b1 = (uint32_t)freq_q16;
    const uint32_t b4 = config_crc();

    /* payload→configuration CRC→application CRCの順。途中で止まればapplication CRCが合わない */
    TAMP->BKP0R = b0;
    TAMP->BKP1R = b1;
    TAMP->BKP2R = sync_unix;
    TAMP->BKP4R = b4;
    TAMP->BKP3R = app_crc(b0, b1, sync_unix, b4);
}

/* CALR: 2^20周期（32秒）ごとにCALM個のpulseを除き、CALPなら512個加える。1 step ≒ 0.954 ppm */
int32_t rtck_calibration_ppb(void)
{
    const uint32_t calr = RTC->CALR;
    const int32_t steps = ((calr & RTC_CALR_CALP) ? 512 : 0) - (int32_t)(calr & RTC_CALR_CALM_Msk);
    return (int32_t)((int64_t)steps * 1000000000 / 1048576);
}

bool rtck_set_calibration(int32_t lse_fast_ppb)
{
    if (!rtck_clock_ready()) {
        return false;
    }
    /* 速いLSEは遅らせる: 補正量 = −偏差 */
    int64_t steps = -(int64_t)lse_fast_ppb * 1048576;
    steps = steps >= 0 ? (steps + 500000000) / 1000000000 : -((-steps + 500000000) / 1000000000);
    if (steps > 512) {
        steps = 512;
    } else if (steps < -511) {
        steps = -511;
    }
    const uint32_t calr = steps > 0 ? (RTC_CALR_CALP | (uint32_t)(512 - steps)) : (uint32_t)(-steps);
    if ((RTC->CALR & (RTC_CALR_CALP | RTC_CALR_CALM_Msk)) == calr) {
        return false;
    }
    unlock();
    if (!wait_set(&RTC->ICSR, RTC_ICSR_RECALPF, false, 50u)) {
        lock();
        return false;
    }
    RTC->CALR = calr;
    lock();
    return true;
}

bool rtck_wakeup_ack(void)
{
    if (RTC->MISR & RTC_MISR_WUTMF) {
        RTC->SCR = RTC_SCR_CWUTF;
        return true;
    }
    return false;
}
