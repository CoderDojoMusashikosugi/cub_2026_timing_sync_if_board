#include "board.h"

#include "stm32g0xx.h"

#define LED_PIN 7u /* PA7 STAT_LED_N: 3V3→R12→D2→PA7のsink構成でLowが点灯 */

#define PLL_M 2u
#define PLL_N 16u
#define PLL_R 4u
#define PLL_Q 4u
#define PLL_SYSCLK_HZ 48000000u
#define HSI16_HZ 16000000u

#define TCXO_SETTLE_MS 5u /* U2/U14の安定待ち（firmware_requirements「起動順序」5） */
#define HSE_TIMEOUT_MS 100u
#define STEP_TIMEOUT_MS 10u

uint32_t SystemCoreClock = HSI16_HZ;

static board_boot_info_t boot_info;
static volatile uint32_t ms_ticks;
static uint32_t pps_count;
static uint32_t pps_last_ccr1;

void SysTick_Handler(void)
{
    ms_ticks++;
}

uint32_t board_millis(void)
{
    return ms_ticks;
}

void board_delay_ms(uint32_t ms)
{
    const uint32_t start = ms_ticks;
    while ((uint32_t)(ms_ticks - start) < ms) {
    }
}

static void gpio_set_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}

static void gpio_set_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    volatile uint32_t *afr = &port->AFR[pin / 8u];
    const uint32_t shift = (pin % 8u) * 4u;
    *afr = (*afr & ~(0xFu << shift)) | (af << shift);
    gpio_set_mode(port, pin, 2u);
}

void board_early_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;
    (void)RCC->IOPENR;

    /* 起動順序1: 全OE_Nは出力dataをHighにしてからoutput modeへ切り替え、Low glitchを出さない */
    GPIOB->BSRR = BOARD_OE_N_PINS;
    GPIOB->OTYPER &= ~BOARD_OE_N_PINS;
    for (uint32_t pin = 0; pin < 16u; pin++) {
        if (BOARD_OE_N_PINS & (1u << pin)) {
            gpio_set_mode(GPIOB, pin, 1u);
        }
    }

    /* LEDは消灯してから出力にする。PA6 TP_FW_TRACEはreset値（analog/no-pull）のまま触らない */
    GPIOA->BSRR = 1u << LED_PIN;
    gpio_set_mode(GPIOA, LED_PIN, 1u);

    /* 起動順序2: reset flagをclearする前に保存する（ES0548のBORRSTFはRCC_CSR.PWRRSTF）。
       bring-up firmwareはRTC検証を行わないため、保存した直後にclearする */
    boot_info.rcc_csr_at_boot = RCC->CSR;
    boot_info.flash_empty_at_boot = (FLASH->ACR & FLASH_ACR_PROGEMPTY) != 0u;
    if (boot_info.flash_empty_at_boot) {
        /* firmwareが動いている以上flashは書込み済み。残っているとSW1等のresetでbootloaderへ入る */
        FLASH->ACR &= ~FLASH_ACR_PROGEMPTY;
    }
    const uint32_t marker = g_fault_marker;
    boot_info.fault_marker = (marker == BOARD_FAULT_MARKER_CSS || marker == BOARD_FAULT_MARKER_HARDFAULT ||
                              marker == BOARD_FAULT_MARKER_BOOTLOADER_GO || marker == BOARD_FAULT_MARKER_NMI)
                                 ? marker
                                 : BOARD_FAULT_MARKER_NONE;
    g_fault_marker = BOARD_FAULT_MARKER_NONE;
    RCC->CSR |= RCC_CSR_RMVF;
}

static bool wait_ms(volatile uint32_t *reg, uint32_t mask, uint32_t expected, uint32_t timeout_ms)
{
    const uint32_t start = ms_ticks;
    while ((*reg & mask) != expected) {
        if ((uint32_t)(ms_ticks - start) > timeout_ms) {
            return false;
        }
    }
    return true;
}

