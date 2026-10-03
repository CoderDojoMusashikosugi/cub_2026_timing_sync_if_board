/*
 * 起動処理とvector table（STM32G0B1）
 *
 * Reset_Handlerは周辺を一切触る前に、前回のsoftware resetで残したbootloader移行要求を確認する。
 * 要求があればreset直後の状態のままsystem memoryのbootloader（USB DFU）へ移る。
 * HardFaultでも同じ要求を残してresetするため、firmwareが壊れてもUSBから書き直せる。
 */
#include <stdint.h>

#include "board.h"
#include "stm32g0xx.h"

#define SYSTEM_MEMORY_BASE 0x1FFF0000u
#define BOOTLOADER_MAGIC 0x424F4F54u /* "BOOT" */

typedef void (*vector_t)(void);

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

/* software resetをまたいで残す値（.noinitは起動時に消さない） */
__attribute__((section(".noinit"))) static volatile uint32_t bootloader_request;
__attribute__((section(".noinit"))) static volatile uint32_t bootloader_request_inv;
__attribute__((section(".noinit"))) volatile uint32_t g_fault_marker;

void Reset_Handler(void);
void Default_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);

#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void SVC_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT;
void SysTick_Handler(void) WEAK_DEFAULT;
void WWDG_IRQHandler(void) WEAK_DEFAULT;
void PVD_VDDIO2_IRQHandler(void) WEAK_DEFAULT;
void RTC_TAMP_IRQHandler(void) WEAK_DEFAULT;
void FLASH_IRQHandler(void) WEAK_DEFAULT;
void RCC_CRS_IRQHandler(void) WEAK_DEFAULT;
void EXTI0_1_IRQHandler(void) WEAK_DEFAULT;
void EXTI2_3_IRQHandler(void) WEAK_DEFAULT;
void EXTI4_15_IRQHandler(void) WEAK_DEFAULT;
void USB_UCPD1_2_IRQHandler(void) WEAK_DEFAULT;
void DMA1_Channel1_IRQHandler(void) WEAK_DEFAULT;
void DMA1_Channel2_3_IRQHandler(void) WEAK_DEFAULT;
void DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQHandler(void) WEAK_DEFAULT;
void ADC1_COMP_IRQHandler(void) WEAK_DEFAULT;
void TIM1_BRK_UP_TRG_COM_IRQHandler(void) WEAK_DEFAULT;
void TIM1_CC_IRQHandler(void) WEAK_DEFAULT;
void TIM2_IRQHandler(void) WEAK_DEFAULT;
void TIM3_TIM4_IRQHandler(void) WEAK_DEFAULT;
void TIM6_DAC_LPTIM1_IRQHandler(void) WEAK_DEFAULT;
void TIM7_LPTIM2_IRQHandler(void) WEAK_DEFAULT;
void TIM14_IRQHandler(void) WEAK_DEFAULT;
void TIM15_IRQHandler(void) WEAK_DEFAULT;
void TIM16_FDCAN_IT0_IRQHandler(void) WEAK_DEFAULT;
void TIM17_FDCAN_IT1_IRQHandler(void) WEAK_DEFAULT;
void I2C1_IRQHandler(void) WEAK_DEFAULT;
void I2C2_3_IRQHandler(void) WEAK_DEFAULT;
void SPI1_IRQHandler(void) WEAK_DEFAULT;
void SPI2_3_IRQHandler(void) WEAK_DEFAULT;
void USART1_IRQHandler(void) WEAK_DEFAULT;
void USART2_LPUART2_IRQHandler(void) WEAK_DEFAULT;
void USART3_4_5_6_LPUART1_IRQHandler(void) WEAK_DEFAULT;
void CEC_IRQHandler(void) WEAK_DEFAULT;
void AES_RNG_IRQHandler(void) WEAK_DEFAULT;

