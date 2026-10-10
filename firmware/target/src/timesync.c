#include "timesync.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "console.h"
#include "rtc_keep.h"
#include "stm32g0xx.h"
#include "ts_dpll.h"
#include "ts_nmea.h"
#include "ts_time.h"

/*
 * ---- 暫定値（firmware/test_plan.md §2のP項目。決まったらdecision_logと正本へ移す） ----
 */
#define PPS_SETPOINT 6            /* P-06: J3→J2折返しの実測6 count（125 ns、出力buffer＋入力経路）を全基板共通の既定値 */
#define OUT_BAUD 9600u            /* P-08: 全channel GPRMC、9600 baud、8N1 */
#define NMEA_START_COUNTS 9600000u /* P-10: PPS立上りの200 ms後に送り始める（VLP・Mid-360の両方の時間窓に入る） */
#define OE_ENABLE_COUNTS 7200000u  /* 出力のenableはPPSがLowの150 msで行い、短パルスを作らない */
#define INTERVAL_TOL 4800         /* P-01: GNSS PPS間隔の許容 1秒±100 ppm */
#define LOCK_COUNT 5u             /* PPS＋有効RMCの連続回数（状態遷移表） */
#define LOCK_ERR 48               /* P-04: 位相差1 us以内を揃ったとみなす */
#define TRACK_GATE 4800           /* GNSS_LOCKED中、100 usより離れたGNSS PPSは欠落とみなす（glitch除外） */
#define FAULT_ERR 4800000         /* P-03: 位相差100 ms超でFAULT（GNSS由来の時刻のとき） */
#define STEP_ERR 48000            /* P-05: GNSS以外の時刻源からの復帰で1 ms超ならHigh-Zにしてstep */
#define ACQ_STEP_ERR 480          /* 時刻がまだない（出力High-Z）取得中は10 us超ならstepで揃える */
#define MISS_LIMIT 2u             /* PPS 2回連続欠落、RMC 2回連続無効でholdover */
#define MISMATCH_LIMIT 3u         /* RMCの秒が生成PPSの秒と3回続けて合わなければUTC矛盾 */
#define REENABLE_COUNT 2u         /* stepの後、2秒続けて正常に更新してから出力をenableする */
#define RMC_MAX_DELAY_MS 990u     /* PPSからこの時間内に来たRMCをそのPPSの時刻とする */
#define GNSS_AUTOBAUD_MS 3000u    /* 有効なNMEAがこの時間なければ次のbaudを試す */
#define GNSS_ACTIVE_MS 3000u
#define GNSS_SILENT_MS 1500u      /* PPSもRMCも来ない時間がこれを超えたら、何もない1秒として数える */

#define NOM ((int64_t)TS_DPLL_NOMINAL)
#define COUNTS_PER_MS 48000

/* ---- 出力channel ---- */

typedef struct {
    const char *name;
    USART_TypeDef *uart;
    bool apb2;
    uint32_t en_bit;
    bool lpuart;
    GPIO_TypeDef *port;
    uint8_t pin;
    uint8_t af;
    uint8_t oe_pin;    /* GPIOB */
    bool invert;       /* VLP: TXINVで反転 */
    bool plus_one;     /* VLP: UTC+1秒 */
    uint8_t frac_digits;
} out_ch_t;

/* pinとAFの正本は hardware/overall/pin_assignment.md「MCU割当」 */
static const out_ch_t channels[] = {
    {"J3", USART2, false, RCC_APBENR1_USART2EN, false, GPIOA, 2u, 1u, 12u, false, false, 2u},
    {"J4", USART4, false, RCC_APBENR1_USART4EN, false, GPIOA, 0u, 4u, 13u, true, true, 0u},
    {"J5", USART5, false, RCC_APBENR1_USART5EN, false, GPIOB, 0u, 8u, 14u, false, false, 2u},
    {"J6", USART6, false, RCC_APBENR1_USART6EN, false, GPIOA, 4u, 3u, 2u, false, false, 2u},
    {"J7", USART1, true, RCC_APBENR2_USART1EN, false, GPIOA, 9u, 1u, 8u, false, false, 2u},
    {"J8", LPUART2, false, RCC_APBENR1_LPUART2EN, true, GPIOC, 6u, 3u, 9u, false, false, 2u},
};
#define CH_COUNT (sizeof(channels) / sizeof(channels[0]))
#define CH_J3 0u
#define CH_J4 1u
#define CH_J5 2u
#define CH_J6 3u
#define CH_J7 4u
#define CH_J8 5u
#define PPS_OE_PIN 1u

typedef struct {
    char buf[TS_NMEA_MAX_SENTENCE + 2u];
    uint8_t len;
    volatile uint8_t pos;
    volatile bool ready;  /* bufに次の秒の文がある */
    volatile bool busy;   /* 送信中 */
    volatile uint32_t index; /* 送る秒境界の番号 */
    uint32_t sent;
    uint32_t skipped;
} tx_t;

static tx_t tx[CH_COUNT];
static uint8_t out_mask = 0x3Fu;   /* consoleで止められるTX channel（bit0=J3～bit5=J8） */
static bool out_inhibit;           /* consoleの out off */
static volatile bool oe_on;        /* PPS_OE_Nと各TX OE_NをLowにしている */
static volatile bool oe_request;   /* 次の150 msでenableする */

/* ---- GNSS受信（USART3 RX、PB11） ---- */

#define RX_RING 4096u /* 115200 baudで約350 ms分。main loopが止まっても文を落とさない */
static volatile uint8_t rx_ring[RX_RING];
static volatile uint16_t rx_head;
static uint16_t rx_tail;
static volatile uint32_t rx_bytes, rx_errors, rx_overflow;

static const uint32_t gnss_bauds[] = {115200u, 9600u, 230400u, 460800u, 921600u, 38400u, 57600u, 19200u};
static uint32_t gnss_baud;
static bool gnss_autobaud = true;
static unsigned gnss_baud_idx;
static uint32_t gnss_baud_since_ms;
static ts_nmea_rx_t nmea_rx;
static uint32_t nmea_good, nmea_bad_checksum, nmea_discarded, rmc_count;
static uint32_t last_good_sentence_ms;
static bool have_good_sentence;
static ts_nmea_rmc_t last_fix;     /* 出力RMCの位置fieldの出所（最後の有効RMC） */
static bool last_fix_valid;
static char last_rmc_status = '-';
static uint32_t last_rmc_delay_ms;  /* PPSからRMC受信完了まで */

/* ---- TIM2（ISRと共有） ---- */

typedef struct {
    uint32_t index; /* captureした期間の番号k */
    uint32_t ccr;   /* B_kからのcount */
    uint32_t n;     /* 期間kの長さ */
    uint64_t abs;   /* 絶対位置 */
    uint32_t ms;
} cap_ev_t;

#define CAP_Q 8u
static cap_ev_t cap_q[CAP_Q];
static volatile uint8_t cap_head, cap_tail;
static volatile uint32_t cap_q_overflow;

static volatile uint32_t tim_index;  /* 今の期間の番号k */
static volatile uint64_t tim_base;   /* B_kの絶対位置（count） */
static volatile uint32_t tim_n_cur;  /* 期間kの長さ */
static volatile uint32_t tim_n_next; /* 期間k+1の長さ（ARR preload済み） */
static volatile uint32_t tim_boundary_ms;
static uint32_t tim_cap_count, tim_cap_ccr;
static ts_dpll_t dpll;
static volatile bool second_flag;     /* console用 */
static volatile bool boundary_flag;   /* NMEA作成用 */
static volatile uint32_t pps_captures;

/* ---- 状態 ---- */

typedef enum { ORIGIN_NONE = 0, ORIGIN_GNSS, ORIGIN_JETSON, ORIGIN_RTC } origin_t;

/* ALIGN_GNSS: GNSS PPSへ揃える（ラベルは後のRMCで決める）。ALIGN_TIME: ラベル付きの事象（Jetson SET、RTCの秒境界）へ揃える */
typedef enum { ALIGN_NONE = 0, ALIGN_GNSS, ALIGN_TIME } align_purpose_t;

static uint8_t state = TS_JP_STATE_INIT_INVALID;
static bool time_valid;
static int64_t utc_offset; /* 秒境界B_kのUTC = k + utc_offset */
static origin_t origin;
static const char *last_event = "start";