static board_clock_status_t clock_init_pll(void)
{
    /* Voltage Range 1でVOSF=0を確認する */
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS_Msk) | PWR_CR1_VOS_0;
    if (!wait_ms(&PWR->SR2, PWR_SR2_VOSF, 0u, STEP_TIMEOUT_MS)) {
        return BOARD_CLK_ERR_VOSF;
    }

    /* SYSCLKを上げる前にFlashを2 wait stateへ */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | (2u << FLASH_ACR_LATENCY_Pos);
    if (!wait_ms(&FLASH->ACR, FLASH_ACR_LATENCY_Msk, 2u << FLASH_ACR_LATENCY_Pos, STEP_TIMEOUT_MS)) {
        return BOARD_CLK_ERR_LATENCY;
    }

    /* HSE bypass（U14出力をPF0へ入力） */
    RCC->CR |= RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    const uint32_t hse_start = ms_ticks;
    if (!wait_ms(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, HSE_TIMEOUT_MS)) {
        RCC->CR &= ~RCC_CR_HSEON;
        RCC->CR &= ~RCC_CR_HSEBYP;
        return BOARD_CLK_ERR_HSE;
    }
    boot_info.hse_ready_ms = ms_ticks - hse_start;

    /* PLL: 24 MHz / M2 = 12 MHz、×N16 = VCO 192 MHz、/R4 = SYSCLK 48 MHz、/Q4 = USB 48 MHz */
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSE | ((PLL_M - 1u) << RCC_PLLCFGR_PLLM_Pos) |
                   (PLL_N << RCC_PLLCFGR_PLLN_Pos) | (1u << RCC_PLLCFGR_PLLP_Pos) |
                   ((PLL_R - 1u) << RCC_PLLCFGR_PLLR_Pos) | RCC_PLLCFGR_PLLREN |
                   ((PLL_Q - 1u) << RCC_PLLCFGR_PLLQ_Pos) | RCC_PLLCFGR_PLLQEN;
    RCC->CR |= RCC_CR_PLLON;
    if (!wait_ms(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY, STEP_TIMEOUT_MS)) {
        RCC->CR &= ~RCC_CR_PLLON;
        return BOARD_CLK_ERR_PLL;
    }

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_1;
    if (!wait_ms(&RCC->CFGR, RCC_CFGR_SWS_Msk, RCC_CFGR_SWS_1, STEP_TIMEOUT_MS)) {
        RCC->CFGR &= ~RCC_CFGR_SW_Msk;
        RCC->CR &= ~RCC_CR_PLLON;
        return BOARD_CLK_ERR_SWITCH;
    }
    SystemCoreClock = PLL_SYSCLK_HZ;
    SysTick_Config(SystemCoreClock / 1000u);

    RCC->CR |= RCC_CR_CSSON;
    RCC->CCIPR2 = (RCC->CCIPR2 & ~RCC_CCIPR2_USBSEL_Msk) | RCC_CCIPR2_USBSEL_1; /* USB=PLLQ */
    return BOARD_CLK_OK;
}

/*
 * HSE異常時の診断用。SYSCLKはHSI16のまま、USBだけHSI48+CRS（USB SOF同期）で動かして
 * CDCから状態を読めるようにする。時刻出力はHigh-Zのままで、正常時にはこの経路を使わない。
 */
static void clock_init_fallback(void)
{
    RCC->CR |= RCC_CR_HSI48ON;
    (void)wait_ms(&RCC->CR, RCC_CR_HSI48RDY, RCC_CR_HSI48RDY, STEP_TIMEOUT_MS);
    RCC->APBENR1 |= RCC_APBENR1_CRSEN;
    (void)RCC->APBENR1;
    CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN; /* CRS_CFGRのreset値でSYNCSRC=USB SOF */
    RCC->CCIPR2 &= ~RCC_CCIPR2_USBSEL_Msk;     /* USB=HSI48 */
    SystemCoreClock = HSI16_HZ;
}

