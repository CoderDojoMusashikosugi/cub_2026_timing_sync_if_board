/*
 * NMEA 0183の受信組立て、検証、RMC解析・生成
 *
 * 出力sentenceの構成（talker、mode field、小数秒の桁数、holdover中のposition field）は
 * firmware/test_plan.md のP-08/P-09で決めるため、生成側は引数で選べるようにしてある。
 */
#ifndef TS_NMEA_H
#define TS_NMEA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ts_time.h"

/* '$'からCRLFまでの最大長（NMEA 0183） */
#define TS_NMEA_MAX_SENTENCE 82u
/* '$'からchecksum末尾まで（CRLFを除く） */
#define TS_NMEA_MAX_BODY (TS_NMEA_MAX_SENTENCE - 2u)
/* RMCの位置・速度などを生の文字列で保持するときの最大長 */
#define TS_NMEA_FIELD_MAX 15u

/* ---- 受信組立て ---- */

typedef enum {
    TS_NMEA_RX_NONE = 0,  /* 1文になっていない */
    TS_NMEA_RX_SENTENCE,  /* 1文が揃った（checksumは未検証） */
    TS_NMEA_RX_DISCARDED, /* 途中切れ、長すぎ、制御文字のため組立て中の文を捨てた */
} ts_nmea_rx_status_t;

typedef struct {
    char buf[TS_NMEA_MAX_BODY + 1u];
    uint8_t len;
    bool in_sentence;
} ts_nmea_rx_t;

void ts_nmea_rx_init(ts_nmea_rx_t *rx);

/*
 * 1文字を入れる。TS_NMEA_RX_SENTENCEのとき、*sentenceは'$'から始まりCR/LFを含まない
 * NUL終端文字列を指す（次のpushまで有効）。
 */
ts_nmea_rx_status_t ts_nmea_rx_push(ts_nmea_rx_t *rx, char c, const char **sentence, size_t *len);

/* ---- 検証 ---- */

typedef enum {
    TS_NMEA_OK = 0,
    TS_NMEA_ERR_FORMAT,   /* '$'や"*hh"がない、hexでない */
    TS_NMEA_ERR_CHECKSUM, /* checksum不一致 */
    TS_NMEA_ERR_NOT_RMC,  /* RMC以外のsentence */
    TS_NMEA_ERR_FIELD,    /* field数や書式が不正、必須fieldが空 */
    TS_NMEA_ERR_DATETIME, /* 存在しない日時 */
} ts_nmea_err_t;

/* sの各文字のXOR */
uint8_t ts_nmea_checksum(const char *s, size_t len);

/*
 * "$...*hh"の形とchecksumを検証する。hexの大文字・小文字はどちらも受け付ける。
 * body_endには'*'の位置が入る（NULなら返さない）。
 */
ts_nmea_err_t ts_nmea_verify(const char *s, size_t len, size_t *body_end);

/* ---- RMC ---- */

typedef struct {
    char talker[3];          /* "GP"、"GN"など */
    ts_datetime_t utc;       /* leap_second=trueのときsecondは60 */
    uint8_t centisecond;     /* hhmmss.ssの小数部（0～99）。ない場合は0 */
    bool leap_second;        /* 23:59:60を受信した（扱いはP-17） */
    char status;             /* 'A'または'V' */
    char lat[TS_NMEA_FIELD_MAX + 1u];
    char lat_hemi;           /* 'N'、'S'、空欄なら'\0' */
    char lon[TS_NMEA_FIELD_MAX + 1u];
    char lon_hemi;           /* 'E'、'W'、空欄なら'\0' */
    char speed_kn[TS_NMEA_FIELD_MAX + 1u];
    char course_deg[TS_NMEA_FIELD_MAX + 1u];
    char mag_var[TS_NMEA_FIELD_MAX + 1u];
    char mag_var_dir;        /* 'E'、'W'、空欄なら'\0' */
    char mode;               /* NMEA 2.3以降のmode indicator。ない場合は'\0' */
} ts_nmea_rmc_t;

/*
 * checksum検証込みでRMCを解析する。年はddmmyyの2桁を2000～2099として解釈する。
 * time/date/statusが空のsentenceはTS_NMEA_ERR_FIELDとする。
 */
ts_nmea_err_t ts_nmea_parse_rmc(const char *s, size_t len, ts_nmea_rmc_t *out);

typedef struct {
    char talker[3];                 /* 2文字の英大文字 */
    ts_datetime_t utc;              /* 出力する時刻。VLP向けは+1秒した値を渡す。年は2000～2099 */
    char status;                    /* 'A'または'V' */
    const ts_nmea_rmc_t *position;  /* 位置・速度・方位・磁気偏差の出所。NULLなら空欄 */
    char mode;                      /* '\0'ならmode fieldを出さない */
    uint8_t frac_digits;            /* 時刻の小数秒の桁数（0～3）。PPS境界の時刻なので常に0を並べる */
} ts_nmea_rmc_out_t;

/*
 * RMCを生成する。CRLFとNUL終端を付け、CRLF込みの長さを返す。
 * 82文字を超える場合、capが足りない場合、入力が不正な場合は0を返す。
 */
size_t ts_nmea_build_rmc(char *out, size_t cap, const ts_nmea_rmc_out_t *in);

#endif /* TS_NMEA_H */
