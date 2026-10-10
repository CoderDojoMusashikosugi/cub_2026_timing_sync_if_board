#include "console.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "diag.h"
#ifdef FW_SELFTEST
#include "selftest.h"
#endif
#ifdef FW_APP
#include "timesync.h"
#endif
#include "stm32g0xx.h"
#include "tusb.h"

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "unknown"
#endif
#ifndef FW_BUILD_TYPE
#define FW_BUILD_TYPE "unknown"
#endif

#define LINE_MAX 64u
#ifdef FW_APP
/* 製品firmwareではmain loopがNMEAの準備（秒境界から200 ms以内）も行うので、待ちを短くする */
#define WRITE_TIMEOUT_MS 5u
#else
#define WRITE_TIMEOUT_MS 50u
#endif

static char line[LINE_MAX];
static size_t line_len;
static bool log_enabled = true;
static volatile bool banner_pending;

/* ---- 出力 ---- */

/*
 * DTRを立てないterminalでも読めるよう、USBがconfigureされていれば送る。
 * bufferが一杯のとき、DTRがあれば最大50 ms待ち、なければすぐ捨てる（時刻機能を止めない）。
 */
void console_write(const char *s, size_t n)
{
    const uint32_t start = board_millis();

    while (n > 0u) {
        if (!tud_mounted()) {
            return;
        }
        const uint32_t avail = tud_cdc_write_available();
        if (avail == 0u) {
            if (!tud_cdc_connected()) {
                return;
            }
            tud_cdc_write_flush();
            tud_task();
            if ((uint32_t)(board_millis() - start) > WRITE_TIMEOUT_MS) {
                return; /* hostが読まないときは捨てる（時刻機能を止めない） */
            }
            continue;
        }
        const uint32_t k = n < avail ? (uint32_t)n : avail;
        tud_cdc_write(s, k);
        s += k;
        n -= k;
    }
    tud_cdc_write_flush();
}

void con_printf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        console_write(buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1u);
    }
}

/* terminalがportを開いた（DTRが立った）ら、main loopで起動情報を表示する */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)rts;
    if (itf == 0u && dtr) {
        banner_pending = true;
    }
}

/* 送信を出し切るまで少し待つ（reset/bootloader移行の前に使う） */
void console_drain(uint32_t ms)
{
    const uint32_t start = board_millis();
    while ((uint32_t)(board_millis() - start) < ms) {
        tud_task();
    }
}

/* ---- command ---- */

static const char *clock_status_name(board_clock_status_t s)
{
    switch (s) {
    case BOARD_CLK_OK:
        return "OK";
    case BOARD_CLK_ERR_VOSF:
        return "ERR_VOSF";
    case BOARD_CLK_ERR_LATENCY:
        return "ERR_LATENCY";
    case BOARD_CLK_ERR_HSE:
        return "ERR_HSE (HSERDYにならない: TCXO/U14/PF0を確認)";
    case BOARD_CLK_ERR_PLL:
        return "ERR_PLL";
    case BOARD_CLK_ERR_SWITCH:
        return "ERR_SWITCH";
    case BOARD_CLK_ERR_CSS_RESET:
        return "ERR_CSS_RESET (前回HSE CSSでreset)";
    }
    return "?";
}

static void cmd_help(const char *arg)
{
    (void)arg;
    con_printf("commands:\r\n"
               "  id     MCU ID、UID、build、reset要因\r\n"
               "  clk    clock状態\r\n"
               "  regs   初期化registerの監査（test_plan CK-01等）\r\n"
               "  pps    TIM2（生成PPS、GNSS PPS capture）\r\n"
               "  lse    LSE（X1）の起動時間とTCXOに対する周波数偏差\r\n"
               "  gpio   全I/O pinの設定を想定と照合\r\n"
               "  inputs U4出力（外部入力5系統）の静的レベル\r\n"
               "  rxidle 受信UART 4系統の未接続時の誤受信確認（2秒）\r\n"
               "  log    on/off 毎秒の状態表示\r\n"
               "  dfu    system bootloader（USB DFU）へ移る\r\n"
               "  reset  software reset\r\n");
#ifdef FW_APP
    con_printf("  status 時刻同期の状態（state、UTC、出力、DPLL、各TXの送信数）\r\n"
               "  gnss   GNSS受信（baud、NMEA、PPS→RMCの遅れ、位置）。gnss baud <n> / gnss auto\r\n"
               "  out    出力のenable状態。out on / out off / out mask <hex>（bit0=J3…bit5=J8）\r\n"
               "  reinit FAULTなどからINIT_INVALIDへ戻す（全出力High-Z）\r\n");
#endif
}

