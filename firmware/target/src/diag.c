#include "diag.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "console.h"
#include "stm32g0xx.h"
#include "tusb.h"

/* ---- gpio ---- */

/*
 * bring-up firmwareでのpin状態の想定。文字はpin 0～15の順。
 *   A=analog、F=alternate function、O=output、x=照合しない、-=このpackageにpinがない
 * 正本はhardware/overall/pin_assignment.md「MCU割当」。bring-upではUART等は未使用なのでanalogのまま。
 */
typedef struct {
    const char *name;
    GPIO_TypeDef *port;
    const char *mode;
    uint8_t af[16];       /* Fのpinだけ意味を持つ */
    uint16_t odr_high;    /* Highであるべき出力pin */
    uint16_t pull_up;     /* pull-upであるべきpin（それ以外のA/F/Oはpullなし） */
    uint16_t pull_down;   /* pull-downであるべきpin */
} port_expect_t;

static const port_expect_t expect[] = {
    /* PA5=TIM2_CH1、PA7=LED、PA13/PA14=SWD（reset値のpull-up/pull-down） */
    {"GPIOA", GPIOA, "AAAAAFAOAAAAAFFA", {[5] = 2, [13] = 0, [14] = 0}, 0, 1u << 13, 1u << 14},
    /* PB1/PB2/PB8/PB9/PB12/PB13/PB14=OE_N（High） */
    {"GPIOB", GPIOB, "AOOAAAAAOOAAOOOA", {0}, BOARD_OE_N_PINS, 0, 0},
    /* PC5=TIM2_CH2。PC13はNCのまま（TAMP/RTC_OUTに割り付けない）、PC14/PC15はLSE */
    {"GPIOC", GPIOC, "AAAAAFAAAAAAAAAA", {[5] = 2}, 0, 0, 0},
    {"GPIOD", GPIOD, "AAAAAAA-AA------", {0}, 0, 0, 0},
    /* PF0=HSE bypass入力、PF2=NRST */
    {"GPIOF", GPIOF, "AAx-------------", {0}, 0, 0, 0},
};

static uint32_t mode_code(char c)
{
    switch (c) {
    case 'O':
        return 1u;
    case 'F':
        return 2u;
    default:
        return 3u; /* A */
    }
}

void diag_gpio(const char *arg)
{
    unsigned checked = 0;
    unsigned ng = 0;

    (void)arg;
    RCC->IOPENR |= RCC_IOPENR_GPIODEN | RCC_IOPENR_GPIOFEN;
    (void)RCC->IOPENR;

    for (size_t p = 0; p < sizeof(expect) / sizeof(expect[0]); p++) {
        const port_expect_t *e = &expect[p];
        const uint32_t moder = e->port->MODER;
        const uint32_t pupdr = e->port->PUPDR;
        const uint32_t odr = e->port->ODR;
        const uint32_t otyper = e->port->OTYPER;

        for (uint32_t pin = 0; pin < 16u; pin++) {
            const char m = e->mode[pin];
            if (m == '-' || m == 'x') {
                continue;
            }
            const uint32_t mode = (moder >> (pin * 2u)) & 3u;
            const uint32_t pull = (pupdr >> (pin * 2u)) & 3u;
            const uint32_t want_pull = (e->pull_up & (1u << pin)) ? 1u : (e->pull_down & (1u << pin)) ? 2u : 0u;
            bool ok = mode == mode_code(m) && pull == want_pull;
            if (m == 'F') {
                const uint32_t af = (e->port->AFR[pin / 8u] >> ((pin % 8u) * 4u)) & 0xFu;
                ok = ok && af == e->af[pin];
            }
            if (m == 'O') {
                ok = ok && (otyper & (1u << pin)) == 0u; /* push-pull */
                if (e->odr_high & (1u << pin)) {
                    ok = ok && (odr & (1u << pin)) != 0u;
                }
            }
            checked++;
            if (!ok) {
                ng++;
                con_printf("NG  P%c%-2" PRIu32 " 想定%c  MODER=%" PRIu32 " PUPDR=%" PRIu32 " ODR=%u\r\n", e->name[4], pin, m,
                           mode, pull, (odr >> pin) & 1u ? 1u : 0u);
            }
        }
    }
    con_printf("gpio: %u pin照合、%u NG\r\n", checked, ng);
}

/* ---- inputs ---- */

typedef struct {
    const char *name;
    GPIO_TypeDef *port;
    uint32_t pin;
    uint32_t expected; /* コネクタ未接続時のMCU側のレベル */
    const char *why;
} input_expect_t;

static const input_expect_t inputs[] = {
    {"PA5  GNSS_PPS_MON", GPIOA, 5, 1, "J2 pin2開放→R14 pull-down→U4A反転でHigh"},
    {"PB11 GNSS_NMEA_RX", GPIOB, 11, 0, "J2 pin3開放→R15 pull-up→U4B反転でLow"},
    {"PA3  JETSON_RX", GPIOA, 3, 0, "J3 pin4開放→R39 pull-up→U4C反転でLow"},
    {"PA10 AUX1_RX", GPIOA, 10, 0, "J7 pin4開放→R40 pull-up→U4D反転でLow"},
    {"PC7  AUX2_RX", GPIOC, 7, 0, "J8 pin4開放→R41 pull-up→U4E反転でLow"},
};