static unsigned good_run;     /* PPS＋間隔＋有効RMC＋秒の連続、の連続回数 */
static unsigned aligned_run;  /* そのうち位相差がLOCK_ERR以内の連続回数 */
static unsigned pps_miss, nmea_bad, mismatch;
static bool stepping;         /* High-Zにしてstep中（再enable待ち） */
static uint32_t reenable_at;  /* この境界番号以降で出力をenableする */
static bool reenable_pending;
static bool f_seeded;
static uint32_t settle_index; /* alignで揃えた秒境界。これより前のcaptureは位相の判断に使わない */
static align_purpose_t align_purpose;
static int64_t align_label;   /* ALIGN_TIMEで揃える事象のUTC秒 */
static origin_t align_origin; /* ALIGN_TIMEの時刻源（Jetson、RTC） */
static int64_t last_label;
static bool last_label_valid;
static uint32_t last_gnss_activity_ms;
static int32_t last_err;      /* 最後のGNSS PPSの位相差（count） */
static bool last_err_valid;

/* ---- 内蔵RTCによる時刻保持 ---- */

#define RTC_RESYNC_ERR 240000            /* RTCの秒境界が生成PPSから5 msずれたらcalendarを書き直す */
#define RTC_COMMIT_INTERVAL_MS 600000u   /* GNSS_LOCKED中、TCXO補正値をbackupへ書く間隔 */
#define RTC_CAL_MIN_SECONDS 300u         /* LSE測定がこの秒数たまってからsmooth calibrationを決める */
#define RTC_CAL_INTERVAL_MS 60000u
#define RTC_RETRY_LIMIT 3u
#define JETSON_ACK_TIMEOUT_MS 8000u

typedef enum { RTCS_IDLE = 0, RTCS_INIT, RTCS_VERIFY } rtc_sync_state_t;

static rtc_sync_state_t rtc_state;
static bool rtc_sync_request;
static volatile bool rtc_exit_pending;   /* TIM2 ISR: この番号の秒境界でINITを抜ける */
static volatile uint32_t rtc_exit_index;
static uint32_t rtc_verify_index;
static unsigned rtc_fail;
static bool rtc_written;                 /* このbootでcalendarを書いて確かめた */
static uint32_t rtc_last_sync_ms, rtc_last_commit_ms, rtc_last_cal_ms;
static const char *rtc_note = "";
static bool f_verified;                  /* 今のTCXO補正値はGNSSで検証済み */
static bool rtc_boot_pending;            /* RTC-only起動: RTCの秒境界へ揃える前 */
static unsigned rtc_boot_checks;         /* 揃えた後、RTCの秒と生成PPSの秒が一致した回数 */
static uint32_t rtc_checked_index;

/* RTC 1 Hz wakeup（RTCの秒境界）の位置。RTC割込みで記録する */
static volatile uint64_t rtc_tick_abs;
static volatile uint32_t rtc_tick_ms, rtc_tick_count;
static volatile int32_t rtc_tick_err; /* 最寄りの生成PPS境界に対する位置（count、正でRTCが後） */

/* Jetson SET（step）のACKは、時刻の適用とRTC書込み・backup commitの後に返す */
static bool jetson_wait, jetson_done;
static uint8_t jetson_code;
static uint32_t jetson_wait_ms;

typedef struct {
    bool pps;
    uint32_t pps_ms;
    uint64_t pps_abs;
    int32_t err;
    uint32_t match_index; /* 対応する秒境界の番号 */
    bool interval_ok;
    int64_t interval;
    bool rmc;
    bool rmc_valid;       /* status A、時刻が有効 */
    char status;
    int64_t label;        /* RMCのUTC秒 */
} gnss_rec_t;

static gnss_rec_t rec;
static bool rec_open;
static uint32_t last_rec_ms;
static uint64_t prev_pps_abs;
static uint32_t prev_pps_ms;
static bool prev_pps_valid;

/* ---- 小物 ---- */

static void pin_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}

static void pin_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    volatile uint32_t *afr = &port->AFR[pin / 8u];
    const uint32_t shift = (pin % 8u) * 4u;
    *afr = (*afr & ~(0xFu << shift)) | (af << shift);
}

static int32_t abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

static void set_event(const char *s)
{
    last_event = s;
}

/* ---- UART ---- */

/* 8N1、FIFO有効。LPUARTはBRRが20 bitに収まるようprescalerを選ぶ */
static void uart_config(USART_TypeDef *u, bool lpuart, uint32_t baud, uint32_t cr2, uint32_t cr1)
{
    static const uint16_t lp_div[] = {1, 2, 4, 6, 8, 10, 12, 16, 32, 64, 128, 256};
    uint32_t presc = 0;
    uint32_t brr;

    u->CR1 = 0;
    if (lpuart) {
        uint64_t v = 0;
        for (presc = 0; presc < sizeof(lp_div) / sizeof(lp_div[0]); presc++) {
            v = (256ull * SystemCoreClock / lp_div[presc] + baud / 2u) / baud;
            if (v <= 0xFFFFFu) {
                break;
            }
        }
        brr = (uint32_t)v;
    } else {
        brr = (SystemCoreClock + baud / 2u) / baud;
    }
    u->PRESC = presc;
    u->BRR = brr;
    u->CR2 = cr2;
    u->CR3 = 0;
    u->ICR = 0xFFFFFFFFu;
    u->CR1 = USART_CR1_FIFOEN | cr1 | USART_CR1_UE;
}

static void uart_clock_on(bool apb2, uint32_t bit)
{
    volatile uint32_t *enr = apb2 ? &RCC->APBENR2 : &RCC->APBENR1;
    *enr |= bit;
    (void)*enr;
}

/* USART割込みから: TX FIFOに空きがある間だけ詰める */
static void tx_service(unsigned i)
{
    USART_TypeDef *u = channels[i].uart;
    tx_t *t = &tx[i];

    if (!(u->CR1 & USART_CR1_TXEIE_TXFNFIE)) {
        return;
    }
    while ((u->ISR & USART_ISR_TXE_TXFNF) && t->pos < t->len) {
        u->TDR = (uint8_t)t->buf[t->pos++];
    }
    if (t->pos >= t->len) {
        u->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
        t->sent++;
        t->busy = false; /* CR1を書き終えてから下ろす（TIM2側はbusyを見て次を始める） */
    }
}

static void gnss_rx_service(void)
{
    USART_TypeDef *u = USART3;
    const uint32_t isr = u->ISR;

    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        rx_errors++;
        u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    }
    while (u->ISR & USART_ISR_RXNE_RXFNE) {
        const uint8_t b = (uint8_t)u->RDR;
        const uint16_t next = (uint16_t)((rx_head + 1u) % RX_RING);
        rx_bytes++;
        if (next == rx_tail) {
            rx_overflow++;
        } else {
            rx_ring[rx_head] = b;
            rx_head = next;
        }
    }
}

void USART1_IRQHandler(void)
{
    tx_service(CH_J7);
}

void USART2_LPUART2_IRQHandler(void)
{
    tx_service(CH_J3);
    tx_service(CH_J8);
}

void USART3_4_5_6_LPUART1_IRQHandler(void)
{
    gnss_rx_service();
    tx_service(CH_J4);
    tx_service(CH_J5);
    tx_service(CH_J6);
}

static void gnss_set_baud(uint32_t baud)
{
    NVIC_DisableIRQ(USART3_4_5_6_LPUART1_IRQn);
    /* U4Bで反転しているのでRXINV=1。RXだけ使う */
    uart_config(USART3, false, baud, USART_CR2_RXINV, USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE);
    gnss_baud = baud;
    gnss_baud_since_ms = board_millis();
    ts_nmea_rx_init(&nmea_rx);
    NVIC_EnableIRQ(USART3_4_5_6_LPUART1_IRQn);
}