static void cmd_id(const char *arg)
{
    (void)arg;
    const board_boot_info_t *b = board_boot_info();
    const uint32_t idcode = DBG->IDCODE;
    const uint32_t dev_id = idcode & DBG_IDCODE_DEV_ID_Msk;
    const uint32_t rev_id = idcode >> DBG_IDCODE_REV_ID_Pos;
    const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
    const uint16_t flash_kb = *(const volatile uint16_t *)FLASHSIZE_BASE;
    const uint32_t csr = b->rcc_csr_at_boot;

    con_printf("build      %s (%s) %s %s\r\n", FW_BUILD_ID, FW_BUILD_TYPE, __DATE__, __TIME__);
    con_printf("DEV_ID     0x%03" PRIX32 "%s\r\n", dev_id, dev_id == 0x467u ? " (STM32G0B1/G0C1)" : " (想定外)");
    con_printf("REV_ID     0x%04" PRIX32 "%s\r\n", rev_id,
               rev_id == 0x1000u ? " (Rev A: option byte変更はES0548 2.2.9に注意)" : "");
    con_printf("UID        %08" PRIX32 "%08" PRIX32 "%08" PRIX32 "\r\n", uid[2], uid[1], uid[0]);
    con_printf("flash      %u KB\r\n", (unsigned)flash_kb);
    con_printf("RCC_CSR    0x%08" PRIX32 " at boot:%s%s%s%s%s%s%s\r\n", csr,
               (csr & RCC_CSR_PWRRSTF) ? " PWR/BOR" : "", (csr & RCC_CSR_PINRSTF) ? " PIN" : "",
               (csr & RCC_CSR_SFTRSTF) ? " SOFT" : "", (csr & RCC_CSR_IWDGRSTF) ? " IWDG" : "",
               (csr & RCC_CSR_WWDGRSTF) ? " WWDG" : "", (csr & RCC_CSR_OBLRSTF) ? " OBL" : "",
               (csr & RCC_CSR_LPWRRSTF) ? " LPWR" : "");
    con_printf("PROGEMPTY  %u%s\r\n", b->flash_empty_at_boot ? 1u : 0u,
               b->flash_empty_at_boot ? " (起動時にclear済み)" : "");
    con_printf("last reset %s\r\n",
               b->fault_marker == BOARD_FAULT_MARKER_CSS              ? "HSE CSS"
               : b->fault_marker == BOARD_FAULT_MARKER_HARDFAULT      ? "HardFault（bootloaderへ退避した）"
               : b->fault_marker == BOARD_FAULT_MARKER_BOOTLOADER_GO ? "bootloaderのGOから起動しresetし直した"
               : b->fault_marker == BOARD_FAULT_MARKER_NMI           ? "NMI（bootloaderへ退避した）"
                                                                      : "-");
}

static void cmd_clk(const char *arg)
{
    (void)arg;
    const board_boot_info_t *b = board_boot_info();

    con_printf("mode       %s\r\n", b->clock_mode == BOARD_CLOCK_PLL_HSE ? "PLL_HSE (TCXO 24 MHz)"
                                                                         : "FALLBACK_HSI (診断。USB=HSI48+CRS)");
    con_printf("status     %s\r\n", clock_status_name(b->clock_status));
    con_printf("HSE ready  %" PRIu32 " ms\r\n", b->hse_ready_ms);
    con_printf("SYSCLK     %" PRIu32 " Hz\r\n", SystemCoreClock);
    con_printf("RCC_CR     0x%08" PRIX32 "\r\n", RCC->CR);
    con_printf("RCC_CFGR   0x%08" PRIX32 "\r\n", RCC->CFGR);
    con_printf("PLLCFGR    0x%08" PRIX32 "\r\n", RCC->PLLCFGR);
    con_printf("CCIPR2     0x%08" PRIX32 "\r\n", RCC->CCIPR2);
    con_printf("FLASH_ACR  0x%08" PRIX32 "\r\n", FLASH->ACR);
    con_printf("PWR_CR1    0x%08" PRIX32 "  PWR_SR2 0x%08" PRIX32 "\r\n", PWR->CR1, PWR->SR2);
}

