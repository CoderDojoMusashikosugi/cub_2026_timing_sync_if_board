/*
 * 外部コネクタの折返し試験（test_plan UA-03、IO-03、PP-03/PP-04の一部）
 *
 * loop: UART TXの出力をコネクタ上で受信入力へつないで使う。
 *   j3/j7/j8: 同じコネクタのpin 3（TX）とpin 4（RX）を短絡する
 *   j3j2:     J3 pin 3（Jetson TX）をJ2 pin 3（GNSS NMEA入力）へつなぐ
 * 経路は MCU TX → 74LVC125A（OE_Nで個別enable、非反転）→ 33 Ohm → コネクタ → 4.7 kOhm → SN74LV14A（反転）→ MCU RX
 * で、受信側はRXINV=1で反転を戻す。次を確かめる。
 *   1. OE_N=High（disable）ではTXを動かしてもRXが変化しない（bufferがHigh-Zで、RXは100 kOhm pull-upのidle）
 *   2. OE_N=Lowで、TX High→RX pin Low、TX Low→RX pin High（buffer非反転、U4反転）
 *   3. OE_N=HighではUARTで送っても1 byteも受信しない
 *   4. OE_N=Lowで、各baudの送受信が全byte一致し、FE/NE/ORE/欠落がない
 * 試験中だけ対象channelのOE_Nを下げ、終わったらOE_NをHighに戻してからpinをanalogへ戻す。
 *
 * ppsloop: J3 pin 2（生成PPS）をJ2 pin 2（GNSS PPS入力）へつないで使う。
 *   TIM2 CH2（PC5）→ U7A（PPS_OE_N）→ J3 pin 2 → J2 pin 2 → 4.7 kOhm → U4A（反転）→ PA5 TIM2 CH1
 * PPS_OE_N=HighでcaptureがないこととPA5のレベル、Lowで毎秒1回captureし、生成PPSの立上り（TIM2 update）から
 * captureまでの遅れが一定であること、両edgeのcaptureでHigh幅が100 msであることを確かめる。
 * PPS_OE_NはJ3～J8のpin 2の6系統を同時にenableするので、J4～J8には何もつながないこと。
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "console.h"
#include "selftest.h"
#include "stm32g0xx.h"
#include "tusb.h"

typedef struct {
    USART_TypeDef *uart;
    bool apb2;       /* clock enableがRCC_APBENR2（USART1）か */
    uint32_t en_bit; /* RCC_APBENRxのclock enable bit */
    bool lpuart;
    GPIO_TypeDef *port;
    uint32_t pin;
    uint32_t af;
} uart_io_t;

typedef struct {
    const char *name;
    const char *label;
    const char *wiring;
    uart_io_t tx;
    uart_io_t rx;
    uint32_t oe_pin;  /* GPIOB */
    bool pps_to_j2;   /* J3 pin 2→J2 pin 2もつないでいる（loop j3j2 ppsを使える） */
} loop_ch_t;

#define IO_USART1(pin) {USART1, true, RCC_APBENR2_USART1EN, false, GPIOA, (pin), 1u}
#define IO_USART2(pin) {USART2, false, RCC_APBENR1_USART2EN, false, GPIOA, (pin), 1u}
#define IO_USART3_PB11 {USART3, false, RCC_APBENR1_USART3EN, false, GPIOB, 11u, 4u}
#define IO_LPUART2(pin) {LPUART2, false, RCC_APBENR1_LPUART2EN, true, GPIOC, (pin), 3u}

/* pinとAFの正本はhardware/overall/pin_assignment.md「MCU割当」「NMEAロジック接続」 */
static const loop_ch_t channels[] = {
    {"j3", "J3 Jetson（USART2 PA2→PA3、JETSON_TX_OE_N PB12）", "J3 pin 3―pin 4短絡", IO_USART2(2u), IO_USART2(3u),
     12u, false},
    {"j7", "J7 AUX1（USART1 PA9→PA10、AUX1_TX_OE_N PB8）", "J7 pin 3―pin 4短絡", IO_USART1(9u), IO_USART1(10u), 8u,
     false},
    {"j8", "J8 AUX2（LPUART2 PC6→PC7、AUX2_TX_OE_N PB9）", "J8 pin 3―pin 4短絡", IO_LPUART2(6u), IO_LPUART2(7u), 9u,
     false},
    {"j3j2", "J3 TX→J2 NMEA（USART2 PA2→USART3 PB11、JETSON_TX_OE_N PB12）", "J3 pin 3→J2 pin 3", IO_USART2(2u),
     IO_USART3_PB11, 12u, true},
};

static const uint32_t sweep_bauds[] = {9600u, 115200u, 460800u, 921600u, 2000000u, 3000000u};

#define GPIO_MODE_INPUT 0u
#define GPIO_MODE_OUTPUT 1u
#define GPIO_MODE_AF 2u
#define GPIO_MODE_ANALOG 3u

