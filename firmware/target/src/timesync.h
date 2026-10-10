/*
 * 時刻同期の本体（製品firmware）
 *
 * GNSS（J2: UM982のPPSとRMC）で生成PPS（TIM2）を規律し、J3～J8の6系統へPPSとRMCを出す。
 * 状態遷移の正本は hardware/overall/time_source_state_machine.md。仕様が未決定の値（P-01、P-04、P-06～P-10）は
 * timesync.c冒頭の暫定値を使う。
 *
 * 時刻保持（内蔵RTC、backup domainの検証、RTC-only起動）はrtc_keep.cとtimesync.cの「内蔵RTC」節。
 * 未実装: watchdog（P-14）
 */
#ifndef TIMESYNC_H
#define TIMESYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "ts_jetson_proto.h"

/* main loopから: 初期化（board_initの後）と毎回の処理 */
void timesync_init(void);
void timesync_task(void);

/* 生成PPSの秒境界を過ぎていればtrueと境界の番号を返す（consoleの毎秒log用、1回だけ） */
bool timesync_second_elapsed(uint32_t *index);

/* status LEDの点灯（状態別の点滅pattern） */
bool timesync_led(uint32_t now_ms);

typedef struct {
    uint8_t state;      /* ts_jp_state_t */
    bool time_valid;
    int64_t unix_sec;   /* 現在のUTC（time_validのとき） */
    uint32_t nanoseconds;
    bool outputs_on;
    bool gnss_valid;    /* GNSS_LOCKEDまたはRESYNC */
    bool rtc_valid;     /* 内蔵RTCのcalendarが有効（起動時の検証に合格、またはこのbootで書いて確かめた） */
} timesync_status_t;

void timesync_get_status(timesync_status_t *st);

/*
 * Jetson（USB）からのSET_TIME/FORCE_SET_TIME。CRC・長さ・範囲・期限の検証は済んでいること。
 * rx_msはframe受信完了時刻（board_millis）。結果codeを返す。
 */
ts_jp_result_t timesync_jetson_set(const ts_jp_time_set_t *ts, uint32_t rx_ms, bool force);

/*
 * stepのSETは、時刻を適用し、RTCへ書いてbackupをcommitしてからACKを返す（jetson_time_protocol「エラー処理」）。
 * timesync_jetson_setがOKを返した後にwaitingならACKを保留し、resultが出たらその結果を返す。
 */
bool timesync_jetson_waiting(void);
bool timesync_jetson_result(uint8_t *code);

/* console command */
void timesync_cmd_status(const char *arg);
void timesync_cmd_gnss(const char *arg);
void timesync_cmd_out(const char *arg);
void timesync_cmd_reinit(const char *arg);
void timesync_log_line(uint32_t index);

#endif /* TIMESYNC_H */