/* TIM2: CH2（PC5）に1 Hz・High 100 msのPPS、CH1（PA5）にGNSS PPSの立下りcapture。
   PPS_OE_NはHighのままなので外部コネクタには出ず、TP6でだけ観測できる */
static void tim2_init(void)
{
    const uint32_t tim_clk = SystemCoreClock; /* APB prescaler=1なのでTIM2 clock=SYSCLK */

    RCC->APBENR1 |= RCC_APBENR1_TIM2EN;
    (void)RCC->APBENR1;
    gpio_set_af(GPIOC, 5u, 2u); /* TIM2_CH2 = PPS_GENERATED */
    gpio_set_af(GPIOA, 5u, 2u); /* TIM2_CH1 = GNSS_PPS_MON */

    TIM2->CR1 = 0;
    TIM2->PSC = 0;
    TIM2->ARR = tim_clk - 1u;   /* 48 MHzで47999999 */
    TIM2->CCR2 = tim_clk / 10u; /* High幅100 ms（48 MHzで4800000） */
    TIM2->CCMR1 = (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE | TIM_CCMR1_CC1S_0;
    TIM2->CCER = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC2E; /* CH1は立下り（U4Aで反転） */
    TIM2->CR1 = TIM_CR1_ARPE;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->CR1 |= TIM_CR1_CEN;
}

void board_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_PWREN | RCC_APBENR1_DBGEN;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;

    /* 起動順序4: PVM_VDDIO2=110（VDDIO2 domainのI/OとUSBを有効化）、UCPD dead-battery strobe。
       VREFBUFはreset値（無効）のまま触らない */
    PWR->CR2 = (PWR->CR2 & ~PWR_CR2_PVM_VDDIO2_Msk) | (6u << PWR_CR2_PVM_VDDIO2_Pos);
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD1_STROBE | SYSCFG_CFGR1_UCPD2_STROBE;
    boot_info.ucpd_strobed = true;

    SystemCoreClock = HSI16_HZ;
    SysTick_Config(SystemCoreClock / 1000u);

    /* 起動順序5: 電源投入後5 ms以上待ってからclockを切り替える */
    board_delay_ms(TCXO_SETTLE_MS);

    if (boot_info.fault_marker == BOARD_FAULT_MARKER_CSS) {
        /* 前回HSE CSSでresetした。再度HSEを使うとreset loopになり得るので診断modeで起動する */
        boot_info.clock_status = BOARD_CLK_ERR_CSS_RESET;
    } else {
        boot_info.clock_status = clock_init_pll();
    }
    if (boot_info.clock_status == BOARD_CLK_OK) {
        boot_info.clock_mode = BOARD_CLOCK_PLL_HSE;
    } else {
        boot_info.clock_mode = BOARD_CLOCK_FALLBACK_HSI;
        clock_init_fallback();
    }

    tim2_init();
    RCC->APBENR1 |= RCC_APBENR1_USBEN;
    (void)RCC->APBENR1;
}

const board_boot_info_t *board_boot_info(void)
{
    return &boot_info;
}

void board_led(bool on)
{
    if (on) {
        GPIOA->BRR = 1u << LED_PIN;
    } else {
        GPIOA->BSRR = 1u << LED_PIN;
    }
}

#define LSE_TIMEOUT_MS 5000u

static board_lse_status_t lse;
static uint32_t lse_start_ms;
static volatile uint32_t lse_isr_captures;
static volatile uint64_t lse_isr_ticks;
static volatile uint32_t lse_isr_overcaptures;
static uint16_t lse_last_ccr;
static bool lse_have_last;