#define WINDOW 8u          /* 送信済みで未受信のbyteの上限。RX FIFO（8段）を溢れさせない */
#define BATCH 256u         /* このbyte数ごとにpipelineを空にしてUSBを処理する */
#define SEED 0x2545F491u
#define ERR_FLAGS (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)
#define ERR_CLEAR (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF)

#define PPS_OE_N_PIN 1u        /* PB1: J3～J8 pin 2の6系統を同時にenableする */
#define GNSS_PPS_MON_PIN 5u    /* PA5 = TIM2_CH1 */
#define PPS_MARGIN 48000u      /* 1 ms（48 MHz count） */
#define PPS_DELAY_MAX 48u      /* 立上り→captureの遅れの上限（1 us）。この試験での妥当性確認用 */
#define PPS_SPREAD_MAX 2u      /* captureの遅れのばらつきの上限（count） */

/* 生成PPSが [lo, hi) のcountにある間まで待つ。PPSのHigh期間はCNT < CCR2 */
static void wait_tim2_window(uint32_t lo, uint32_t hi)
{
    const uint32_t start = board_millis();
    for (;;) {
        const uint32_t cnt = TIM2->CNT;
        if (cnt >= lo && cnt < hi) {
            return;
        }
        if ((uint32_t)(board_millis() - start) > 1500u) {
            return; /* TIM2が止まっている。続く判定でNGになる */
        }
    }
}

static void wait_pps_low(void)
{
    wait_tim2_window(TIM2->CCR2 + PPS_MARGIN, TIM2->ARR - PPS_MARGIN);
}

static void wait_pps_high(void)
{
    wait_tim2_window(PPS_MARGIN, TIM2->CCR2 - PPS_MARGIN);
}

/*
 * UART試験中のGNSS PPS入力（PA5 TIM2 CH1、立下りcapture）の監視。
 * PPS_OE_N=LowでJ3 pin 2→J2 pin 2の生成PPSが入っているときは、立上りからPPS_DELAY_MAX以内のcaptureだけを正規とし、
 * それ以外（隣の線からの誘導など）を誤captureとして数える。CC1OF（読む前に2回目が来た）も誤captureに数える。
 */
typedef struct {
    bool expect_pps;
    uint32_t updates; /* 生成PPSの立上り（TIM2 update） */
    uint32_t valid;
    uint32_t glitch;
    uint32_t last_glitch_ccr;
} pps_mon_t;

static pps_mon_t pps_mon;
static bool loop_with_pps; /* loop j3j2 pps: UART試験中もPPS_OE_N=Low */

static void pps_mon_start(void)
{
    memset(&pps_mon, 0, sizeof(pps_mon));
    pps_mon.expect_pps = loop_with_pps;
    (void)TIM2->CCR1;
    TIM2->SR = ~(TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC1OF);
}

static void pps_mon_poll(void)
{
    const uint32_t sr = TIM2->SR;
    if (sr & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;
        pps_mon.updates++;
    }
    if (sr & TIM_SR_CC1IF) {
        const uint32_t ccr = TIM2->CCR1; /* 読むとCC1IFがclearされる */
        if (sr & TIM_SR_CC1OF) {
            TIM2->SR = ~TIM_SR_CC1OF;
            pps_mon.glitch++;
        }
        if (pps_mon.expect_pps && ccr <= PPS_DELAY_MAX) {
            pps_mon.valid++;
        } else {
            pps_mon.glitch++;
            pps_mon.last_glitch_ccr = ccr;
        }
    }
}

/* 監視結果を1行出し、PPSを入れているときの合否を返す（入れていないときの誤captureは参考扱いでtrue） */
static bool pps_mon_report(void)
{
    pps_mon_poll();
    if (!pps_mon.expect_pps) {
        if (pps_mon.glitch > 0u) {
            con_printf("    参考: GNSS PPS入力の誤capture %" PRIu32 "回（最後のCCR1 %" PRIu32
                       "）。PPS_OE_N=HighでJ2 pin 2は100 kOhm pull-downだけ\r\n",
                       pps_mon.glitch, pps_mon.last_glitch_ccr);
        }
        return true;
    }
    const bool ok = pps_mon.glitch == 0u && pps_mon.valid + 1u >= pps_mon.updates && pps_mon.valid <= pps_mon.updates + 1u;
    con_printf("    %s GNSS PPS入力: 生成PPS %" PRIu32 "回、正規capture %" PRIu32 "回、誤capture %" PRIu32 "回", ok ? "OK" : "NG",
               pps_mon.updates, pps_mon.valid, pps_mon.glitch);
    if (pps_mon.glitch > 0u) {
        con_printf("（最後のCCR1 %" PRIu32 "）", pps_mon.last_glitch_ccr);
    }
    con_printf("\r\n");
    return ok;
}

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