typedef struct {
    const char *name;
    uint32_t actual;
    uint32_t mask;
    uint32_t expected;
} audit_t;

static uint32_t moder_mask(uint32_t pins)
{
    uint32_t m = 0;
    for (uint32_t pin = 0; pin < 16u; pin++) {
        if (pins & (1u << pin)) {
            m |= 3u << (pin * 2u);
        }
    }
    return m;
}

static uint32_t moder_value(uint32_t pins, uint32_t mode)
{
    uint32_t v = 0;
    for (uint32_t pin = 0; pin < 16u; pin++) {
        if (pins & (1u << pin)) {
            v |= mode << (pin * 2u);
        }
    }
    return v;
}

static void cmd_regs(const char *arg)
{
    (void)arg;
    const uint32_t pll_mask = RCC_PLLCFGR_PLLSRC_Msk | RCC_PLLCFGR_PLLM_Msk | RCC_PLLCFGR_PLLN_Msk |
                              RCC_PLLCFGR_PLLR_Msk | RCC_PLLCFGR_PLLREN | RCC_PLLCFGR_PLLQ_Msk |
                              RCC_PLLCFGR_PLLQEN;
    const uint32_t pll_expected = RCC_PLLCFGR_PLLSRC_HSE | (1u << RCC_PLLCFGR_PLLM_Pos) |
                                  (16u << RCC_PLLCFGR_PLLN_Pos) | (3u << RCC_PLLCFGR_PLLR_Pos) |
                                  RCC_PLLCFGR_PLLREN | (3u << RCC_PLLCFGR_PLLQ_Pos) | RCC_PLLCFGR_PLLQEN;
    const uint32_t cr_on = RCC_CR_HSEBYP | RCC_CR_HSEON | RCC_CR_HSERDY | RCC_CR_CSSON | RCC_CR_PLLON |
                           RCC_CR_PLLRDY;

    /* CRS_CRを読むためにclockを入れる（読むだけで設定は変えない） */
    RCC->APBENR1 |= RCC_APBENR1_CRSEN;
    (void)RCC->APBENR1;
    const audit_t items[] = {
        {"PWR_CR1.VOS=Range1", PWR->CR1, PWR_CR1_VOS_Msk, PWR_CR1_VOS_0},
        {"PWR_SR2.VOSF=0", PWR->SR2, PWR_SR2_VOSF, 0u},
        {"FLASH_ACR.LATENCY=2", FLASH->ACR, FLASH_ACR_LATENCY_Msk, 2u},
        {"RCC_CR HSEBYP/HSEON/HSERDY/CSSON/PLLON/PLLRDY", RCC->CR, cr_on, cr_on},
        {"RCC_CR.HSI48ON=0", RCC->CR, RCC_CR_HSI48ON, 0u},
        {"RCC_PLLCFGR HSE M2 N16 R4 Q4", RCC->PLLCFGR, pll_mask, pll_expected},
        {"RCC_CFGR.SWS=PLLR", RCC->CFGR, RCC_CFGR_SWS_Msk, RCC_CFGR_SWS_1},
        {"RCC_CCIPR2.USBSEL=PLLQ", RCC->CCIPR2, RCC_CCIPR2_USBSEL_Msk, RCC_CCIPR2_USBSEL_1},
        {"CRS_CR.CEN=0", CRS->CR, CRS_CR_CEN, 0u},
        {"VREFBUF_CSR.ENVR=0", VREFBUF->CSR, VREFBUF_CSR_ENVR, 0u},
        {"PWR_CR2.PVM_VDDIO2=110", PWR->CR2, PWR_CR2_PVM_VDDIO2_Msk, 6u << PWR_CR2_PVM_VDDIO2_Pos},
        /* UCPDx_STROBEは書いた瞬間に作用するstrobeで読出しは0のため、書いたことを記録した印で判定する */
        {"UCPD1/2 dead-battery strobe実行", board_boot_info()->ucpd_strobed ? 1u : 0u, 1u, 1u},
        {"SYSCFG_CFGR1 PA11/PA12_RMP=0", SYSCFG->CFGR1, SYSCFG_CFGR1_PA11_RMP | SYSCFG_CFGR1_PA12_RMP, 0u},
        {"GPIOB OE_N ODR=High", GPIOB->ODR, BOARD_OE_N_PINS, BOARD_OE_N_PINS},
        {"GPIOB OE_N MODER=output", GPIOB->MODER, moder_mask(BOARD_OE_N_PINS), moder_value(BOARD_OE_N_PINS, 1u)},
        {"PA6 TP_FW_TRACE MODER=analog", GPIOA->MODER, 3u << 12, 3u << 12},
        {"PA6 TP_FW_TRACE PUPDR=none", GPIOA->PUPDR, 3u << 12, 0u},
        {"TIM2_ARR=47999999", TIM2->ARR, 0xFFFFFFFFu, 47999999u},
        {"TIM2_CCR2=4800000", TIM2->CCR2, 0xFFFFFFFFu, 4800000u},
        {"TIM2_CR1.ARPE/CEN", TIM2->CR1, TIM_CR1_ARPE | TIM_CR1_CEN, TIM_CR1_ARPE | TIM_CR1_CEN},
        {"TIM2 CH2 PWM1/OC2PE", TIM2->CCMR1, TIM_CCMR1_OC2M_Msk | TIM_CCMR1_OC2PE, (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE},
        {"TIM2 CH1 input TI1", TIM2->CCMR1, TIM_CCMR1_CC1S_Msk, TIM_CCMR1_CC1S_0},
        {"TIM2 CH1 falling, CH2 active high", TIM2->CCER,
         TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP | TIM_CCER_CC2E | TIM_CCER_CC2P,
         TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC2E},
        {"PC5 AF2 (TIM2_CH2)", GPIOC->AFR[0], GPIO_AFRL_AFSEL5_Msk, 2u << GPIO_AFRL_AFSEL5_Pos},
        {"PA5 AF2 (TIM2_CH1)", GPIOA->AFR[0], GPIO_AFRL_AFSEL5_Msk, 2u << GPIO_AFRL_AFSEL5_Pos},
    };
    unsigned ng = 0;

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        const bool ok = (items[i].actual & items[i].mask) == items[i].expected;
        ng += ok ? 0u : 1u;
        con_printf("%s  %-46s actual=0x%08" PRIX32 "\r\n", ok ? "OK" : "NG", items[i].name,
                   items[i].actual & items[i].mask);
    }
    con_printf("RCC_BDCR   0x%08" PRIX32 " (RTCは未実装のため参考値)\r\n", RCC->BDCR);
    con_printf("result: %u NG / %u\r\n", ng, (unsigned)(sizeof(items) / sizeof(items[0])));
}

