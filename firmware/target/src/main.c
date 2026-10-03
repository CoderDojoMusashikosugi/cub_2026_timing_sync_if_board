/*
 * bring-up firmware
 *
 * 全出力をHigh-Zに保ったまま、clock、USB CDC console、TIM2（TP6の生成PPS、GNSS PPS capture）を動かす。
 * 外部コネクタへは何も出さない（OE_NはHighのまま）。
 */
#include "board.h"
#include "console.h"
#include "tusb.h"

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

/* 正常: 1秒に1回100 ms点灯。clock診断mode: 200 ms周期で点滅 */
static void led_task(void)
{
    const uint32_t now = board_millis();

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

    board_lse_start();

    const tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &dev_init);

    for (;;) {
        tud_task();
        console_task();
        board_pps_task();
        board_lse_task();
        led_task();
    }
}