static void pin_write(GPIO_TypeDef *port, uint32_t pin, bool high)
{
    if (high) {
        port->BSRR = 1u << pin;
    } else {
        port->BRR = 1u << pin;
    }
}

static uint32_t pin_read(GPIO_TypeDef *port, uint32_t pin)
{
    return (port->IDR >> pin) & 1u;
}

static void oe_enable(const loop_ch_t *ch, bool enable)
{
    pin_write(GPIOB, ch->oe_pin, !enable); /* active-low */
}

static void uart_clock(const uart_io_t *io, bool on)
{
    volatile uint32_t *enr = io->apb2 ? &RCC->APBENR2 : &RCC->APBENR1;
    if (on) {
        *enr |= io->en_bit;
    } else {
        *enr &= ~io->en_bit;
    }
    (void)*enr;
}

/* TXをGPIO出力のHigh（UART idleと同じ）、RXをGPIO入力にする。UARTの設定を変える間もTX線を動かさない */
static void pins_gpio(const loop_ch_t *ch)
{
    pin_write(ch->tx.port, ch->tx.pin, true);
    ch->tx.port->OTYPER &= ~(1u << ch->tx.pin);
    pin_mode(ch->tx.port, ch->tx.pin, GPIO_MODE_OUTPUT);
    pin_mode(ch->rx.port, ch->rx.pin, GPIO_MODE_INPUT);
}

/* UARTをenableした後に呼ぶ。TX idle（High）とGPIOのHighが同じなので切替えでglitchは出ない */
static void pins_af(const loop_ch_t *ch)
{
    pin_af(ch->tx.port, ch->tx.pin, ch->tx.af);
    pin_af(ch->rx.port, ch->rx.pin, ch->rx.af);
    pin_mode(ch->tx.port, ch->tx.pin, GPIO_MODE_AF);
    pin_mode(ch->rx.port, ch->rx.pin, GPIO_MODE_AF);
}

/* 元の状態（OE_N High、TX/RX analog、AF 0、UART停止・clock停止）へ戻す。OE_Nを先にHighにする */
static void restore(const loop_ch_t *ch)
{
    oe_enable(ch, false);
    ch->tx.uart->CR1 = 0;
    ch->rx.uart->CR1 = 0;
    pin_mode(ch->tx.port, ch->tx.pin, GPIO_MODE_ANALOG);
    pin_mode(ch->rx.port, ch->rx.pin, GPIO_MODE_ANALOG);
    pin_af(ch->tx.port, ch->tx.pin, 0u);
    pin_af(ch->rx.port, ch->rx.pin, 0u);
    pin_write(ch->tx.port, ch->tx.pin, false);
    uart_clock(&ch->tx, false);
    uart_clock(&ch->rx, false);
}

/* 8N1、RXINV=1、FIFO有効で1つのUARTを設定する。BRRが範囲外ならfalse */
static bool uart_init_one(const uart_io_t *io, uint32_t baud, uint32_t dir)
{
    USART_TypeDef *u = io->uart;
    uint32_t brr;

    if (io->lpuart) {
        const uint64_t v = (256ull * SystemCoreClock + baud / 2u) / baud;
        if (v < 0x300u || v > 0xFFFFFu) {
            return false;
        }
        brr = (uint32_t)v;
    } else {
        brr = (SystemCoreClock + baud / 2u) / baud;
        if (brr < 16u || brr > 0xFFFFu) {
            return false;
        }
    }
    u->CR1 = 0;
    u->PRESC = 0;
    u->BRR = brr;
    u->CR2 = USART_CR2_RXINV; /* U4で反転しているので受信側で戻す（TXには影響しない） */
    u->CR3 = 0;
    u->ICR = ERR_CLEAR;
    u->CR1 = USART_CR1_FIFOEN | dir | USART_CR1_UE;
    const uint32_t ack = ((dir & USART_CR1_TE) ? USART_ISR_TEACK : 0u) | ((dir & USART_CR1_RE) ? USART_ISR_REACK : 0u);
    const uint32_t start = board_millis();
    while ((u->ISR & ack) != ack) {
        if ((uint32_t)(board_millis() - start) > 5u) {
            return false;
        }
    }
    return true;
}

static bool uart_setup(const loop_ch_t *ch, uint32_t baud)
{
    if (ch->tx.uart == ch->rx.uart) {
        return uart_init_one(&ch->tx, baud, USART_CR1_TE | USART_CR1_RE);
    }
    return uart_init_one(&ch->rx, baud, USART_CR1_RE) && uart_init_one(&ch->tx, baud, USART_CR1_TE);
}