static void uarts_init(void)
{
    for (unsigned i = 0; i < CH_COUNT; i++) {
        const out_ch_t *c = &channels[i];
        uart_clock_on(c->apb2, c->en_bit);
        /* OE_NはHighのまま（board_early_init）。VLPはTXINVでidleがLowになる */
        uart_config(c->uart, c->lpuart, OUT_BAUD, c->invert ? USART_CR2_TXINV : 0u, USART_CR1_TE);
        pin_af(c->port, c->pin, c->af);
        pin_mode(c->port, c->pin, 2u);
    }
    uart_clock_on(false, RCC_APBENR1_USART3EN);
    pin_af(GPIOB, 11u, 4u);
    pin_mode(GPIOB, 11u, 2u);
    gnss_set_baud(gnss_bauds[0]);

    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_SetPriority(USART2_LPUART2_IRQn, 1);
    NVIC_SetPriority(USART3_4_5_6_LPUART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART2_LPUART2_IRQn);
    NVIC_EnableIRQ(USART3_4_5_6_LPUART1_IRQn);
}

/* ---- 出力enable ---- */

static uint32_t oe_bits(void)
{
    uint32_t bits = 1u << PPS_OE_PIN;
    for (unsigned i = 0; i < CH_COUNT; i++) {
        if (out_mask & (1u << i)) {
            bits |= 1u << channels[i].oe_pin;
        }
    }
    return bits;
}

/* すぐHigh-Zにする */
static void outputs_off(void)
{
    __disable_irq();
    oe_request = false;
    GPIOB->BSRR = BOARD_OE_N_PINS;
    oe_on = false;
    __enable_irq();
}

/* 次の150 ms（PPSがLowの間）でenableする */
static void outputs_on_request(void)
{
    if (!out_inhibit && !oe_on) {
        oe_request = true;
    }
}

/* ---- TIM2 ---- */

static void on_capture(uint32_t c)
{
    tim_cap_count++;
    if (tim_cap_count == 1u) {
        tim_cap_ccr = c;
    }
    pps_captures++;
    const uint8_t next = (uint8_t)((cap_head + 1u) % CAP_Q);
    if (next == cap_tail) {
        cap_q_overflow++;
        return;
    }
    cap_q[cap_head] = (cap_ev_t){tim_index, c, tim_n_cur, tim_base + c, board_millis()};
    cap_head = next;
}

static void on_update(void)
{
    ts_dpll_in_t in;

    in.have_capture = tim_cap_count == 1u;
    in.capture = tim_cap_ccr;
    in.n_k = tim_n_cur;
    tim_base += tim_n_cur;
    tim_index++;
    if (rtc_exit_pending && tim_index == rtc_exit_index) {
        /* この秒境界からRTCが書いた秒を数え始める（解除からRTCCLK 4周期後、約122 us遅れる） */
        rtck_write_exit();
        rtc_exit_pending = false;
    }
    tim_n_cur = tim_n_next;
    in.base_k1 = tim_base;
    in.index_k1 = tim_index;
    in.n_k1 = tim_n_cur;
    const uint32_t n2 = ts_dpll_boundary(&dpll, &in);
    TIM2->ARR = n2 - 1u;
    tim_n_next = n2;
    tim_cap_count = 0;
    tim_boundary_ms = board_millis();
    second_flag = true;
    boundary_flag = true;
}

/* 150 ms: 出力のenable。200 ms: 準備できたNMEAの送信開始 */
static void nmea_kick(void)
{
    if (!oe_on) {
        return;
    }
    for (unsigned i = 0; i < CH_COUNT; i++) {
        tx_t *t = &tx[i];
        if (t->ready && t->index == tim_index && !t->busy) {
            t->ready = false;
            t->busy = true;
            t->pos = 0;
            channels[i].uart->CR1 |= USART_CR1_TXEIE_TXFNFIE;
        }
    }
}

void TIM2_IRQHandler(void)
{
    const uint32_t sr = TIM2->SR;

    if (sr & TIM_SR_CC1IF) {
        const uint32_t c = TIM2->CCR1; /* 読むとCC1IFがclearされる */
        if (TIM2->SR & TIM_SR_CC1OF) {
            TIM2->SR = ~TIM_SR_CC1OF;
            tim_cap_count++; /* captureを取りこぼした: この期間は使わない */
        }
        if ((sr & TIM_SR_UIF) && c > tim_n_cur / 2u) {
            on_capture(c); /* 境界の直前のedge: 終わった期間に属する */
            TIM2->SR = ~TIM_SR_UIF;
            on_update();
        } else {
            if (sr & TIM_SR_UIF) {
                TIM2->SR = ~TIM_SR_UIF;
                on_update();
            }
            on_capture(c);
        }
    } else if (sr & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;
        on_update();
    }
    if (sr & TIM_SR_CC4IF) {
        TIM2->SR = ~TIM_SR_CC4IF;
        if (oe_request) {
            oe_request = false;
            GPIOB->BRR = oe_bits();
            oe_on = true;
        }
    }
    if (sr & TIM_SR_CC3IF) {
        TIM2->SR = ~TIM_SR_CC3IF;
        nmea_kick();
    }
}

/* RTCの1 Hz wakeup = RTCの秒境界。そのときの生成PPSに対する位置を記録する */
void RTC_TAMP_IRQHandler(void)
{
    if (!rtck_wakeup_ack()) {
        return;
    }
    __disable_irq();
    uint64_t base = tim_base;
    uint32_t n = tim_n_cur;
    const uint32_t cnt = TIM2->CNT;
    if ((TIM2->SR & TIM_SR_UIF) && cnt < n / 2u) {
        base += n; /* 境界を過ぎたがTIM2の割込みがまだ */
        n = tim_n_next;
    }
    __enable_irq();
    rtc_tick_abs = base + cnt;
    rtc_tick_err = cnt <= n / 2u ? (int32_t)cnt : (int32_t)cnt - (int32_t)n;
    rtc_tick_ms = board_millis();
    rtc_tick_count++;
}

static void tim2_start(void)
{
    __disable_irq();
    tim_index = 0;
    tim_base = 0;
    tim_n_cur = TIM2->ARR + 1u;
    tim_n_next = tim_n_cur;
    tim_cap_count = 0;
    tim_boundary_ms = board_millis();
    /* CH3/CH4はpinに出さない比較（frozen）。秒内の150 ms・200 msで割込みを出す */
    TIM2->CCMR2 = 0;
    TIM2->CCR3 = NMEA_START_COUNTS;
    TIM2->CCR4 = OE_ENABLE_COUNTS;
    (void)TIM2->CCR1;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_UIE | TIM_DIER_CC1IE | TIM_DIER_CC3IE | TIM_DIER_CC4IE;
    __enable_irq();
    NVIC_SetPriority(TIM2_IRQn, 0);
    NVIC_EnableIRQ(TIM2_IRQn);
}

/* ISRと共有している値を一貫して読む */
typedef struct {
    uint32_t index;
    uint64_t base;
    uint32_t n_cur;
    uint32_t boundary_ms;
    uint32_t cnt;
} tim_snap_t;

static void tim_snapshot(tim_snap_t *s)
{
    __disable_irq();
    s->index = tim_index;
    s->base = tim_base;
    s->n_cur = tim_n_cur;
    s->boundary_ms = tim_boundary_ms;
    s->cnt = TIM2->CNT;
    if ((TIM2->SR & TIM_SR_UIF) && s->cnt < s->n_cur / 2u) {
        /* 境界を過ぎたがISRがまだ: 次の期間として扱う */
        s->base += s->n_cur;
        s->index++;
        s->n_cur = tim_n_next;
    }
    __enable_irq();
}

/* ---- 状態遷移 ---- */

static void set_state(uint8_t s, const char *why)
{
    state = s;
    set_event(why);
}

static void dpll_mode(ts_dpll_mode_t mode, int32_t gate)
{
    __disable_irq();
    dpll.mode = mode;
    dpll.gate = gate;
    __enable_irq();
}

static void dpll_align(uint64_t pos, align_purpose_t purpose)
{
    __disable_irq();
    dpll.align_pos = pos;
    dpll.align_pending = true;
    dpll.align_done = false;
    __enable_irq();
    align_purpose = purpose;
}

static void reset_counters(void)
{
    good_run = 0;
    aligned_run = 0;
    pps_miss = 0;
    nmea_bad = 0;
    mismatch = 0;
}

static void jetson_complete(uint8_t code)
{
    if (jetson_wait) {
        jetson_wait = false;
        jetson_done = true;
        jetson_code = code;
    }
}

