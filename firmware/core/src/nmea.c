#include "ts_nmea.h"

#include <string.h>

/* address field + 13 data field（NMEA 4.1のnavigational statusまで） */
#define RMC_MIN_FIELDS 12u
#define RMC_MAX_FIELDS 14u

typedef struct {
    const char *p;
    size_t n;
} field_t;

/* ---- 受信組立て ---- */

void ts_nmea_rx_init(ts_nmea_rx_t *rx)
{
    rx->len = 0;
    rx->in_sentence = false;
}

ts_nmea_rx_status_t ts_nmea_rx_push(ts_nmea_rx_t *rx, char c, const char **sentence, size_t *len)
{
    const unsigned char uc = (unsigned char)c;

    if (c == '$') {
        const bool discarded = rx->in_sentence;
        rx->buf[0] = '$';
        rx->len = 1;
        rx->in_sentence = true;
        return discarded ? TS_NMEA_RX_DISCARDED : TS_NMEA_RX_NONE;
    }
    if (!rx->in_sentence) {
        return TS_NMEA_RX_NONE;
    }
    if (c == '\r') {
        return TS_NMEA_RX_NONE;
    }
    if (c == '\n') {
        rx->in_sentence = false;
        rx->buf[rx->len] = '\0';
        *sentence = rx->buf;
        *len = rx->len;
        return TS_NMEA_RX_SENTENCE;
    }
    if (uc < 0x20u || uc > 0x7Eu || rx->len >= TS_NMEA_RX_MAX_BODY) {
        rx->in_sentence = false;
        return TS_NMEA_RX_DISCARDED;
    }
    rx->buf[rx->len++] = c;
    return TS_NMEA_RX_NONE;
}

/* ---- 検証 ---- */

uint8_t ts_nmea_checksum(const char *s, size_t len)
{
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum ^= (uint8_t)s[i];
    }
    return sum;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

ts_nmea_err_t ts_nmea_verify(const char *s, size_t len, size_t *body_end)
{
    if (len < 4u || s[0] != '$' || s[len - 3u] != '*') {
        return TS_NMEA_ERR_FORMAT;
    }
    const int hi = hex_value(s[len - 2u]);
    const int lo = hex_value(s[len - 1u]);
    if (hi < 0 || lo < 0 || memchr(s + 1, '*', len - 4u) != NULL) {
        return TS_NMEA_ERR_FORMAT;
    }
    if (ts_nmea_checksum(s + 1, len - 4u) != (uint8_t)(hi * 16 + lo)) {
        return TS_NMEA_ERR_CHECKSUM;
    }
    if (body_end != NULL) {
        *body_end = len - 3u;
    }
    return TS_NMEA_OK;
}

/* ---- RMC解析 ---- */

/* s[0..len)を','で分ける。field数がmaxを超えたらmax + 1を返す */
static size_t split_fields(const char *s, size_t len, field_t *fields, size_t max)
{
    size_t count = 0;
    size_t start = 0;

    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == ',') {
            if (count == max) {
                return max + 1u;
            }
            fields[count].p = s + start;
            fields[count].n = i - start;
            count++;
            start = i + 1u;
        }
    }
    return count;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool all_digits(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (!is_digit(p[i])) {
            return false;
        }
    }
    return true;
}

static unsigned dec2(const char *p)
{
    return (unsigned)(p[0] - '0') * 10u + (unsigned)(p[1] - '0');
}

/* 数値field（数字と'.'だけ、空欄可）を文字列としてコピーする */
static bool copy_numeric(const field_t *f, char *dst)
{
    if (f->n > TS_NMEA_FIELD_MAX) {
        return false;
    }
    for (size_t i = 0; i < f->n; i++) {
        if (!is_digit(f->p[i]) && f->p[i] != '.') {
            return false;
        }
    }
    memcpy(dst, f->p, f->n);
    dst[f->n] = '\0';
    return true;
}

/* 1文字fieldで許される文字のどれか、または空欄 */
static bool parse_char_field(const field_t *f, const char *allowed, char *dst)
{
    if (f->n == 0u) {
        *dst = '\0';
        return true;
    }
    if (f->n != 1u || f->p[0] == '\0' || strchr(allowed, f->p[0]) == NULL) {
        return false;
    }
    *dst = f->p[0];
    return true;
}