/* RX pinのレベルを20 ms見て、最初の値と変化回数を返す */
static uint32_t rx_level(const loop_ch_t *ch, unsigned *changes)
{
    board_delay_ms(2);
    const uint32_t first = pin_read(ch->rx.port, ch->rx.pin);
    uint32_t level = first;
    *changes = 0;
    for (unsigned n = 0; n < 20u; n++) {
        board_delay_ms(1);
        pps_mon_poll();
        const uint32_t now = pin_read(ch->rx.port, ch->rx.pin);
        *changes += now != level ? 1u : 0u;
        level = now;
    }
    return first;
}

static bool static_check(const loop_ch_t *ch, bool oe, bool tx_high, uint32_t expected, const char *why)
{
    unsigned changes;

    pps_mon_start();
    pin_write(ch->tx.port, ch->tx.pin, tx_high);
    const uint32_t rx = rx_level(ch, &changes);
    const bool ok = rx == expected && changes == 0u;
    con_printf("%s  静的  OE_N=%s  TX %s → RX pin %s 変化%u回（想定%s: %s）\r\n", ok ? "OK" : "NG",
               oe ? "Low (enable) " : "High(disable)", tx_high ? "High" : "Low ", rx ? "High" : "Low ", changes,
               expected ? "High" : "Low", why);
    /* 静的確認は約20 msしか見ないので、正規captureの回数は判定せず誤captureだけを示す */
    pps_mon_poll();
    if (pps_mon.glitch > 0u) {
        con_printf("    参考: GNSS PPS入力の誤capture %" PRIu32 "回（最後のCCR1 %" PRIu32 "）\r\n", pps_mon.glitch,
                   pps_mon.last_glitch_ccr);
    }
    return ok;
}

/* 先頭256 byteは0x00～0xFFの全値、その後はxorshift32の乱数 */
static uint8_t pattern(uint32_t index, uint32_t *state)
{
    if (index < 256u) {
        return (uint8_t)index;
    }
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (uint8_t)(x >> 24);
}

typedef struct {
    uint32_t sent;
    uint32_t received;
    uint32_t mismatches;
    uint32_t pe;
    uint32_t fe;
    uint32_t ne;
    uint32_t ore;
    bool stalled; /* 受信が止まったので打ち切った */
    uint32_t first_bad;
    uint8_t first_got;
    uint8_t first_want;
    uint32_t elapsed_ms;
} xfer_t;

static void count_errors(USART_TypeDef *u, uint32_t isr, xfer_t *r)
{
    if (isr & ERR_FLAGS) {
        r->pe += (isr & USART_ISR_PE) ? 1u : 0u;
        r->fe += (isr & USART_ISR_FE) ? 1u : 0u;
        r->ne += (isr & USART_ISR_NE) ? 1u : 0u;
        r->ore += (isr & USART_ISR_ORE) ? 1u : 0u;
        u->ICR = ERR_CLEAR;
    }
}

/*
 * count byteを送りながら受信して照合する。未受信がWINDOWを超えないように送るので、割込みで少し待たされても
 * RX FIFOは溢れない。BATCHごとにpipelineが空になったところでUSBを処理する。
 */
static void xfer(const loop_ch_t *ch, uint32_t baud, uint32_t count, xfer_t *r)
{
    USART_TypeDef *tu = ch->tx.uart;
    USART_TypeDef *ru = ch->rx.uart;
    uint32_t tx_state = SEED;
    uint32_t rx_state = SEED;
    const uint32_t stall_ms = 64u * 10u * 1000u / baud + 5u; /* 64 byte分受信が進まなければ打ち切る */

    memset(r, 0, sizeof(*r));
    const uint32_t start = board_millis();
    for (uint32_t batch_end = 0; batch_end < count && !r->stalled;) {
        batch_end = batch_end + BATCH < count ? batch_end + BATCH : count;
        uint32_t last_rx = board_millis();
        while (r->received < batch_end) {
            pps_mon_poll();
            const uint32_t isr = ru->ISR;
            count_errors(ru, isr, r);
            if (isr & USART_ISR_RXNE_RXFNE) {
                const uint8_t got = (uint8_t)ru->RDR;
                const uint8_t want = pattern(r->received, &rx_state);
                if (got != want) {
                    if (r->mismatches == 0u) {
                        r->first_bad = r->received;
                        r->first_got = got;
                        r->first_want = want;
                    }
                    r->mismatches++;
                }
                r->received++;
                last_rx = board_millis();
            } else if (r->sent < batch_end && r->sent - r->received < WINDOW && (tu->ISR & USART_ISR_TXE_TXFNF)) {
                tu->TDR = pattern(r->sent, &tx_state);
                r->sent++;
            } else if ((uint32_t)(board_millis() - last_rx) > stall_ms) {
                r->stalled = true;
                break;
            }
        }
        tud_task();
    }
    count_errors(ru, ru->ISR, r);
    r->elapsed_ms = board_millis() - start;
}