/* calendar書込みの途中なら打ち切る（INITを抜け、calendarはそのまま数え続ける） */
static void rtc_abort(void)
{
    if (rtc_state == RTCS_INIT) {
        __disable_irq();
        rtc_exit_pending = false;
        __enable_irq();
        rtck_write_end();
    }
    rtc_state = RTCS_IDLE;
    rtc_sync_request = false;
}

static void enter_invalid(const char *why)
{
    outputs_off();
    time_valid = false;
    origin = ORIGIN_NONE;
    stepping = false;
    reenable_pending = false;
    f_seeded = false;
    align_purpose = ALIGN_NONE;
    rtc_boot_pending = false;
    rtc_abort();
    jetson_complete(TS_JP_NACK_RTC_WRITE);
    reset_counters();
    dpll_mode(TS_DPLL_HOLD, 0);
    set_state(TS_JP_STATE_INIT_INVALID, why);
}

static void enter_holdover(const char *why)
{
    dpll_mode(TS_DPLL_HOLD, 0);
    reset_counters();
    stepping = false;
    set_state(TS_JP_STATE_TCXO_HOLDOVER, why);
}

static void enter_fault(const char *why)
{
    outputs_off();
    dpll_mode(TS_DPLL_HOLD, 0);
    stepping = false;
    reenable_pending = false;
    reset_counters();
    set_state(TS_JP_STATE_FAULT, why);
}

static void enter_locked(const gnss_rec_t *r, const char *why)
{
    utc_offset = r->label - (int64_t)r->match_index;
    time_valid = true;
    origin = ORIGIN_GNSS;
    stepping = false;
    rtc_boot_pending = false;
    f_verified = true;
    rtc_abort();             /* 書きかけがあれば前の時刻なので捨てる */
    rtc_sync_request = true; /* RTCをGNSS時刻へ合わせ、検証済みのTCXO補正値をbackupへ書く */
    reset_counters();
    dpll_mode(TS_DPLL_TRACK, TRACK_GATE);
    outputs_on_request();
    set_state(TS_JP_STATE_GNSS_LOCKED, why);
}

/* High-Zにして、このGNSS PPSへ秒境界を揃え直す（P-05、または取得中） */
static void start_gnss_step(const gnss_rec_t *r, bool keep_outputs_off)
{
    if (keep_outputs_off) {
        outputs_off();
    }
    rtc_abort(); /* 秒の割当てが変わるので、書きかけのcalendarは捨てる */
    dpll_mode(TS_DPLL_TRACK, 0);
    dpll_align(r->pps_abs - (uint64_t)PPS_SETPOINT, ALIGN_GNSS);
    aligned_run = 0;
}

static bool settled(const gnss_rec_t *r)
{
    return (int32_t)(r->match_index - settle_index) >= 0;
}

static int64_t dpll_freq_q16(void)
{
    int64_t f;
    __disable_irq();
    f = dpll.freq_q16;
    __enable_irq();
    return f;
}

static bool gnss_lost(const gnss_rec_t *r)
{
    if (r->rmc && r->status == 'V') {
        set_event("RMC status V");
        return true;
    }
    if (pps_miss >= MISS_LIMIT) {
        set_event("GNSS PPS 2回連続欠落");
        return true;
    }
    if (nmea_bad >= MISS_LIMIT) {
        set_event("RMC 2回連続無効・欠落");
        return true;
    }
    return false;
}

static void gnss_second(const gnss_rec_t *r)
{
    const bool pps_ok = r->pps && r->interval_ok;
    const bool nmea_ok = r->rmc && r->rmc_valid;
    bool continuous = true;

    if (r->pps) {
        last_err = r->err;
        last_err_valid = true;
    }
    if (nmea_ok) {
        continuous = !last_label_valid || r->label == last_label + 1;
        last_label = r->label;
        last_label_valid = true;
    } else {
        last_label_valid = false; /* 有効なRMCのない秒で連続性を切る */
    }
    const bool good = pps_ok && nmea_ok && continuous;
    good_run = good ? good_run + 1u : 0u;
    pps_miss = (r->pps && (state != TS_JP_STATE_GNSS_LOCKED || abs32(r->err) <= TRACK_GATE)) ? 0u : pps_miss + 1u;
    nmea_bad = nmea_ok ? 0u : nmea_bad + 1u;

    /* 取得中はPPS間隔から周波数を1回だけ与えておく（以後はTRACKが詰める）。backupの検証済みの値があればそれを使う */
    if (pps_ok && !f_seeded && !time_valid && !f_verified) {
        __disable_irq();
        ts_dpll_set_freq(&dpll, (r->interval - NOM) * 65536);
        __enable_irq();
        f_seeded = true;
    }

    switch (state) {
    case TS_JP_STATE_INIT_INVALID:
    case TS_JP_STATE_GNSS_ACQUIRE:
        if (!time_valid && align_purpose != ALIGN_TIME) {
            if (r->pps || r->rmc) {
                state = TS_JP_STATE_GNSS_ACQUIRE;
            }
            /* 有効なRMCを伴うPPSでだけ揃える（GNSSが確立すればRTC起動より優先する） */
            if (r->pps && nmea_ok && align_purpose == ALIGN_NONE && settled(r)) {
                rtc_boot_pending = false;
                if (abs32(r->err) > ACQ_STEP_ERR) {
                    start_gnss_step(r, true);
                    set_event("GNSS PPSへ秒境界を揃える");
                } else {
                    dpll_mode(TS_DPLL_TRACK, 0);
                }
            }
            aligned_run = (good && abs32(r->err) <= LOCK_ERR && align_purpose == ALIGN_NONE && settled(r))
                              ? aligned_run + 1u
                              : 0u;
            if (aligned_run >= LOCK_COUNT) {
                enter_locked(r, "GNSS 5回連続正常でlock");
            }
        }
        break;

    case TS_JP_STATE_GNSS_LOCKED:
        if (gnss_lost(r)) {
            enter_holdover(last_event);
            break;
        }
        if (good && abs32(r->err) <= TRACK_GATE) {
            const int64_t expected = (int64_t)r->match_index + utc_offset;
            mismatch = r->label == expected ? 0u : mismatch + 1u;
            if (mismatch >= MISMATCH_LIMIT) {
                enter_fault("UTC矛盾（RMCの秒が生成PPSと合わない）");
            }
        }
        break;

    case TS_JP_STATE_TCXO_HOLDOVER:
    case TS_JP_STATE_JETSON_SET:
        if (state == TS_JP_STATE_JETSON_SET && (reenable_pending || align_purpose == ALIGN_TIME)) {
            break; /* Jetson時刻のstep中 */
        }
        if (good_run >= LOCK_COUNT) {
            const int64_t label_err = r->label - ((int64_t)r->match_index + utc_offset);
            const bool small = label_err == 0 && abs32(r->err) <= STEP_ERR;
            if (origin == ORIGIN_GNSS) {
                if (label_err != 0 || abs32(r->err) > FAULT_ERR) {
                    enter_fault("GNSS復帰時の位相差が100 ms超（P-03）");
                    break;
                }
            } else if (!small) {
                /* P-05: GNSS以外の時刻からの復帰で1 ms超: High-Zにしてstepし、2秒正常の後にenable */
                stepping = true;
                start_gnss_step(r, true);
                reset_counters();
                set_state(TS_JP_STATE_RESYNC, "GNSS取得: 1 ms超のためHigh-Zにしてstep（P-05）");
                break;
            }
            stepping = false;
            reset_counters();
            dpll_mode(TS_DPLL_TRACK, 0);
            set_state(TS_JP_STATE_RESYNC, "GNSS 5回連続正常: slewで合わせる");
        }
        break;

    case TS_JP_STATE_RESYNC:
        if (gnss_lost(r)) {
            if (stepping) {
                enter_invalid("step中にGNSSを失った");
            } else {
                enter_holdover(last_event);
            }
            break;
        }
        if (stepping) {
            if (align_purpose != ALIGN_NONE || !settled(r)) {
                break; /* alignの処理待ち */
            }
            aligned_run = (good && abs32(r->err) <= LOCK_ERR) ? aligned_run + 1u : 0u;
            if (aligned_run >= REENABLE_COUNT) {
                enter_locked(r, "step後2秒正常: 出力を再enable");
            }
            break;
        }
        if (good) {
            const int64_t label_err = r->label - ((int64_t)r->match_index + utc_offset);
            if (label_err != 0 || (origin == ORIGIN_GNSS && abs32(r->err) > FAULT_ERR)) {
                enter_fault("RESYNC中の位相差が100 ms超（P-03）");
                break;
            }
        }
        aligned_run = (good && abs32(r->err) <= LOCK_ERR) ? aligned_run + 1u : 0u;
        if (aligned_run >= LOCK_COUNT) {
            enter_locked(r, "位相差収束: lock");
        }
        break;

    default: /* FAULT、FAULT_CLOCK: GNSSでは抜けない */
        break;
    }
}

