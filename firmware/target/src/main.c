/*
 * bring-up firmware / 製品firmware（FW_APP）
 *
 * bring-up: 全出力をHigh-Zに保ったまま、clock、USB CDC console、TIM2（TP6の生成PPS、GNSS PPS capture）を動かす。
 * 外部コネクタへは何も出さない（OE_NはHighのまま）。
 * 製品firmware: GNSSで規律したPPSとRMCをJ3～J8へ出す（timesync.c）。JetsonとはUSBの2つ目のCDCで通信する。
 */
#include "board.h"
#include "console.h"
#include "tusb.h"
#ifdef FW_APP
#include "jetson_usb.h"
#include "rtc_keep.h"
#include "stm32g0xx.h"
#include "timesync.h"
#endif

#define LED_PERIOD_MS 1000u
#define LED_ON_MS_NORMAL 100u
#define LED_PERIOD_FALLBACK_MS 200u

uint32_t tusb_time_millis_api(void)
{
    return board_millis();
}

void USB_UCPD1_2_IRQHandler(void)
{
    tusb_int_handler(0, true);
}

/* 正常: 1秒に1回100 ms点灯。clock診断mode: 200 ms周期で点滅。製品firmwareは状態別（timesync_led） */
static void led_task(void)
{
    const uint32_t now = board_millis();

#ifdef FW_APP
    if (board_boot_info()->clock_mode == BOARD_CLOCK_PLL_HSE) {
        board_led(timesync_led(now));
        return;
    }
#endif
    if (board_boot_info()->clock_mode == BOARD_CLOCK_PLL_HSE) {
        board_led(now % LED_PERIOD_MS < LED_ON_MS_NORMAL);
    } else {
        board_led(now % LED_PERIOD_FALLBACK_MS < LED_PERIOD_FALLBACK_MS / 2u);
    }
}

int main(void)
{
    board_early_init();
    board_init();

#ifdef FW_APP
    /* RTC-domainを検証してからBDCRを触る（RT-03）。異常ならここでBDRSTする */
    rtck_boot_check();
#endif
    board_lse_start();

    const tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &dev_init);
#ifdef FW_APP
    /* TIM2（PPS）とUARTの割込みをUSBより優先する */
    NVIC_SetPriority(USB_UCPD1_2_IRQn, 2);
    timesync_init();
    jetson_usb_init();
#endif

    for (;;) {
        tud_task();
        console_task();
#ifdef FW_APP
        timesync_task();
        jetson_usb_task();
#else
        board_pps_task();
#endif
        board_lse_task();
        led_task();
    }
}