static bool xfer_check(const loop_ch_t *ch, uint32_t baud, uint32_t count)
{
    xfer_t r;

    if (!uart_setup(ch, baud)) {
        con_printf("--  UART %7" PRIu32 " baud  このUARTでは設定できない（skip）\r\n", baud);
        pins_gpio(ch);
        return true;
    }
    pins_af(ch);
    pps_mon_start();
    xfer(ch, baud, count, &r);
    pins_gpio(ch);

    bool ok = r.received == count && r.mismatches == 0u && (r.pe | r.fe | r.ne | r.ore) == 0u;
    const uint32_t expected_ms = (uint32_t)((uint64_t)count * 10u * 1000u / baud);
    con_printf("%s  UART %7" PRIu32 " baud  %7" PRIu32 " byte  受信%" PRIu32 " 不一致%" PRIu32 " 未受信%" PRIu32
               " FE%" PRIu32 " NE%" PRIu32 " ORE%" PRIu32 " PE%" PRIu32 "  %" PRIu32 " ms（線上の最短%" PRIu32 " ms）\r\n",
               ok ? "OK" : "NG", baud, count, r.received, r.mismatches, count - r.received, r.fe, r.ne, r.ore, r.pe,
               r.elapsed_ms, expected_ms);
    if (r.stalled) {
        con_printf("    受信が止まったため%" PRIu32 " byte目で打ち切った（送信%" PRIu32 " byte）\r\n", r.received, r.sent);
    }
    if (r.mismatches > 0u) {
        con_printf("    最初の不一致: %" PRIu32 " byte目 受信0x%02X 想定0x%02X\r\n", r.first_bad, r.first_got,
                   r.first_want);
    }
    ok = pps_mon_report() && ok;
    return ok;
}

/* OE_N=HighのままUARTで送り、1 byteも折り返らないことを見る */
static bool isolation_check(const loop_ch_t *ch, uint32_t baud, uint32_t count)
{
    USART_TypeDef *tu = ch->tx.uart;
    USART_TypeDef *ru = ch->rx.uart;
    uint32_t state = SEED;
    xfer_t r;

    memset(&r, 0, sizeof(r));
    if (!uart_setup(ch, baud)) {
        con_printf("NG  UART %" PRIu32 " baudを設定できない\r\n", baud);
        pins_gpio(ch);
        return false;
    }
    pins_af(ch);
    pps_mon_start();
    const uint32_t wait_ms = (uint32_t)((uint64_t)count * 10u * 1000u / baud) + 20u;
    const uint32_t start = board_millis();
    while ((uint32_t)(board_millis() - start) < wait_ms) {
        pps_mon_poll();
        const uint32_t isr = ru->ISR;
        count_errors(ru, isr, &r);
        if (isr & USART_ISR_RXNE_RXFNE) {
            (void)ru->RDR;
            r.received++;
        } else if (r.sent < count && (tu->ISR & USART_ISR_TXE_TXFNF)) {
            tu->TDR = pattern(r.sent, &state);
            r.sent++;
        }
    }
    const bool sent_all = r.sent == count && (tu->ISR & USART_ISR_TC) != 0u;
    pins_gpio(ch);

    bool ok = sent_all && r.received == 0u && (r.pe | r.fe | r.ne | r.ore) == 0u;
    con_printf("%s  UART %7" PRIu32 " baud  OE_N=High(disable)で%" PRIu32 " byte送信%s → 受信%" PRIu32
               " byte FE%" PRIu32 " NE%" PRIu32 " ORE%" PRIu32 "（想定: 受信0）\r\n",
               ok ? "OK" : "NG", baud, r.sent, sent_all ? "" : "（送信未完了）", r.received, r.fe, r.ne, r.ore);
    ok = pps_mon_report() && ok;
    return ok;
}

static uint32_t sweep_count(uint32_t baud)
{
    const uint32_t n = baud / 20u; /* 約0.5秒分 */
    return n < 512u ? 512u : n;
}