static void cmd_pps(const char *arg)
{
    (void)arg;
    uint32_t count;
    uint32_t last;

    board_pps_stats(&count, &last);
    con_printf("TIM2 CNT   %" PRIu32 " / ARR %" PRIu32 "\r\n", TIM2->CNT, TIM2->ARR);
    con_printf("GNSS PPS   capture %" PRIu32 " 回", count);
    if (count > 0u) {
        const uint32_t ns = (uint32_t)((uint64_t)last * 1000000000u / SystemCoreClock);
        con_printf("、最後のCCR1 %" PRIu32 "（生成PPSの立上りから %" PRIu32 " ns）", last, ns);
    }
    con_printf("\r\n");
}

/* TCXOに対するLSEの偏差[ppb]。測定がなければfalse */
static bool lse_offset_ppb(const board_lse_status_t *st, int64_t *ppb)
{
    if (!st->ready || st->captures == 0u || st->ticks == 0u) {
        return false;
    }
    /* 8周期の期待値は48 MHz×8/32768 = 11718.75 count。1/4 count単位の整数で偏差を求める */
    const int64_t diff_q = (int64_t)46875 * (int64_t)st->captures - 4 * (int64_t)st->ticks;
    *ppb = diff_q * 1000000000LL / (4 * (int64_t)st->ticks);
    return true;
}