static void close_rec(void)
{
    rec_open = false;
    last_rec_ms = board_millis();
    gnss_second(&rec);
}

static void on_capture_event(const cap_ev_t *e)
{
    bool next;

    if (rec_open) {
        close_rec();
    }
    memset(&rec, 0, sizeof(rec));
    rec.pps = true;
    rec.pps_ms = e->ms;
    rec.pps_abs = e->abs;
    rec.err = ts_dpll_edge_error(e->ccr, e->n, PPS_SETPOINT, &next);
    rec.match_index = e->index + (next ? 1u : 0u);
    if (prev_pps_valid && (uint32_t)(e->ms - prev_pps_ms) < 1500u) {
        rec.interval = (int64_t)(e->abs - prev_pps_abs);
        const int64_t m = NOM + dpll_freq_q16() / 65536;
        rec.interval_ok = rec.interval >= m - INTERVAL_TOL && rec.interval <= m + INTERVAL_TOL;
    }
    prev_pps_abs = e->abs;
    prev_pps_ms = e->ms;
    prev_pps_valid = true;
    rec_open = true;
    last_gnss_activity_ms = e->ms;
}

static void on_rmc(const ts_nmea_rmc_t *rmc)
{
    const uint32_t now = board_millis();
    int64_t unix_sec = 0;
    const bool time_ok = !rmc->leap_second && rmc->centisecond == 0u && ts_datetime_to_unix(&rmc->utc, &unix_sec);

    rmc_count++;
    last_rmc_status = rmc->status;
    last_gnss_activity_ms = now;
    if (time_ok && rmc->status == 'A') {
        last_fix = *rmc;
        last_fix_valid = true;
    }
    if (prev_pps_valid) {
        last_rmc_delay_ms = now - prev_pps_ms; /* 対応付けに失敗しているときの診断にも使う */
    }
    if (rec_open && rec.pps && !rec.rmc && (uint32_t)(now - rec.pps_ms) <= RMC_MAX_DELAY_MS) {
        /* 直前のPPSの時刻としてまとめる */
    } else {
        if (rec_open) {
            close_rec();
        }
        memset(&rec, 0, sizeof(rec)); /* PPSのないRMC */
    }
    rec.rmc = true;
    rec.status = rmc->status;
    rec.rmc_valid = time_ok && rmc->status == 'A';
    rec.label = unix_sec;
    close_rec();
}

static void gnss_poll(void)
{
    const uint32_t now = board_millis();

    /* 受信byteをNMEAへ組み立てる */
    while (rx_tail != rx_head) {
        const char c = (char)rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1u) % RX_RING);
        const char *s;
        size_t len;
        const ts_nmea_rx_status_t st = ts_nmea_rx_push(&nmea_rx, c, &s, &len);
        if (st == TS_NMEA_RX_DISCARDED) {
            nmea_discarded++;
        } else if (st == TS_NMEA_RX_SENTENCE) {
            if (ts_nmea_verify(s, len, NULL) != TS_NMEA_OK) {
                nmea_bad_checksum++;
                continue;
            }
            nmea_good++;
            have_good_sentence = true;
            last_good_sentence_ms = now;
            ts_nmea_rmc_t rmc;
            if (ts_nmea_parse_rmc(s, len, &rmc) == TS_NMEA_OK) {
                on_rmc(&rmc);
            }
        }
    }

    /* baud自動選択: 有効なNMEAが3秒来なければ次のbaudへ */
    if (gnss_autobaud) {
        const uint32_t ref = have_good_sentence ? last_good_sentence_ms : gnss_baud_since_ms;
        if ((uint32_t)(now - ref) > GNSS_AUTOBAUD_MS) {
            gnss_baud_idx = (gnss_baud_idx + 1u) % (sizeof(gnss_bauds) / sizeof(gnss_bauds[0]));
            have_good_sentence = false;
            gnss_set_baud(gnss_bauds[gnss_baud_idx]);
        }
    }

    /* PPSの後RMCが来ない、またはGNSSから何も来ない秒も1秒として数える */
    if (rec_open && (uint32_t)(now - rec.pps_ms) > RMC_MAX_DELAY_MS) {
        close_rec();
    } else if (!rec_open && (uint32_t)(now - last_rec_ms) > GNSS_SILENT_MS) {
        memset(&rec, 0, sizeof(rec));
        close_rec();
    }
    if (!time_valid && state == TS_JP_STATE_GNSS_ACQUIRE &&
        (uint32_t)(now - last_gnss_activity_ms) > GNSS_ACTIVE_MS) {
        state = TS_JP_STATE_INIT_INVALID;
    }
}

/* ---- alignの完了 ---- */

static void align_poll(void)
{
    bool done;
    uint32_t idx = 0;
    int32_t cycles = 0;

    __disable_irq();
    done = dpll.align_done;
    if (done) {
        dpll.align_done = false;
        idx = dpll.align_index;
        cycles = dpll.align_cycles;
    }
    __enable_irq();
    if (!done) {
        return;
    }
    if (align_purpose == ALIGN_TIME) {
        utc_offset = align_label + cycles - (int64_t)idx;
        time_valid = true;
        origin = align_origin;
        reenable_at = idx + REENABLE_COUNT;
        reenable_pending = true;
        rtc_boot_checks = 0;
        set_event(origin == ORIGIN_RTC ? "RTCの秒境界へ揃えた" : "Jetson時刻へstep");
    } else {
        set_event("GNSS PPSへ揃えた");
    }
    align_purpose = ALIGN_NONE;
    settle_index = idx;
    prev_pps_valid = false; /* 揃える前後のPPS間隔は使わない */
}

static void reenable_poll(void)
{
    if (!reenable_pending) {
        return;
    }
    tim_snap_t s;
    tim_snapshot(&s);
    if ((int32_t)(s.index - reenable_at) < 0) {
        return;
    }
    if (origin == ORIGIN_RTC) {
        /* RTC-only起動: RTCの秒が2回続けて生成PPSの秒と一致して（単調に更新されて）からenableする */
        if (rtc_boot_checks < REENABLE_COUNT) {
            return;
        }
        reenable_pending = false;
        outputs_on_request();
        enter_holdover("RTCで起動: 出力をenable（RMC=V）");
        return;
    }
    reenable_pending = false;
    outputs_on_request();
    enter_holdover("Jetson時刻のstep後2秒: 出力をenable");
    if (rtck_clock_ready()) {
        rtc_sync_request = true; /* ACKはRTCの書込みとbackup commitの後 */
    } else {
        jetson_complete(TS_JP_RESULT_OK); /* LSEが動いていない（RTC_INVALID）: 時刻だけ使う */
    }
}

/* ---- 内蔵RTC ---- */

static bool rtc_payload_valid(void)
{
    return rtc_written || rtck_boot()->valid;
}

/* backup payload（同期時刻、TCXO補正値）を書く。CRCは最後 */
static void rtc_commit(int64_t label)
{
    if (label < 0 || label > (int64_t)UINT32_MAX) {
        return;
    }
    rtck_commit((uint32_t)label, (int32_t)dpll_freq_q16(), f_verified);
    rtc_last_commit_ms = board_millis();
}

static void rtc_failed(const char *why)
{
    rtc_abort();
    rtc_note = why;
    rtc_fail++;
    if (rtc_fail < RTC_RETRY_LIMIT) {
        rtc_sync_request = true;
        return;
    }
    /* 何度やっても書けない: Jetson SETならNACK_RTC_WRITEとして出力をHigh-Zにする（jetson_time_protocol） */
    if (jetson_wait) {
        enter_invalid("RTC書込み失敗（NACK_RTC_WRITE）");
    }
}