void selftest_loop(const char *arg)
{
    char name[8] = {0};
    const loop_ch_t *ch = NULL;
    unsigned checks = 0;
    unsigned ng = 0;

    size_t len = strcspn(arg, " ");
    if (len < sizeof(name)) {
        memcpy(name, arg, len);
    }
    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
        if (strcmp(name, channels[i].name) == 0) {
            ch = &channels[i];
        }
    }
    if (ch == NULL) {
        con_printf("usage: loop <j3|j7|j8|j3j2> [pps] [baud bytes]\r\n"
                   "  j3/j7/j8はpin 3とpin 4を短絡、j3j2はJ3 pin 3をJ2 pin 3へつないでから実行する。\r\n"
                   "  baud/bytesを省くと、OE_N disable時の分離、静的レベルと極性、9600～3000000 baudの送受信を一通り行う。\r\n"
                   "  ppsはj3j2でJ3 pin 2→J2 pin 2もつないだとき、UART試験中も生成PPSを入れてGNSS PPS captureを監視する\r\n");
        return;
    }

    /* ppsを付けると、UART試験中もPPS_OE_N=Lowにして生成PPSをJ2 pin 2へ入れる */
    const char *rest = arg + len;
    while (*rest == ' ') {
        rest++;
    }
    loop_with_pps = strncmp(rest, "pps", 3) == 0 && (rest[3] == ' ' || rest[3] == '\0');
    if (loop_with_pps) {
        rest += 3;
        if (!ch->pps_to_j2) {
            con_printf("ppsはj3j2だけで使える\r\n");
            loop_with_pps = false;
            return;
        }
    }

    /* 単一baudの長時間試験（例: loop j3 115200 1000000） */
    char *end;
    const uint32_t baud = (uint32_t)strtoul(rest, &end, 10);
    const uint32_t count = (uint32_t)strtoul(end, &end, 10);
    const bool single = baud > 0u;
    if (single && count == 0u) {
        con_printf("usage: loop %s [pps] <baud> <bytes>\r\n", ch->name);
        loop_with_pps = false;
        return;
    }

    con_printf("%s: %sを前提とする\r\n", ch->label, ch->wiring);
    con_printf("%s\r\n", loop_with_pps ? "J3 pin 2→J2 pin 2も前提とし、PPS_OE_N=Low（J3～J8 pin 2をenable）で生成PPSを入れる"
                                       : "PPS_OE_NはHigh（High-Z）のまま");
    uart_clock(&ch->tx, true);
    uart_clock(&ch->rx, true);
    oe_enable(ch, false);
    pins_gpio(ch);
    if (loop_with_pps) {
        wait_pps_low(); /* PPSがLowの間に切り替え、短パルスを作らない */
        pin_write(GPIOB, PPS_OE_N_PIN, false);
    }

    if (single) {
        oe_enable(ch, true);
        checks++;
        ng += xfer_check(ch, baud, count) ? 0u : 1u;
    } else {
        /* 1. OE_N disable: TXを動かしてもRXはpull-upのidle（U4反転後Low）のまま */
        checks += 3;
        ng += static_check(ch, false, true, 0u, "High-Zでpull-upのidle") ? 0u : 1u;
        ng += static_check(ch, false, false, 0u, "High-Zでpull-upのidle") ? 0u : 1u;
        pin_write(ch->tx.port, ch->tx.pin, true);
        ng += isolation_check(ch, 115200u, 64u) ? 0u : 1u;

        /* 2. OE_N enable: buffer非反転、U4反転 */
        oe_enable(ch, true);
        checks += 3;
        ng += static_check(ch, true, true, 0u, "buffer非反転・U4反転") ? 0u : 1u;
        ng += static_check(ch, true, false, 1u, "buffer非反転・U4反転") ? 0u : 1u;
        ng += static_check(ch, true, true, 0u, "idleへ戻る") ? 0u : 1u;

        /* 3. 各baudの送受信 */
        for (size_t i = 0; i < sizeof(sweep_bauds) / sizeof(sweep_bauds[0]); i++) {
            checks++;
            ng += xfer_check(ch, sweep_bauds[i], sweep_count(sweep_bauds[i])) ? 0u : 1u;
        }
    }

    if (loop_with_pps) {
        wait_pps_low();
        pin_write(GPIOB, PPS_OE_N_PIN, true);
        loop_with_pps = false;
    }
    restore(ch);
    (void)TIM2->CCR1;
    TIM2->SR = ~(TIM_SR_CC1IF | TIM_SR_CC1OF);
    con_printf("loop %s: %u NG / %u（終了後OE_N/PPS_OE_N=High、TX/RX pinはanalogへ戻した）\r\n", ch->name, ng,
               checks);
}

/* ---- ppsloop ---- */

typedef struct {
    uint32_t updates;      /* TIM2 update（生成PPSの立上り）の回数 */
    uint32_t early;        /* 前半（CNT < CCR2/2）のcapture＝PPS立上り */
    uint32_t late;         /* 後半のcapture＝PPS立下り（両edge captureのときだけ） */
    uint32_t overcaptures; /* CC1OF */
    uint32_t early_min;
    uint32_t early_max;
    uint32_t late_min;
    uint32_t late_max;
    uint32_t last_early;
    bool have_early;
    uint32_t width_min; /* 同じ周期の立下り−立上り */
    uint32_t width_max;
    uint32_t widths;
} pps_watch_t;

static void tim2_capture_edges(bool both)
{
    TIM2->CCER &= ~TIM_CCER_CC1E;
    TIM2->CCER = (TIM2->CCER & ~(TIM_CCER_CC1P | TIM_CCER_CC1NP)) | TIM_CCER_CC1P | (both ? TIM_CCER_CC1NP : 0u);
    TIM2->CCER |= TIM_CCER_CC1E;
    (void)TIM2->CCR1; /* 切替え前のcaptureを捨てる */
    TIM2->SR = ~(TIM_SR_CC1IF | TIM_SR_CC1OF);
}