static void print_ppm(int64_t ppb)
{
    const int64_t a = ppb < 0 ? -ppb : ppb;
    con_printf("%c%" PRIu32 ".%03" PRIu32 " ppm", ppb < 0 ? '-' : '+', (uint32_t)(a / 1000), (uint32_t)(a % 1000));
}

static void cmd_lse(const char *arg)
{
    (void)arg;
    board_lse_status_t st;
    int64_t ppb;

    board_lse_status(&st);
    con_printf("RCC_BDCR   0x%08" PRIX32 " (LSEDRV=%" PRIu32 ", LSECSSON=%u)\r\n", st.bdcr,
               (st.bdcr & RCC_BDCR_LSEDRV_Msk) >> RCC_BDCR_LSEDRV_Pos, (st.bdcr & RCC_BDCR_LSECSSON) ? 1u : 0u);
    if (!st.ready) {
        con_printf("LSE        %s\r\n", st.started ? "起動待ち" : "起動しない（5 s timeout）");
        return;
    }
    if (st.was_running) {
        con_printf("LSE        ready（reset前から動作中。起動時間は電源投入後の値を見る）\r\n");
    } else {
        con_printf("LSE        ready（LSEONから%" PRIu32 " ms）\r\n", st.ready_ms);
    }
    if (!lse_offset_ppb(&st, &ppb)) {
        con_printf("周波数     測定なし（clock診断modeでは測らない）\r\n");
        return;
    }
    con_printf("周波数     TCXO比 ");
    print_ppm(ppb);
    con_printf("（%" PRIu32 "区間、約%" PRIu32 " s、取りこぼし%" PRIu32 "）\r\n", st.captures,
               (uint32_t)(st.ticks / 48000000u), st.overcaptures);
}

static void cmd_dfu(const char *arg)
{
    (void)arg;
    con_printf("system bootloaderへ移ります（USB DFU）\r\n");
    console_drain(50);
    tud_disconnect();
    board_delay_ms(20);
    board_request_bootloader();
}

static void cmd_reset(const char *arg)
{
    (void)arg;
    con_printf("reset\r\n");
    console_drain(50);
    tud_disconnect();
    board_delay_ms(20);
    NVIC_SystemReset();
}

static void cmd_log(const char *arg)
{
    if (strcmp(arg, "on") == 0) {
        log_enabled = true;
    } else if (strcmp(arg, "off") == 0) {
        log_enabled = false;
    }
    con_printf("log %s（毎秒の状態表示。log on / log off）\r\n", log_enabled ? "on" : "off");
}

static void execute(char *cmd)
{
    static const struct {
        const char *name;
        void (*fn)(const char *arg);
    } table[] = {
        {"help", cmd_help}, {"id", cmd_id},   {"clk", cmd_clk}, {"regs", cmd_regs},   {"pps", cmd_pps},
        {"lse", cmd_lse},   {"log", cmd_log}, {"gpio", diag_gpio}, {"inputs", diag_inputs},
        {"rxidle", diag_rxidle}, {"dfu", cmd_dfu}, {"reset", cmd_reset},
#ifdef FW_APP
        {"status", timesync_cmd_status}, {"gnss", timesync_cmd_gnss}, {"out", timesync_cmd_out},
        {"reinit", timesync_cmd_reinit},
#endif
#ifdef FW_SELFTEST
        {"ut", selftest_ut},     {"fault", selftest_fault}, {"loop", selftest_loop},
        {"ppsloop", selftest_ppsloop},
#endif
    };

    /* 1語目をcommand、空白の後ろを引数とする */
    char *arg = strchr(cmd, ' ');
    if (arg != NULL) {
        *arg++ = '\0';
        while (*arg == ' ') {
            arg++;
        }
    } else {
        arg = cmd + strlen(cmd);
    }
    if (cmd[0] != '\0') {
        size_t i = 0;
        while (i < sizeof(table) / sizeof(table[0]) && strcmp(cmd, table[i].name) != 0) {
            i++;
        }
        if (i < sizeof(table) / sizeof(table[0])) {
            table[i].fn(arg);
        } else {
            con_printf("unknown command: %s（helpで一覧）\r\n", cmd);
        }
    }
    con_printf("> ");
}