/* LSEのTCXOに対する偏差（ppb）。測定がRTC_CAL_MIN_SECONDSに満たなければfalse */
static bool lse_vs_tcxo_ppb(int32_t *ppb)
{
    board_lse_status_t st;

    board_lse_status(&st);
    if (!st.ready || st.captures == 0u || st.ticks / 48000000u < RTC_CAL_MIN_SECONDS) {
        return false;
    }
    /* 8周期の期待値は48 MHz×8/32768 = 11718.75 count。1/4 count単位で偏差を求める（console.cと同じ） */
    const int64_t diff_q = (int64_t)46875 * (int64_t)st.captures - 4 * (int64_t)st.ticks;
    *ppb = (int32_t)(diff_q * 1000000000LL / (4 * (int64_t)st.ticks));
    return true;
}

static void rtc_poll(void)
{
    const uint32_t now = board_millis();
    tim_snap_t s;

    if (rtck_config_changed() && rtc_payload_valid() && time_valid) {
        tim_snapshot(&s);
        rtc_commit((int64_t)s.index + utc_offset); /* configuration CRCを合わせる */
    }
    rtck_task();
    if (jetson_wait && (uint32_t)(now - jetson_wait_ms) > JETSON_ACK_TIMEOUT_MS) {
        rtc_note = "Jetson SETの適用が時間内に終わらない";
        jetson_complete(TS_JP_NACK_RTC_WRITE);
    }

    /* RTC-only起動: RTCの秒境界（1 Hz wakeup）へ生成PPSを揃える。calendarは秒の途中で読む */
    if (rtc_boot_pending && align_purpose == ALIGN_NONE && rtc_tick_count > 0u) {
        const uint32_t since = now - rtc_tick_ms;
        int64_t u;
        uint32_t sub;
        if (since >= 100u && since < 700u && rtck_read(&u, &sub)) {
            const int32_t d = (int32_t)(sub * 1000u / 256u) - (int32_t)since; /* SSRとwakeupからの時間が合うこと */
            if (d > -50 && d < 50) {
                rtc_boot_pending = false;
                align_label = u;
                align_origin = ORIGIN_RTC;
                dpll_mode(TS_DPLL_HOLD, 0);
                dpll_align(rtc_tick_abs, ALIGN_TIME);
                set_event("RTC有効: RTCの秒境界へ揃える");
            }
        }
    }

    /* 以下は秒の途中（境界から100～700 ms）で1周期に1回 */
    tim_snapshot(&s);
    const uint32_t into = now - s.boundary_ms;
    if (s.index == rtc_checked_index || into < 100u || into >= 700u) {
        return;
    }
    rtc_checked_index = s.index;
    const int64_t ours = (int64_t)s.index + utc_offset;
    int64_t rtc_now = 0;
    const bool rtc_ok = rtck_read(&rtc_now, NULL);

    /* RTCで揃えた後: RTCの秒が生成PPSの秒と一致し続けることを確かめる */
    if (reenable_pending && origin == ORIGIN_RTC && (int32_t)(s.index - (reenable_at - REENABLE_COUNT)) >= 0) {
        if (rtc_ok && rtc_now == ours) {
            rtc_boot_checks++;
        } else {
            enter_invalid("RTCの秒が生成PPSと合わない（単調に更新されない）");
            return;
        }
    }

    switch (rtc_state) {
    case RTCS_IDLE:
        if (rtc_sync_request && time_valid && !stepping && !reenable_pending && align_purpose == ALIGN_NONE) {
            if (!rtck_clock_ready()) {
                rtc_sync_request = false;
                rtc_note = "LSEが動いていない（RTC_INVALID）";
                jetson_complete(TS_JP_RESULT_OK);
                break;
            }
            const uint32_t target = s.index + 1u;
            if (rtck_write_begin((int64_t)target + utc_offset)) {
                __disable_irq();
                rtc_exit_index = target;
                rtc_exit_pending = true;
                __enable_irq();
                rtc_state = RTCS_INIT;
                rtc_sync_request = false;
            } else {
                rtc_failed("INITへ入れない");
            }
        }
        break;
    case RTCS_INIT:
        if (!rtc_exit_pending) {
            rtck_write_end();
            rtc_state = RTCS_VERIFY;
            rtc_verify_index = s.index + 1u;
        } else if ((int32_t)(s.index - rtc_exit_index) > 0) {
            rtc_failed("INITを抜ける秒境界を逃した");
        }
        break;
    case RTCS_VERIFY:
        if ((int32_t)(s.index - rtc_verify_index) >= 0) {
            if (rtc_ok && rtc_now == ours) {
                rtc_state = RTCS_IDLE;
                rtc_fail = 0;
                rtc_written = true;
                rtc_last_sync_ms = now;
                rtc_note = "calendarを書いて確かめた";
                rtc_commit(ours);
                jetson_complete(TS_JP_RESULT_OK);
            } else {
                rtc_failed("書込み後の読み返しが合わない");
            }
        }
        break;
    }
    if (rtc_state != RTCS_IDLE || !time_valid || stepping || reenable_pending || align_purpose != ALIGN_NONE) {
        return;
    }

    /* 運用中の監視: RTCが生成PPSの秒・秒境界からずれたら書き直す */
    if (rtck_clock_ready() && (uint32_t)(now - rtc_last_sync_ms) > 3000u && !rtc_sync_request) {
        if (!rtck_calendar_valid()) {
            rtc_sync_request = true; /* 初回（BDRSTの後）: まだ書いていない */
        } else if (!rtc_ok || rtc_now != ours) {
            rtc_note = "RTCの秒が生成PPSと違うので書き直す";
            rtc_sync_request = true;
        } else if ((uint32_t)(now - rtc_tick_ms) < 1500u &&
                   (rtc_tick_err > RTC_RESYNC_ERR || rtc_tick_err < -RTC_RESYNC_ERR)) {
            rtc_note = "RTCの秒境界が5 ms以上ずれたので書き直す";
            rtc_sync_request = true;
        }
    }

    /* GNSS_LOCKED中: TCXO補正値を定期的にbackupへ。LSEの偏差からsmooth calibrationを決める */
    if (state == TS_JP_STATE_GNSS_LOCKED && rtc_payload_valid()) {
        if ((uint32_t)(now - rtc_last_commit_ms) > RTC_COMMIT_INTERVAL_MS) {
            rtc_commit(ours);
        }
        int32_t lse_ppb;
        if ((uint32_t)(now - rtc_last_cal_ms) > RTC_CAL_INTERVAL_MS && lse_vs_tcxo_ppb(&lse_ppb)) {
            rtc_last_cal_ms = now;
            /* LSEの真の偏差 = TCXO基準で測った偏差 + TCXO自身の偏差（GNSSで求めたF） */
            const int32_t tcxo_ppb = (int32_t)(dpll_freq_q16() * 1000 / 65536 * 1000 / 48);
            if (rtck_set_calibration(lse_ppb + tcxo_ppb)) {
                rtc_commit(ours); /* CALRはconfiguration CRCの対象 */
                rtc_note = "smooth calibrationを更新";
            }
        }
    }
}

/* ---- NMEA出力の準備（秒境界の後、200 msまでに） ---- */

static void prepare_nmea(uint32_t index)
{
    if (!time_valid || !(oe_on || oe_request) || stepping) {
        return;
    }
    const bool locked = state == TS_JP_STATE_GNSS_LOCKED;
    const int64_t s = (int64_t)index + utc_offset;
    ts_nmea_rmc_out_t o;

    memset(&o, 0, sizeof(o));
    o.talker[0] = 'G';
    o.talker[1] = 'P';
    o.status = locked ? 'A' : 'V';
    o.position = locked && last_fix_valid ? &last_fix : NULL;
    o.mode = locked ? (last_fix_valid && last_fix.mode != '\0' ? last_fix.mode : 'A') : 'N';
    for (unsigned i = 0; i < CH_COUNT; i++) {
        tx_t *t = &tx[i];
        if (!(out_mask & (1u << i))) {
            continue;
        }
        if (t->busy) {
            t->skipped++;
            continue;
        }
        if (!ts_unix_to_datetime(s + (channels[i].plus_one ? 1 : 0), &o.utc)) {
            continue;
        }
        o.frac_digits = channels[i].frac_digits;
        const size_t n = ts_nmea_build_rmc(t->buf, sizeof(t->buf), &o);
        if (n == 0u) {
            continue;
        }
        __disable_irq();
        t->len = (uint8_t)n;
        t->index = index;
        t->ready = true;
        __enable_irq();
    }
}