/* ms間、TIM2のupdateとCH1 captureを数える。PPSのedgeは100 ms以上離れているのでpollingで取りこぼさない */
static void pps_watch(uint32_t ms, pps_watch_t *w)
{
    const uint32_t half = TIM2->CCR2 / 2u;
    uint32_t last_usb = board_millis();

    memset(w, 0, sizeof(*w));
    w->early_min = UINT32_MAX;
    w->late_min = UINT32_MAX;
    w->width_min = UINT32_MAX;
    (void)TIM2->CCR1;
    TIM2->SR = ~(TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC1OF);

    const uint32_t start = board_millis();
    while ((uint32_t)(board_millis() - start) < ms) {
        const uint32_t sr = TIM2->SR;
        if (sr & TIM_SR_UIF) {
            TIM2->SR = ~TIM_SR_UIF;
            w->updates++;
            w->have_early = false;
        }
        if (sr & TIM_SR_CC1IF) {
            const uint32_t ccr = TIM2->CCR1; /* 読むとCC1IFがclearされる */
            if (sr & TIM_SR_CC1OF) {
                TIM2->SR = ~TIM_SR_CC1OF;
                w->overcaptures++;
            }
            if (ccr < half) {
                w->early++;
                w->early_min = ccr < w->early_min ? ccr : w->early_min;
                w->early_max = ccr > w->early_max ? ccr : w->early_max;
                w->last_early = ccr;
                w->have_early = true;
            } else {
                w->late++;
                w->late_min = ccr < w->late_min ? ccr : w->late_min;
                w->late_max = ccr > w->late_max ? ccr : w->late_max;
                if (w->have_early) {
                    const uint32_t width = ccr - w->last_early;
                    w->width_min = width < w->width_min ? width : w->width_min;
                    w->width_max = width > w->width_max ? width : w->width_max;
                    w->widths++;
                }
            }
        }
        if ((uint32_t)(board_millis() - last_usb) >= 10u) {
            tud_task();
            last_usb = board_millis();
        }
    }
}

/* count→ns（48 MHzで1 count＝20.833 ns） */
static uint32_t counts_ns(uint32_t counts)
{
    return (uint32_t)((uint64_t)counts * 1000000000u / SystemCoreClock);
}

static bool pps_level_check(bool oe, bool pps_high, uint32_t expected, const char *why)
{
    if (pps_high) {
        wait_pps_high();
    } else {
        wait_pps_low();
    }
    const uint32_t level = pin_read(GPIOA, GNSS_PPS_MON_PIN);
    const bool ok = level == expected;
    con_printf("%s  静的  PPS_OE_N=%s  生成PPS %s → PA5 %s（想定%s: %s）\r\n", ok ? "OK" : "NG",
               oe ? "Low (enable) " : "High(disable)", pps_high ? "High" : "Low ", level ? "High" : "Low ",
               expected ? "High" : "Low", why);
    return ok;
}

static bool pps_none_check(const char *when, uint32_t ms)
{
    pps_watch_t w;

    pps_watch(ms, &w);
    const bool ok = w.early == 0u && w.late == 0u && w.updates > 0u;
    con_printf("%s  %s: %" PRIu32 " ms間でcapture %" PRIu32 "回（生成PPS %" PRIu32 "回、想定capture 0）\r\n",
               ok ? "OK" : "NG", when, ms, w.early + w.late, w.updates);
    return ok;
}