static void print_banner(void)
{
    const board_boot_info_t *b = board_boot_info();
    const uint32_t rev_id = DBG->IDCODE >> DBG_IDCODE_REV_ID_Pos;

#ifdef FW_APP
    con_printf("\r\ntiming_sync_if firmware %s (%s %s)\r\n", FW_BUILD_ID, __DATE__, __TIME__);
    con_printf("REV_ID 0x%04" PRIX32 "、clock %s\r\n", rev_id,
               b->clock_mode == BOARD_CLOCK_PLL_HSE ? "PLL_HSE 48 MHz" : "FALLBACK_HSI（TCXO系の異常、出力High-Z）");
#else
    con_printf("\r\ntiming_sync_if bring-up firmware %s (%s %s)\r\n", FW_BUILD_ID, __DATE__, __TIME__);
    con_printf("REV_ID 0x%04" PRIX32 "、clock %s、外部出力はすべてHigh-Z\r\n", rev_id,
               b->clock_mode == BOARD_CLOCK_PLL_HSE ? "PLL_HSE 48 MHz" : "FALLBACK_HSI（TCXO系の異常）");
#endif
    con_printf("helpでcommand一覧。毎秒の状態表示は log off で止まる\r\n> ");
}

/* 生成PPSの秒境界ごとに1行の状態を出す。入力途中の行があるときは崩さないよう出さない */
#ifdef FW_APP
static void log_second(uint32_t seconds)
{
    if (log_enabled && line_len == 0u && tud_cdc_connected()) {
        timesync_log_line(seconds);
    }
}
#else
static void log_second(uint32_t seconds)
{
    const board_boot_info_t *b = board_boot_info();
    board_lse_status_t st;
    uint32_t count;
    uint32_t last;
    int64_t ppb;

    if (!log_enabled || line_len > 0u || !tud_cdc_connected()) {
        return;
    }
    board_pps_stats(&count, &last);
    board_lse_status(&st);

    con_printf("\r[%6" PRIu32 " s] clock=%s GNSS-PPS=%" PRIu32, seconds,
               b->clock_mode == BOARD_CLOCK_PLL_HSE ? "PLL_HSE" : "FALLBACK", count);
    if (count > 0u) {
        con_printf("(%" PRIu32 " ns)", (uint32_t)((uint64_t)last * 1000000000u / SystemCoreClock));
    }
    con_printf(" LSE=");
    if (lse_offset_ppb(&st, &ppb)) {
        print_ppm(ppb);
    } else {
        con_printf("%s", st.ready ? "ready" : (st.started ? "starting" : "NG"));
    }
    con_printf(" OE=High-Z\r\n> ");
}
#endif

void console_task(void)
{
    static bool last_was_cr;
    uint32_t seconds;

    if (banner_pending) {
        banner_pending = false;
        print_banner();
    }
#ifdef FW_APP
    if (timesync_second_elapsed(&seconds)) {
        log_second(seconds);
    }
#else
    if (board_pps_second_elapsed(&seconds)) {
        log_second(seconds);
    }
#endif

    while (tud_cdc_available() > 0u) {
        const int32_t c = tud_cdc_read_char();
        if (c < 0) {
            break;
        }
        if (c == '\n' && last_was_cr) {
            last_was_cr = false; /* CRLFのLFは無視する */
            continue;
        }
        last_was_cr = c == '\r';
        if (c == '\r' || c == '\n') {
            con_printf("\r\n");
            line[line_len] = '\0';
            execute(line);
            line_len = 0;
        } else if (c == 0x08 || c == 0x7F) {
            if (line_len > 0u) {
                line_len--;
                con_printf("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7F && line_len + 1u < LINE_MAX) {
            line[line_len++] = (char)c;
            const char echo = (char)c;
            console_write(&echo, 1u);
        }
    }
}