static bool parse_time_field(const field_t *f, ts_nmea_rmc_t *out)
{
    if (f->n < 6u || !all_digits(f->p, 6u)) {
        return false;
    }
    out->centisecond = 0;
    if (f->n > 6u) {
        const size_t frac = f->n - 7u;
        if (f->p[6] != '.' || frac < 1u || frac > 3u || !all_digits(f->p + 7, frac)) {
            return false;
        }
        out->centisecond = (uint8_t)((unsigned)(f->p[7] - '0') * 10u +
                                     (frac >= 2u ? (unsigned)(f->p[8] - '0') : 0u));
    }
    out->utc.hour = (uint8_t)dec2(f->p);
    out->utc.minute = (uint8_t)dec2(f->p + 2);
    out->utc.second = (uint8_t)dec2(f->p + 4);
    return true;
}

static bool parse_date_field(const field_t *f, ts_nmea_rmc_t *out)
{
    if (f->n != 6u || !all_digits(f->p, 6u)) {
        return false;
    }
    out->utc.day = (uint8_t)dec2(f->p);
    out->utc.month = (uint8_t)dec2(f->p + 2);
    out->utc.year = (uint16_t)(2000u + dec2(f->p + 4));
    return true;
}

ts_nmea_err_t ts_nmea_parse_rmc(const char *s, size_t len, ts_nmea_rmc_t *out)
{
    field_t f[RMC_MAX_FIELDS];
    size_t body_end;
    char nav_status;

    const ts_nmea_err_t err = ts_nmea_verify(s, len, &body_end);
    if (err != TS_NMEA_OK) {
        return err;
    }

    const size_t count = split_fields(s + 1, body_end - 1u, f, RMC_MAX_FIELDS);
    if (f[0].n != 5u || memcmp(f[0].p + 2, "RMC", 3u) != 0) {
        return TS_NMEA_ERR_NOT_RMC;
    }
    if (f[0].p[0] < 'A' || f[0].p[0] > 'Z' || f[0].p[1] < 'A' || f[0].p[1] > 'Z') {
        return TS_NMEA_ERR_FORMAT;
    }
    if (count < RMC_MIN_FIELDS || count > RMC_MAX_FIELDS) {
        return TS_NMEA_ERR_FIELD;
    }

    memset(out, 0, sizeof(*out));
    out->talker[0] = f[0].p[0];
    out->talker[1] = f[0].p[1];

    if (!parse_time_field(&f[1], out) ||
        f[2].n != 1u || (f[2].p[0] != 'A' && f[2].p[0] != 'V') ||
        !copy_numeric(&f[3], out->lat) ||
        !parse_char_field(&f[4], "NS", &out->lat_hemi) ||
        !copy_numeric(&f[5], out->lon) ||
        !parse_char_field(&f[6], "EW", &out->lon_hemi) ||
        !copy_numeric(&f[7], out->speed_kn) ||
        !copy_numeric(&f[8], out->course_deg) ||
        !parse_date_field(&f[9], out) ||
        !copy_numeric(&f[10], out->mag_var) ||
        !parse_char_field(&f[11], "EW", &out->mag_var_dir)) {
        return TS_NMEA_ERR_FIELD;
    }
    out->status = f[2].p[0];
    if (count >= 13u && !parse_char_field(&f[12], "ADEFMNPRS", &out->mode)) {
        return TS_NMEA_ERR_FIELD;
    }
    if (count >= 14u && !parse_char_field(&f[13], "ACSUV", &nav_status)) {
        return TS_NMEA_ERR_FIELD;
    }

    /* うるう秒は23:59:60だけを受け付け、日付の検証は59秒として行う */
    ts_datetime_t check = out->utc;
    if (out->utc.second == 60u) {
        if (out->utc.hour != 23u || out->utc.minute != 59u) {
            return TS_NMEA_ERR_DATETIME;
        }
        out->leap_second = true;
        check.second = 59u;
    }
    if (!ts_datetime_is_valid(&check)) {
        return TS_NMEA_ERR_DATETIME;
    }
    return TS_NMEA_OK;
}

/* ---- RMC生成 ---- */