void selftest_ppsloop(const char *arg)
{
    const uint32_t seconds = arg[0] != '\0' ? (uint32_t)strtoul(arg, NULL, 10) : 10u;
    const uint32_t ccr2 = TIM2->CCR2;
    unsigned checks = 0;
    unsigned ng = 0;
    pps_watch_t w;

    if (seconds < 2u || seconds > 3600u) {
        con_printf("usage: ppsloop [秒数 2～3600、既定10]\r\n"
                   "  J3 pin 2をJ2 pin 2へつないでから実行する。J4～J8には何もつながないこと\r\n");
        return;
    }
    if (!(TIM2->CR1 & TIM_CR1_CEN) || board_boot_info()->clock_mode != BOARD_CLOCK_PLL_HSE) {
        con_printf("NG  TIM2が動いていないかclock診断mode（PLL_HSEで実行する）\r\n");
        return;
    }
    con_printf("J3 PPS→J2 PPS（TIM2 CH2 PC5→U7A→J3 pin 2→J2 pin 2→U4A→PA5 TIM2 CH1）。PPS_OE_N（PB1）はJ3～J8 pin 2を同時に"
               "enableする\r\n");

    /* 1. PPS_OE_N disable: J2 pin 2はR14 pull-downのLow、PA5はU4A反転でHigh、captureなし */
    pin_write(GPIOB, PPS_OE_N_PIN, true);
    tim2_capture_edges(false);
    checks += 3;
    ng += pps_level_check(false, true, 1u, "High-ZでR14 pull-down") ? 0u : 1u;
    ng += pps_level_check(false, false, 1u, "High-ZでR14 pull-down") ? 0u : 1u;
    ng += pps_none_check("PPS_OE_N=High(disable)", 2500u) ? 0u : 1u;

    /* 2. PPS_OE_N enable（PPSがLowの間に切り替え、短パルスを作らない）: U7A非反転、U4A反転 */
    wait_pps_low();
    pin_write(GPIOB, PPS_OE_N_PIN, false);
    checks += 2;
    ng += pps_level_check(true, true, 0u, "buffer非反転・U4A反転") ? 0u : 1u;
    ng += pps_level_check(true, false, 1u, "buffer非反転・U4A反転") ? 0u : 1u;

    /* 3. 立下りcapture（製品と同じ設定）: 毎秒1回、遅れが一定 */
    wait_pps_low();
    pps_watch(seconds * 1000u, &w);
    {
        const uint32_t spread = w.early > 0u ? w.early_max - w.early_min : 0u;
        const bool count_ok = w.early + 1u >= w.updates && w.early <= w.updates + 1u && w.early + 1u >= seconds;
        const bool ok = count_ok && w.late == 0u && w.overcaptures == 0u && w.early > 0u &&
                        w.early_max <= PPS_DELAY_MAX && spread <= PPS_SPREAD_MAX;
        checks++;
        ng += ok ? 0u : 1u;
        con_printf("%s  立下りcapture %" PRIu32 " s: capture %" PRIu32 "回 / 生成PPS %" PRIu32 "回、後半capture %" PRIu32
                   "、CC1OF %" PRIu32 "\r\n",
                   ok ? "OK" : "NG", seconds, w.early, w.updates, w.late, w.overcaptures);
        if (w.early > 0u) {
            con_printf("    生成PPS立上り→capture: %" PRIu32 "～%" PRIu32 " count（%" PRIu32 "～%" PRIu32
                       " ns、ばらつき%" PRIu32 " count）（判定: 遅れ%u count以下、ばらつき%u count以下）\r\n",
                       w.early_min, w.early_max, counts_ns(w.early_min), counts_ns(w.early_max), spread,
                       (unsigned)PPS_DELAY_MAX, (unsigned)PPS_SPREAD_MAX);
        }
    }

    /* 4. 両edge capture: High幅＝CCR2（100 ms） */
    wait_pps_low();
    tim2_capture_edges(true);
    const uint32_t both_s = seconds < 5u ? seconds : 5u;
    pps_watch(both_s * 1000u, &w);
    tim2_capture_edges(false);
    {
        const uint32_t lo = ccr2 - PPS_DELAY_MAX;
        const uint32_t hi = ccr2 + PPS_DELAY_MAX;
        const bool ok = w.widths + 1u >= both_s && w.early == w.late && w.overcaptures == 0u && w.widths > 0u &&
                        w.width_min >= lo && w.width_max <= hi;
        checks++;
        ng += ok ? 0u : 1u;
        con_printf("%s  両edge capture %" PRIu32 " s: 立上り%" PRIu32 "回、立下り%" PRIu32 "回、CC1OF %" PRIu32 "\r\n",
                   ok ? "OK" : "NG", both_s, w.early, w.late, w.overcaptures);
        if (w.widths > 0u) {
            con_printf("    High幅: %" PRIu32 "～%" PRIu32 " count（%" PRIu32 ".%06" PRIu32 "～%" PRIu32 ".%06" PRIu32
                       " ms、CCR2=%" PRIu32 "）、立下り→capture %" PRIu32 "～%" PRIu32 " count（CCR2基準 %+" PRId32 "～%+" PRId32
                       "）\r\n",
                       w.width_min, w.width_max, counts_ns(w.width_min) / 1000000u, counts_ns(w.width_min) % 1000000u,
                       counts_ns(w.width_max) / 1000000u, counts_ns(w.width_max) % 1000000u, ccr2, w.late_min,
                       w.late_max, (int32_t)(w.late_min - ccr2), (int32_t)(w.late_max - ccr2));
        }
    }

    /* 5. PPS_OE_N disableへ戻す（PPSがLowの間に切り替える）。以後captureがない */
    wait_pps_low();
    pin_write(GPIOB, PPS_OE_N_PIN, true);
    checks++;
    ng += pps_none_check("PPS_OE_N=Highへ戻した後", 1500u) ? 0u : 1u;

    (void)TIM2->CCR1;
    TIM2->SR = ~(TIM_SR_CC1IF | TIM_SR_CC1OF);
    con_printf("ppsloop: %u NG / %u（終了後PPS_OE_N=High、TIM2 CH1は立下りcaptureへ戻した）\r\n", ng, checks);
}