/* ---- Jetson ---- */

ts_jp_result_t timesync_jetson_set(const ts_jp_time_set_t *ts, uint32_t rx_ms, bool force)
{
    if (state == TS_JP_STATE_GNSS_LOCKED || state == TS_JP_STATE_RESYNC) {
        return TS_JP_NACK_GNSS_ACTIVE;
    }
    if (state == TS_JP_STATE_FAULT_CLOCK) {
        return TS_JP_NACK_RTC_WRITE;
    }
    tim_snap_t s;
    tim_snapshot(&s);
    const int32_t elapsed_ms = (int32_t)(rx_ms - s.boundary_ms);
    const bool valid_now = time_valid && state != TS_JP_STATE_FAULT && align_purpose == ALIGN_NONE;

    if (valid_now && !force) {
        /* offset = Jetson − 本基板（ms）。正なら本基板が遅れているので周期を縮める */
        const int64_t ours_ms = ((int64_t)s.index + utc_offset) * 1000 + elapsed_ms;
        const int64_t jetson_ms = ts->unix_sec * 1000 + (int64_t)(ts->nanoseconds / 1000000u);
        const int64_t offset_ms = jetson_ms - ours_ms;
        if (offset_ms > 100 || offset_ms < -100) {
            return TS_JP_NACK_OFFSET_TOO_LARGE;
        }
        __disable_irq();
        dpll.slew = -offset_ms * COUNTS_PER_MS;
        __enable_irq();
        origin = ORIGIN_JETSON;
        set_event("Jetson SET: slew");
        return TS_JP_RESULT_OK;
    }

    /* step: High-Zにして、Jetson時刻の次の整数秒へ秒境界を揃える。ACKはRTC書込みの後（timesync_jetson_result） */
    outputs_off();
    rtc_abort();
    jetson_complete(TS_JP_NACK_RTC_WRITE); /* 前のSETがまだ待っていたら打ち切る */
    time_valid = false;
    stepping = false;
    reenable_pending = false;
    rtc_boot_pending = false;
    reset_counters();
    const uint64_t rx_abs = s.base + (uint64_t)((int64_t)elapsed_ms * COUNTS_PER_MS);
    const uint64_t pos = rx_abs + (uint64_t)((1000000000ull - ts->nanoseconds) * 48u / 1000u);
    align_label = ts->unix_sec + 1;
    align_origin = ORIGIN_JETSON;
    dpll_mode(TS_DPLL_HOLD, 0);
    dpll_align(pos, ALIGN_TIME);
    jetson_wait = true;
    jetson_done = false;
    jetson_wait_ms = board_millis();
    set_state(TS_JP_STATE_JETSON_SET, force ? "Jetson FORCE_SET: step" : "Jetson SET: step");
    return TS_JP_RESULT_OK;
}

bool timesync_jetson_waiting(void)
{
    return jetson_wait;
}

bool timesync_jetson_result(uint8_t *code)
{
    if (!jetson_done) {
        return false;
    }
    jetson_done = false;
    *code = jetson_code;
    return true;
}

/* ---- 状態の取得 ---- */

void timesync_get_status(timesync_status_t *st)
{
    tim_snap_t s;

    tim_snapshot(&s);
    memset(st, 0, sizeof(*st));
    st->state = state;
    /* Jetson stepの直後は、揃えた秒境界（reenable_at − 2）に達するまで時刻を出さない */
    const bool settling = reenable_pending && (int32_t)(s.index - (reenable_at - REENABLE_COUNT)) < 0;
    st->time_valid = time_valid && !stepping && !settling;
    if (st->time_valid) {
        st->unix_sec = (int64_t)s.index + utc_offset;
        st->nanoseconds = (uint32_t)((uint64_t)s.cnt * 1000000000ull / s.n_cur);
        if (st->nanoseconds > 999999999u) {
            st->nanoseconds = 999999999u;
        }
    }
    st->outputs_on = oe_on;
    st->gnss_valid = state == TS_JP_STATE_GNSS_LOCKED || state == TS_JP_STATE_RESYNC;
    /* RTC_VALID: calendarが有効で、起動時の検証に合格したかこのbootで書いて確かめた */
    st->rtc_valid = rtck_calendar_valid() && rtc_payload_valid();
}