__attribute__((section(".isr_vector"), used)) static const vector_t vector_table[16 + 32] = {
    (vector_t)(uintptr_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0,
    SVC_Handler,
    0, 0,
    PendSV_Handler,
    SysTick_Handler,
    WWDG_IRQHandler,
    PVD_VDDIO2_IRQHandler,
    RTC_TAMP_IRQHandler,
    FLASH_IRQHandler,
    RCC_CRS_IRQHandler,
    EXTI0_1_IRQHandler,
    EXTI2_3_IRQHandler,
    EXTI4_15_IRQHandler,
    USB_UCPD1_2_IRQHandler,
    DMA1_Channel1_IRQHandler,
    DMA1_Channel2_3_IRQHandler,
    DMA1_Ch4_7_DMA2_Ch1_5_DMAMUX1_OVR_IRQHandler,
    ADC1_COMP_IRQHandler,
    TIM1_BRK_UP_TRG_COM_IRQHandler,
    TIM1_CC_IRQHandler,
    TIM2_IRQHandler,
    TIM3_TIM4_IRQHandler,
    TIM6_DAC_LPTIM1_IRQHandler,
    TIM7_LPTIM2_IRQHandler,
    TIM14_IRQHandler,
    TIM15_IRQHandler,
    TIM16_FDCAN_IT0_IRQHandler,
    TIM17_FDCAN_IT1_IRQHandler,
    I2C1_IRQHandler,
    I2C2_3_IRQHandler,
    SPI1_IRQHandler,
    SPI2_3_IRQHandler,
    USART1_IRQHandler,
    USART2_LPUART2_IRQHandler,
    USART3_4_5_6_LPUART1_IRQHandler,
    CEC_IRQHandler,
    AES_RNG_IRQHandler,
};

/* reset直後の状態（clock=HSI16、周辺停止）でsystem memoryのbootloaderへ移る */
__attribute__((noreturn)) static void jump_to_system_memory(void)
{
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) | SYSCFG_CFGR1_MEM_MODE_0;
    SCB->VTOR = 0u; /* system memoryを0番地へ割り当てたので、bootloaderのvectorを使わせる */
    __DSB();
    __ISB();

    const uint32_t sp = *(volatile const uint32_t *)SYSTEM_MEMORY_BASE;
    const uint32_t pc = *(volatile const uint32_t *)(SYSTEM_MEMORY_BASE + 4u);
    __set_MSP(sp);
    ((void (*)(void))pc)();
    for (;;) {
    }
}

void board_request_bootloader(void)
{
    __disable_irq();
    bootloader_request = BOOTLOADER_MAGIC;
    bootloader_request_inv = ~BOOTLOADER_MAGIC;
    __DSB();
    NVIC_SystemReset();
}

void Reset_Handler(void)
{
    if (bootloader_request == BOOTLOADER_MAGIC && bootloader_request_inv == ~BOOTLOADER_MAGIC) {
        bootloader_request = 0;
        bootloader_request_inv = 0;
        jump_to_system_memory();
    }

    /*
     * bootloaderのGO command（DFU書込み後の起動）から来た場合は、0番地がsystem memoryのままで、
     * bootloaderが設定したclockやUSBも残っている。flashは書込み済みなのでEMPTY bitを0にしてから
     * software resetをかけ直し、全周辺がreset状態の通常起動にする（EMPTYはsoftwareで書ける。RM0444）。
     */
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;
    if ((SYSCFG->CFGR1 & SYSCFG_CFGR1_MEM_MODE) != 0u) {
        FLASH->ACR &= ~FLASH_ACR_PROGEMPTY;
        g_fault_marker = BOARD_FAULT_MARKER_BOOTLOADER_GO;
        __DSB();
        NVIC_SystemReset();
    }
    SCB->VTOR = (uint32_t)(uintptr_t)vector_table;
    __DSB();

    for (uint32_t *src = &_sidata, *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    for (uint32_t *dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0;
    }

    main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    for (;;) {
    }
}

/* HSE CSSはNMIになる。HSIへ自動で切り替わりUSB clock（PLLQ）を失うため、印を残してresetし、
   次の起動でHSE異常として扱う（bring-upではUSBをHSI48へ逃がして診断を続ける） */
void NMI_Handler(void)
{
    if (RCC->CIFR & RCC_CIFR_CSSF) {
        RCC->CICR = RCC_CICR_CSSC;
        g_fault_marker = BOARD_FAULT_MARKER_CSS;
        __DSB();
        NVIC_SystemReset();
    }
    /* CSS以外のNMI（SRAM parity、flash ECCなど）はHardFaultと同じくbootloaderへ逃がす */
    g_fault_marker = BOARD_FAULT_MARKER_NMI;
    board_request_bootloader();
}

/* 原因を問わずbootloaderへ逃がし、USBから書き直せる状態にする */
void HardFault_Handler(void)
{
    g_fault_marker = BOARD_FAULT_MARKER_HARDFAULT;
    board_request_bootloader();
}
