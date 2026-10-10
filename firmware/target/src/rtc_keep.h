/*
 * 内蔵RTC（LSE）とbackup register（TAMP_BKP0R～BKP4R）による時刻保持
 *
 * 正本は hardware/overall/time_source_state_machine.md「Clock・内蔵RTC設定」。
 * - 起動時にbackup domainを検証し、異常ならBDRST（set→read-back→clear）して時刻無効とする（RT-03）
 * - backup payloadはCRCを最後に書く（RT-04）。RTC-domain configuration CRCはsoftwareが設定するbitだけを対象にする
 * - calendarを書き直すときは、RSFを待ってからINITへ入り、全registerを1回のINIT sessionで書く（ES0548 §2.9.1）
 *
 * backup registerの割当（5本）:
 *   BKP0R: signature 0x5453（"TS"）<<16 | version<<8 | flags（bit0: TCXO補正値はGNSSで検証済み）
 *   BKP1R: TCXO補正値（count/秒、Q16、int32）
 *   BKP2R: 最後に同期したUTC（Unix秒、uint32）
 *   BKP3R: application CRC（BKP0R、BKP1R、BKP2R、BKP4RのCRC32C）。最後に書く
 *   BKP4R: RTC-domain configuration CRC
 */
#ifndef RTC_KEEP_H
#define RTC_KEEP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;            /* 起動時の検証に合格した（RTC_VALID） */
    const char *reason;    /* 不合格の理由（合格なら"OK"） */
    bool did_bdrst;        /* BDRSTでbackup domainを初期化した */
    bool freq_verified;    /* 保存してあったTCXO補正値はGNSSで検証済み */
    int32_t freq_q16;
    uint32_t sync_unix;
    int64_t boot_unix;     /* 起動時のcalendar */
} rtck_boot_t;

/* board_lse_start()より前に呼ぶ。検証し、不合格ならBDRSTする */
void rtck_boot_check(void);
const rtck_boot_t *rtck_boot(void);

/* main loopから: LSEが起動したらRTCSEL=LSE・RTCEN、1 Hz wakeup割込みを設定する */
void rtck_task(void);

/* configuration CRCの対象（RTCSEL、RTCEN、wakeup設定）を変えたらtrue（1回だけ）。backupの書き直しが要る */
bool rtck_config_changed(void);

/* LSEが動き、RTCがLSEで動いている（calendarを書ける） */
bool rtck_clock_ready(void);

/* calendarが初期化済み（INITS=1）で読める */
bool rtck_calendar_valid(void);

/* calendarを読む。sub256は秒内の位置（0～255、1/256秒単位） */
bool rtck_read(int64_t *unix_sec, uint32_t *sub256);

/*
 * calendarの書込み: INITへ入ってunix_secを書き、rtck_write_exit()でINITを抜けた瞬間からその秒を数え始める
 * （実際の再開はINIT解除からRTCCLK 4周期後）。
 */
bool rtck_write_begin(int64_t unix_sec);
void rtck_write_exit(void);  /* 割込みから呼べる */
void rtck_write_end(void);   /* 書込み保護を戻す */
bool rtck_in_init(void);

/* backup payloadを書く（CRCを最後に書く） */
void rtck_commit(uint32_t sync_unix, int32_t freq_q16, bool freq_verified);

/* smooth calibration。lse_fast_ppbはLSEの偏差（正で速い）。CALRを変えたらtrue */
bool rtck_set_calibration(int32_t lse_fast_ppb);
int32_t rtck_calibration_ppb(void); /* 今のCALRによる補正量（正で速める） */

/* RTC wakeup（1 Hz）の割込みflagを下ろす。立っていたらtrue */
bool rtck_wakeup_ack(void);

#endif /* RTC_KEEP_H */