void diag_inputs(const char *arg)
{
    unsigned ng = 0;

    (void)arg;
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        const input_expect_t *in = &inputs[i];
        const uint32_t shift = in->pin * 2u;
        const uint32_t saved = in->port->MODER;
        const bool is_af = ((saved >> shift) & 3u) == 2u;

        /* analogのままでは入力bufferが切れているので、読む間だけinputにする（AFはそのまま読める） */
        if (!is_af) {
            in->port->MODER = saved & ~(3u << shift);
        }
        board_delay_ms(2);

        /* 100 msの間に変化しないことも見る */
        const uint32_t first = (in->port->IDR >> in->pin) & 1u;
        unsigned changes = 0;
        uint32_t level = first;
        for (unsigned n = 0; n < 100u; n++) {
            board_delay_ms(1);
            const uint32_t now = (in->port->IDR >> in->pin) & 1u;
            changes += now != level ? 1u : 0u;
            level = now;
        }
        if (!is_af) {
            in->port->MODER = saved;
        }

        const bool ok = first == in->expected && changes == 0u;
        ng += ok ? 0u : 1u;
        con_printf("%s  %-18s %s 変化%u回  （想定%s: %s）\r\n", ok ? "OK" : "NG", in->name, first ? "High" : "Low ",
                   changes, in->expected ? "High" : "Low", in->why);
    }
    con_printf("inputs: %u NG（コネクタに何かつながっている場合は想定と異なってよい）\r\n", ng);
}

/* ---- rxidle ---- */

typedef struct {
    const char *name;
    USART_TypeDef *uart;
    GPIO_TypeDef *port;
    uint32_t pin;
    uint32_t af;
    bool lpuart;
} uart_rx_t;

static const uart_rx_t rx_uarts[] = {
    {"USART3  GNSS NMEA (PB11)", USART3, GPIOB, 11, 4, false},
    {"USART2  Jetson    (PA3)", USART2, GPIOA, 3, 1, false},
    {"USART1  AUX1      (PA10)", USART1, GPIOA, 10, 1, false},
    {"LPUART2 AUX2      (PC7)", LPUART2, GPIOC, 7, 3, true},
};

#define RX_BAUD 115200u
#define RX_WATCH_MS 2000u

void diag_rxidle(const char *arg)
{
    const size_t n = sizeof(rx_uarts) / sizeof(rx_uarts[0]);
    uint32_t saved_moder[sizeof(rx_uarts) / sizeof(rx_uarts[0])];
    uint32_t saved_afr[sizeof(rx_uarts) / sizeof(rx_uarts[0])];
    unsigned ng = 0;

    (void)arg;
    RCC->APBENR1 |= RCC_APBENR1_USART2EN | RCC_APBENR1_USART3EN | RCC_APBENR1_LPUART2EN;
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    (void)RCC->APBENR2;

    for (size_t i = 0; i < n; i++) {
        const uart_rx_t *u = &rx_uarts[i];
        volatile uint32_t *afr = &u->port->AFR[u->pin / 8u];
        const uint32_t ashift = (u->pin % 8u) * 4u;
        const uint32_t mshift = u->pin * 2u;

        saved_moder[i] = u->port->MODER;
        saved_afr[i] = *afr;
        *afr = (*afr & ~(0xFu << ashift)) | (u->af << ashift);
        u->port->MODER = (u->port->MODER & ~(3u << mshift)) | (2u << mshift);

        u->uart->CR1 = 0;
        u->uart->BRR = u->lpuart ? (uint32_t)((256ull * SystemCoreClock + RX_BAUD / 2u) / RX_BAUD)
                                 : (SystemCoreClock + RX_BAUD / 2u) / RX_BAUD;
        u->uart->CR2 = USART_CR2_RXINV; /* U4で反転しているので受信側で戻す */
        u->uart->CR3 = 0;
        u->uart->ICR = USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
        u->uart->CR1 = USART_CR1_UE | USART_CR1_RE;
    }

    /* 未接続のまま見張り、受信byteとerrorを数える */
    uint32_t bytes[sizeof(rx_uarts) / sizeof(rx_uarts[0])] = {0};
    uint32_t errors[sizeof(rx_uarts) / sizeof(rx_uarts[0])] = {0};
    const uint32_t start = board_millis();
    while ((uint32_t)(board_millis() - start) < RX_WATCH_MS) {
        tud_task();
        for (size_t i = 0; i < n; i++) {
            USART_TypeDef *uart = rx_uarts[i].uart;
            const uint32_t isr = uart->ISR;
            if (isr & (USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) {
                errors[i]++;
                uart->ICR = USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
            }
            if (isr & USART_ISR_RXNE_RXFNE) {
                (void)uart->RDR;
                bytes[i]++;
            }
        }
    }

    for (size_t i = 0; i < n; i++) {
        const uart_rx_t *u = &rx_uarts[i];
        u->uart->CR1 = 0;
        u->port->AFR[u->pin / 8u] = saved_afr[i];
        u->port->MODER = saved_moder[i];
        const bool ok = bytes[i] == 0u && errors[i] == 0u;
        ng += ok ? 0u : 1u;
        con_printf("%s  %s  受信%" PRIu32 " byte、error %" PRIu32 "回（RXINV=1、%u ms）\r\n", ok ? "OK" : "NG", u->name,
                   bytes[i], errors[i], (unsigned)RX_WATCH_MS);
    }
    RCC->APBENR1 &= ~(RCC_APBENR1_USART2EN | RCC_APBENR1_USART3EN | RCC_APBENR1_LPUART2EN);
    RCC->APBENR2 &= ~RCC_APBENR2_USART1EN;
    con_printf("rxidle: %u NG（コネクタ未接続が前提）\r\n", ng);
}
