/*
 * 基板固有の初期化（GPIO、clock、SysTick、LED、TIM2）
 *
 * pinと初期化順序の正本は hardware/overall/pin_assignment.md と firmware_requirements.md。
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* software resetをまたいで原因を残す印（.noinit） */
#define BOARD_FAULT_MARKER_NONE 0u
#define BOARD_FAULT_MARKER_CSS 0x43535321u       /* "CSS!" HSE CSSでresetした */
#define BOARD_FAULT_MARKER_HARDFAULT 0x48464C54u /* "HFLT" HardFaultでbootloaderへ移った */
#define BOARD_FAULT_MARKER_BOOTLOADER_GO 0x424C474Fu /* "BLGO" bootloaderのGOから起動し、resetし直した */
#define BOARD_FAULT_MARKER_NMI 0x4E4D4921u           /* "NMI!" CSS以外のNMIでbootloaderへ移った */

/* 外部出力bufferのOE_N（全部active-low、外付け100 kOhm pull-up）。PB1=PPS、PB2=IMU、PB8=AUX1、
   PB9=AUX2、PB12=JETSON、PB13=VLP、PB14=MID360 */
#define BOARD_OE_N_PINS ((1u << 1) | (1u << 2) | (1u << 8) | (1u << 9) | (1u << 12) | (1u << 13) | (1u << 14))

typedef enum {
    BOARD_CLOCK_PLL_HSE = 0,  /* 正常: TCXO→HSE bypass→PLL 48 MHz、USB=PLLQ */
    BOARD_CLOCK_FALLBACK_HSI, /* 診断: HSE異常のためSYSCLK=HSI16、USB=HSI48+CRS。出力はHigh-Zのまま */
} board_clock_mode_t;

typedef enum {
    BOARD_CLK_OK = 0,
    BOARD_CLK_ERR_VOSF,     /* VOSFが下がらない */
    BOARD_CLK_ERR_LATENCY,  /* Flash wait stateが設定できない */
    BOARD_CLK_ERR_HSE,      /* HSERDYにならない（TCXO/U14/PF0の異常） */
    BOARD_CLK_ERR_PLL,      /* PLLRDYにならない */
    BOARD_CLK_ERR_SWITCH,   /* SYSCLKがPLLに切り替わらない */
    BOARD_CLK_ERR_CSS_RESET /* 前回HSE CSSでresetした */
} board_clock_status_t;

typedef struct {
    uint32_t rcc_csr_at_boot;   /* reset flagをclearする前のRCC_CSR（PWRRSTF=ES0548のBORRSTF） */
    bool flash_empty_at_boot;   /* FLASH_ACR.PROGEMPTY。初回書込み後は電源を入れ直すまで1のまま */
    uint32_t fault_marker;      /* 前回のresetの原因の印 */
    board_clock_mode_t clock_mode;
    board_clock_status_t clock_status;
    uint32_t hse_ready_ms;      /* HSEONからHSERDYまでの時間 */
    bool ucpd_strobed;          /* UCPD dead-battery pull-downのstrobeを書いた（write-only bitのため読み返せない） */
} board_boot_info_t;

extern volatile uint32_t g_fault_marker;
extern uint32_t SystemCoreClock;

/* reset直後に最初に呼ぶ。全OE_NをHighに保ち、reset flagを保存する */
void board_early_init(void);

/* PVM_VDDIO2、UCPD strobe、SysTick、5 ms待ち、clock、LED、TIM2、USB clockまで */
void board_init(void);

const board_boot_info_t *board_boot_info(void);

uint32_t board_millis(void);
void board_delay_ms(uint32_t ms);

void board_led(bool on);

/* TIM2 CH1（GNSS PPS、立下り）のcaptureを回収する。main loopから呼ぶ */
void board_pps_task(void);

/* 生成PPSの秒境界（TIM2 update）を過ぎていればtrueと起動からの秒数を返す（1回だけ） */
bool board_pps_second_elapsed(uint32_t *seconds);

/* これまでのcapture回数と最後のCCR1 */
void board_pps_stats(uint32_t *count, uint32_t *last_ccr1);

/*
 * LSE（X1 32.768 kHz）の起動確認と周波数測定（bring-up用）。
 * LSEDRV=10、LSECSSON=0で起動し、LSERDYまでの時間を記録する。起動後はTIM16のTI1にLSEを選び、
 * 8周期ごとのcaptureをTCXO由来の48 MHzで数えて、TCXOに対するLSEの周波数偏差を求める。
 * 製品firmwareではRTC-domainの検証（RT-03）を済ませてからBDCRを触る。
 */
typedef struct {
    bool started;          /* LSEONを書いた */
    bool ready;            /* LSERDYになった */
    bool was_running;      /* 起動時にすでに動いていた（software resetではbackup domainが保持される） */
    uint32_t ready_ms;     /* LSEONからLSERDYまでの時間 */
    uint32_t bdcr;         /* RCC_BDCR */
    uint32_t captures;     /* 有効な測定区間（LSE 8周期）の数 */
    uint64_t ticks;        /* 測定区間の48 MHz count合計 */
    uint32_t overcaptures; /* 取りこぼし（区間を捨てた回数） */
} board_lse_status_t;

void board_lse_start(void);
void board_lse_task(void);
void board_lse_status(board_lse_status_t *out);

/* bootloader移行要求を残してsoftware resetする（戻らない） */
__attribute__((noreturn)) void board_request_bootloader(void);

#endif /* BOARD_H */