static const char *state_name(uint8_t s)
{
    static const char *const names[] = {"INIT_INVALID", "GNSS_ACQUIRE", "GNSS_LOCKED", "JETSON_SET",
                                        "TCXO_HOLDOVER", "RESYNC", "FAULT", "FAULT_CLOCK"};
    return s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

/* F（count/秒、Q16）をppbへ */
static int32_t freq_ppb(void)
{
    return (int32_t)(dpll_freq_q16() * 1000 / 65536 * 1000 / 48);
}

static void print_utc(int64_t unix_sec)
{
    ts_datetime_t dt;
    if (ts_unix_to_datetime(unix_sec, &dt)) {
        con_printf("%04u-%02u-%02uT%02u:%02u:%02uZ", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    } else {
        con_printf("-");
    }
}

static void print_signed(int32_t v)
{
    con_printf("%c%" PRIu32, v < 0 ? '-' : '+', (uint32_t)(v < 0 ? -(int64_t)v : v));
}

static void print_ppb(int32_t ppb)
{
    const uint32_t a = (uint32_t)(ppb < 0 ? -(int64_t)ppb : ppb);
    con_printf("%c%" PRIu32 ".%03" PRIu32 " ppm", ppb < 0 ? '-' : '+', a / 1000u, a % 1000u);
}

void timesync_log_line(uint32_t index)
{
    timesync_status_t st;

    (void)index;
    timesync_get_status(&st);
    con_printf("\r[%s] ", state_name(st.state));
    if (st.time_valid) {
        print_utc(st.unix_sec);
    } else {
        con_printf("time invalid");
    }
    con_printf(" out=%s GNSS-PPS=", st.outputs_on ? "on" : "off");
    if (last_err_valid && (uint32_t)(board_millis() - prev_pps_ms) < 1500u) {
        print_signed(last_err);
        con_printf(" cnt");
    } else {
        con_printf("none");
    }
    con_printf(" RMC=%c F=", last_rmc_status);
    print_ppb(freq_ppb());
    con_printf(" good=%u (%s)\r\n> ", good_run, last_event);
}

void timesync_cmd_status(const char *arg)
{
    timesync_status_t st;
    int32_t dl_err, dl_pred;
    bool dl_used;

    (void)arg;
    timesync_get_status(&st);
    __disable_irq();
    dl_used = dpll.last_used;
    dl_err = dpll.last_err;
    dl_pred = dpll.last_pred;
    __enable_irq();
    con_printf("state      %s（%s）\r\n", state_name(st.state), last_event);
    con_printf("UTC        ");
    if (st.time_valid) {
        print_utc(st.unix_sec);
        con_printf(" +%03" PRIu32 " ms（源: %s）\r\n", st.nanoseconds / 1000000u,
                   origin == ORIGIN_GNSS     ? "GNSS"
                   : origin == ORIGIN_JETSON ? "Jetson"
                   : origin == ORIGIN_RTC    ? "内蔵RTC"
                                             : "なし");
    } else {
        con_printf("無効\r\n");
    }
    con_printf("出力       %s（mask 0x%02X%s）\r\n", st.outputs_on ? "enable" : "High-Z", out_mask,
               out_inhibit ? "、out offで停止中" : "");
    con_printf("DPLL       mode=%s F=", dpll.mode == TS_DPLL_TRACK ? "TRACK" : "HOLD");
    print_ppb(freq_ppb());
    con_printf(" 最後の誤差=");
    print_signed(dl_err);
    con_printf(" 予測=");
    print_signed(dl_pred);
    con_printf(" count%s setpoint=%d\r\n", dl_used ? "" : "（未使用）", PPS_SETPOINT);
    con_printf("GNSS       good連続=%u 揃い連続=%u PPS欠落=%u RMC無効=%u 秒不一致=%u\r\n", good_run, aligned_run,
               pps_miss, nmea_bad, mismatch);
    {
        const rtck_boot_t *b = rtck_boot();
        int64_t rtc_now;
        uint32_t sub;
        con_printf("RTC        起動時 %s（%s%s）", b->valid ? "有効" : "無効", b->reason, b->did_bdrst ? "、BDRST済み" : "");
        if (b->valid) {
            con_printf(" 起動時のcalendar ");
            print_utc(b->boot_unix);
        }
        con_printf("\r\n           ");
        if (rtck_read(&rtc_now, &sub)) {
            con_printf("今 ");
            print_utc(rtc_now);
            con_printf(" +%03" PRIu32 " ms、秒境界は生成PPSから", sub * 1000u / 256u);
            print_signed(rtc_tick_err / 48);
            con_printf(" us");
        } else {
            con_printf("calendar未設定%s", rtck_clock_ready() ? "" : "（LSE/RTC clockが動いていない）");
        }
        con_printf("、補正 ");
        print_ppb(rtck_calibration_ppb());
        con_printf("\r\n           TCXO補正値 %s、状態 %s、%s\r\n", f_verified ? "GNSS検証済み" : "未検証",
                   rtc_state == RTCS_IDLE ? (rtc_sync_request ? "書込み待ち" : "待機") : "書込み中", rtc_note);
    }
    for (unsigned i = 0; i < CH_COUNT; i++) {
        con_printf("  %s TX 送信%" PRIu32 " 間に合わず%" PRIu32 "%s\r\n", channels[i].name, tx[i].sent, tx[i].skipped,
                   (out_mask & (1u << i)) ? "" : "（mask）");
    }
    /* 最後に送った文（J3とJ4。J4はUTC+1秒） */
    for (unsigned i = CH_J3; i <= CH_J4; i++) {
        char line[sizeof(tx[0].buf)];
        __disable_irq();
        const bool ok = !tx[i].busy && tx[i].sent > 0u;
        if (ok) {
            memcpy(line, tx[i].buf, sizeof(line));
        }
        __enable_irq();
        if (ok) {
            line[strcspn(line, "\r\n")] = '\0';
            con_printf("  %s 最後のRMC: %s\r\n", channels[i].name, line);
        }
    }
}

void timesync_cmd_gnss(const char *arg)
{
    if (strncmp(arg, "baud ", 5) == 0) {
        const uint32_t b = (uint32_t)strtoul(arg + 5, NULL, 10);
        if (b >= 4800u && b <= 3000000u) {
            gnss_autobaud = false;
            gnss_set_baud(b);
        }
    } else if (strcmp(arg, "auto") == 0) {
        gnss_autobaud = true;
        gnss_set_baud(gnss_bauds[gnss_baud_idx]);
    }
    con_printf("baud       %" PRIu32 "（%s）\r\n", gnss_baud, gnss_autobaud ? "自動選択" : "固定");
    con_printf("受信       %" PRIu32 " byte、UART error %" PRIu32 "、溢れ %" PRIu32 "\r\n", rx_bytes, rx_errors,
               rx_overflow);
    con_printf("NMEA       正常%" PRIu32 " checksum異常%" PRIu32 " 途中切れ%" PRIu32 " RMC%" PRIu32 "（最後 %c）\r\n",
               nmea_good, nmea_bad_checksum, nmea_discarded, rmc_count, last_rmc_status);
    con_printf("PPS        capture %" PRIu32 "回、PPS→RMC %" PRIu32 " ms、最後の位相差 ", pps_captures,
               last_rmc_delay_ms);
    print_signed(last_err);
    con_printf(" count（setpoint %d込み）\r\n", PPS_SETPOINT);
    if (last_fix_valid) {
        con_printf("位置       %s,%c %s,%c\r\n", last_fix.lat, last_fix.lat_hemi ? last_fix.lat_hemi : '-',
                   last_fix.lon, last_fix.lon_hemi ? last_fix.lon_hemi : '-');
    }
    con_printf("usage: gnss [baud <n> | auto]\r\n");
}

void timesync_cmd_out(const char *arg)
{
    if (strcmp(arg, "off") == 0) {
        out_inhibit = true;
        outputs_off();
    } else if (strcmp(arg, "on") == 0) {
        out_inhibit = false;
        if (time_valid && !stepping && state != TS_JP_STATE_FAULT) {
            outputs_on_request();
        }
    } else if (strncmp(arg, "mask ", 5) == 0) {
        out_mask = (uint8_t)(strtoul(arg + 5, NULL, 16) & 0x3Fu);
        if (oe_on) {
            /* maskから外したTXだけHigh-Zにし、加えたものは次のenableで入る */
            GPIOB->BSRR = BOARD_OE_N_PINS & ~oe_bits();
            GPIOB->BRR = oe_bits();
        }
    }
    con_printf("出力 %s、TX mask 0x%02X（bit0=J3 … bit5=J8。PPSは6系統共通）\r\n", oe_on ? "enable" : "High-Z",
               out_mask);
    con_printf("TX %" PRIu32 " baud、J4（VLP）TXINV=%u、J4はUTC+1秒\r\n", (uint32_t)OUT_BAUD,
               (USART4->CR2 & USART_CR2_TXINV) ? 1u : 0u);
    con_printf("usage: out [on | off | mask <hex>]\r\n");
}

void timesync_cmd_reinit(const char *arg)
{
    (void)arg;
    if (state == TS_JP_STATE_FAULT_CLOCK) {
        con_printf("clock異常のためreinitできない（resetで再起動）\r\n");
        return;
    }
    enter_invalid("reinit");
    con_printf("INIT_INVALIDへ戻した（全出力High-Z）\r\n");
}

bool timesync_second_elapsed(uint32_t *index)
{
    if (!second_flag) {
        return false;
    }
    second_flag = false;
    *index = tim_index;
    return true;
}

bool timesync_led(uint32_t now_ms)
{
    const uint32_t t = (uint32_t)(now_ms - tim_boundary_ms);

    switch (state) {
    case TS_JP_STATE_GNSS_LOCKED:
        return t < 100u; /* PPSに合わせて100 ms */
    case TS_JP_STATE_TCXO_HOLDOVER:
    case TS_JP_STATE_RESYNC:
    case TS_JP_STATE_JETSON_SET:
        return t < 100u || (t >= 200u && t < 300u); /* 2回 */
    case TS_JP_STATE_FAULT:
    case TS_JP_STATE_FAULT_CLOCK:
        return now_ms % 200u < 100u; /* 速い点滅 */
    default:
        return now_ms % 1000u < 30u; /* 短く1回 */
    }
}

/* ---- 初期化・main loop ---- */

void timesync_init(void)
{
    if (board_boot_info()->clock_mode != BOARD_CLOCK_PLL_HSE) {
        state = TS_JP_STATE_FAULT_CLOCK;
        set_event("clock異常（HSI診断mode）: 出力はHigh-Zのまま");
        return;
    }
    ts_dpll_init(&dpll, PPS_SETPOINT);
    ts_nmea_rx_init(&nmea_rx);
    uarts_init();
    tim2_start();
    last_rec_ms = board_millis();
    enter_invalid("起動");

    /* backup domainの検証はmainでboard_lse_start()より前に済ませてある（rtck_boot_check） */
    const rtck_boot_t *b = rtck_boot();
    if (b->valid) {
        if (b->freq_verified) {
            ts_dpll_set_freq(&dpll, (int64_t)b->freq_q16); /* 最後にGNSSで検証したTCXO補正値でholdoverする */
            f_verified = true;
        }
        rtc_boot_pending = true;
        set_event("RTC有効: RTCの秒境界を待つ");
    } else {
        rtc_note = b->reason;
        set_event("RTC無効: GNSSまたはJetsonの時刻を待つ");
    }
}

void timesync_task(void)
{
    if (state == TS_JP_STATE_FAULT_CLOCK) {
        return;
    }
    while (cap_tail != cap_head) {
        const cap_ev_t e = cap_q[cap_tail];
        cap_tail = (uint8_t)((cap_tail + 1u) % CAP_Q);
        on_capture_event(&e);
    }
    gnss_poll();
    align_poll();
    rtc_poll();
    reenable_poll();
    if (boundary_flag) {
        boundary_flag = false;
        prepare_nmea(tim_index);
    }
}