void TIM16_FDCAN_IT0_IRQHandler(void)
{
    const uint32_t sr = TIM16->SR;

    if (sr & TIM_SR_CC1IF) {
        const uint16_t ccr = (uint16_t)TIM16->CCR1; /* 読むとCC1IFがclearされる */
        if (sr & TIM_SR_CC1OF) {
            /* 前の区間を取りこぼしたので、この区間は捨てて数え直す */
            TIM16->SR = ~TIM_SR_CC1OF;
            lse_isr_overcaptures++;
        } else if (lse_have_last) {
            lse_isr_ticks += (uint16_t)(ccr - lse_last_ccr);
            lse_isr_captures++;
        }
        lse_last_ccr = ccr;
        lse_have_last = true;
    }
}

void board_lse_start(void)
{
    PWR->CR1 |= PWR_CR1_DBP;
    (void)PWR->CR1;
    RCC->BDCR = (RCC->BDCR & ~(RCC_BDCR_LSEDRV_Msk | RCC_BDCR_LSECSSON | RCC_BDCR_LSEBYP)) | RCC_BDCR_LSEDRV_1;
    RCC->BDCR |= RCC_BDCR_LSEON;
    lse_start_ms = ms_ticks;
    lse.started = true;
    lse.was_running = (RCC->BDCR & RCC_BDCR_LSERDY) != 0u;
}

static void lse_measure_start(void)
{
    RCC->APBENR2 |= RCC_APBENR2_TIM16EN;
    (void)RCC->APBENR2;
    TIM16->CR1 = 0;
    TIM16->PSC = 0;
    TIM16->ARR = 0xFFFFu;
    TIM16->TISEL = TIM_TISEL_TI1SEL_1;                                  /* TI1=LSE */
    TIM16->CCMR1 = TIM_CCMR1_CC1S_0 | (3u << TIM_CCMR1_IC1PSC_Pos);    /* TI1、8周期ごとにcapture */
    TIM16->CCER = TIM_CCER_CC1E;
    TIM16->SR = 0;
    TIM16->DIER = TIM_DIER_CC1IE;
    NVIC_SetPriority(TIM16_FDCAN_IT0_IRQn, 0);
    NVIC_EnableIRQ(TIM16_FDCAN_IT0_IRQn);
    TIM16->CR1 = TIM_CR1_CEN;
}

void board_lse_task(void)
{
    if (!lse.started || lse.ready) {
        return;
    }
    if (RCC->BDCR & RCC_BDCR_LSERDY) {
        lse.ready = true;
        lse.ready_ms = ms_ticks - lse_start_ms;
        if (boot_info.clock_mode == BOARD_CLOCK_PLL_HSE) {
            lse_measure_start();
        }
    } else if ((uint32_t)(ms_ticks - lse_start_ms) > LSE_TIMEOUT_MS) {
        lse.ready_ms = UINT32_MAX; /* 起動しなかった（RTC_INVALID相当） */
        lse.started = false;
    }
}

void board_lse_status(board_lse_status_t *out)
{
    __disable_irq();
    lse.captures = lse_isr_captures;
    lse.ticks = lse_isr_ticks;
    lse.overcaptures = lse_isr_overcaptures;
    __enable_irq();
    lse.bdcr = RCC->BDCR;
    *out = lse;
}

static uint32_t pps_seconds;
static bool pps_second_flag;

void board_pps_task(void)
{
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR = ~TIM_SR_UIF;
        pps_seconds++;
        pps_second_flag = true;
    }
    if (TIM2->SR & TIM_SR_CC1IF) {
        pps_last_ccr1 = TIM2->CCR1; /* 読むとCC1IFがclearされる */
        pps_count++;
        TIM2->SR = ~TIM_SR_CC1OF;
    }
}

bool board_pps_second_elapsed(uint32_t *seconds)
{
    if (!pps_second_flag) {
        return false;
    }
    pps_second_flag = false;
    *seconds = pps_seconds;
    return true;
}

void board_pps_stats(uint32_t *count, uint32_t *last_ccr1)
{
    *count = pps_count;
    *last_ccr1 = pps_last_ccr1;
}
