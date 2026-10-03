/*
 * UTC暦日時とUnix秒の変換
 *
 * うるう秒（second=60）は本moduleでは扱わない。入力NMEAに現れた場合の扱いは
 * firmware/test_plan.md のP-17で決める。
 */
#ifndef TS_TIME_H
#define TS_TIME_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t year;   /* 西暦 1970～9999 */
    uint8_t month;   /* 1～12 */
    uint8_t day;     /* 1～31 */
    uint8_t hour;    /* 0～23 */
    uint8_t minute;  /* 0～59 */
    uint8_t second;  /* 0～59 */
} ts_datetime_t;

#define TS_MIN_YEAR 1970u
#define TS_MAX_YEAR 9999u
#define TS_MAX_UNIX INT64_C(253402300799) /* 9999-12-31T23:59:59Z */

/* STM32内蔵RTCが表せる範囲（年は2桁で2000～2099） */
#define TS_RTC_MIN_UNIX INT64_C(946684800)  /* 2000-01-01T00:00:00Z */
#define TS_RTC_MAX_UNIX INT64_C(4102444799) /* 2099-12-31T23:59:59Z */

bool ts_is_leap_year(uint16_t year);

/* monthが範囲外なら0 */
uint8_t ts_days_in_month(uint16_t year, uint8_t month);

bool ts_datetime_is_valid(const ts_datetime_t *dt);

/* 1970-01-01からの日数（proleptic Gregorian）。H. Hinnantのdays_from_civil */
int64_t ts_days_from_civil(int32_t year, uint32_t month, uint32_t day);

/* dtが無効ならfalse */
bool ts_datetime_to_unix(const ts_datetime_t *dt, int64_t *unix_sec);

/* 0～TS_MAX_UNIXの範囲外ならfalse */
bool ts_unix_to_datetime(int64_t unix_sec, ts_datetime_t *dt);

/* 結果が範囲外になる場合はfalseを返し、dtを変更しない */
bool ts_datetime_add_seconds(ts_datetime_t *dt, int64_t delta);

bool ts_unix_in_rtc_range(int64_t unix_sec);

#endif /* TS_TIME_H */