typedef struct {
    char *p;
    size_t cap;
    size_t len;
    bool overflow;
} sbuf_t;

static void put_char(sbuf_t *b, char c)
{
    if (b->len + 1u >= b->cap) {
        b->overflow = true;
        return;
    }
    b->p[b->len++] = c;
}

static void put_str(sbuf_t *b, const char *s)
{
    while (*s != '\0') {
        put_char(b, *s++);
    }
}

static void put_dec2(sbuf_t *b, unsigned v)
{
    put_char(b, (char)('0' + v / 10u % 10u));
    put_char(b, (char)('0' + v % 10u));
}

static void put_opt_char(sbuf_t *b, char c)
{
    if (c != '\0') {
        put_char(b, c);
    }
}

/* 緯度経度を、小数点以下TS_NMEA_OUT_COORD_FRAC桁までで切り捨てて書く */
static void put_coord(sbuf_t *b, const char *s)
{
    const char *dot = strchr(s, '.');
    size_t n = strlen(s);

    if (dot != NULL && n - (size_t)(dot - s) - 1u > TS_NMEA_OUT_COORD_FRAC) {
        n = (size_t)(dot - s) + 1u + TS_NMEA_OUT_COORD_FRAC;
    }
    for (size_t i = 0; i < n; i++) {
        put_char(b, s[i]);
    }
}

size_t ts_nmea_build_rmc(char *out, size_t cap, const ts_nmea_rmc_out_t *in)
{
    static const char hex[] = "0123456789ABCDEF";
    const ts_nmea_rmc_t *pos = in->position;
    sbuf_t b = {out, cap, 0u, false};

    if (cap == 0u || (in->status != 'A' && in->status != 'V') || in->frac_digits > 3u ||
        in->talker[0] < 'A' || in->talker[0] > 'Z' || in->talker[1] < 'A' || in->talker[1] > 'Z' ||
        !ts_datetime_is_valid(&in->utc) || in->utc.year < 2000u || in->utc.year > 2099u) {
        return 0u;
    }

    put_char(&b, '$');
    put_char(&b, in->talker[0]);
    put_char(&b, in->talker[1]);
    put_str(&b, "RMC,");
    put_dec2(&b, in->utc.hour);
    put_dec2(&b, in->utc.minute);
    put_dec2(&b, in->utc.second);
    if (in->frac_digits > 0u) {
        put_char(&b, '.');
        for (unsigned i = 0; i < in->frac_digits; i++) {
            put_char(&b, '0');
        }
    }
    put_char(&b, ',');
    put_char(&b, in->status);
    put_char(&b, ',');
    if (pos != NULL) {
        put_coord(&b, pos->lat);
        put_char(&b, ',');
        put_opt_char(&b, pos->lat_hemi);
        put_char(&b, ',');
        put_coord(&b, pos->lon);
        put_char(&b, ',');
        put_opt_char(&b, pos->lon_hemi);
        put_char(&b, ',');
        put_str(&b, pos->speed_kn);
        put_char(&b, ',');
        put_str(&b, pos->course_deg);
        put_char(&b, ',');
    } else {
        put_str(&b, ",,,,,,");
    }
    put_dec2(&b, in->utc.day);
    put_dec2(&b, in->utc.month);
    put_dec2(&b, in->utc.year % 100u);
    put_char(&b, ',');
    if (pos != NULL) {
        put_str(&b, pos->mag_var);
        put_char(&b, ',');
        put_opt_char(&b, pos->mag_var_dir);
    } else {
        put_char(&b, ',');
    }
    if (in->mode != '\0') {
        put_char(&b, ',');
        put_char(&b, in->mode);
    }

    if (b.overflow) {
        out[0] = '\0';
        return 0u;
    }
    const uint8_t sum = ts_nmea_checksum(out + 1, b.len - 1u);
    put_char(&b, '*');
    put_char(&b, hex[sum >> 4]);
    put_char(&b, hex[sum & 0x0Fu]);
    put_str(&b, "\r\n");

    if (b.overflow || b.len > TS_NMEA_MAX_SENTENCE) {
        out[0] = '\0';
        return 0u;
    }
    out[b.len] = '\0';
    return b.len;
}
