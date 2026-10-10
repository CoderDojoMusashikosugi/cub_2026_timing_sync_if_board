/* UT-05 NMEA解析、UT-06 NMEA生成、UT-07 VLP +1秒 */
#include <stdbool.h>

#include "test_harness.h"
#include "ts_nmea.h"

/* UM982の測位後のRMC。緯度経度が分の小数8桁で、規格の80文字を超える（Reference Commands Manual §7.2.11の例、83文字） */
static const char UM982_RMC[] =
    "$GNRMC,054733.00,A,4004.73893635,N,11614.19823325,E,0.002,155.1,301221,6.9,W,A,V*4B";

/* bodyは'$'と'*'の間。checksumを計算して"$body*hh"を作る */
static const char *with_checksum(const char *body)
{
    static char buf[128];
    const uint8_t sum = ts_nmea_checksum(body, strlen(body));
    snprintf(buf, sizeof(buf), "$%s*%02X", body, sum);
    return buf;
}

/* ts_datetime_tにはpaddingがあるためmemcmpで比べない */
static bool dt_equal(const ts_datetime_t *a, const ts_datetime_t *b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day && a->hour == b->hour &&
           a->minute == b->minute && a->second == b->second;
}

static ts_nmea_err_t parse(const char *s, ts_nmea_rmc_t *rmc)
{
    return ts_nmea_parse_rmc(s, strlen(s), rmc);
}

/* ---- checksum・検証 ---- */

static void test_checksum_known(void)
{
    /* NMEA RMCの定番例。checksumは0x6A */
    const char *s = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    size_t end = 0;

    CHECK_EQ_HEX(ts_nmea_checksum(s + 1, strlen(s) - 4), 0x6A);
    CHECK_EQ_INT(ts_nmea_verify(s, strlen(s), &end), TS_NMEA_OK);
    CHECK_EQ_INT(end, strlen(s) - 3);
}

static void test_verify_errors(void)
{
    const char *bad_sum = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B";
    const char *lower = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6a";
    const char *no_star = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W";
    const char *not_hex = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6G";
    const char *no_dollar = "GPRMC,123519,A*6A";
    const char *two_star = "$GP*RMC*00";

    CHECK_EQ_INT(ts_nmea_verify(bad_sum, strlen(bad_sum), NULL), TS_NMEA_ERR_CHECKSUM);
    CHECK_EQ_INT(ts_nmea_verify(lower, strlen(lower), NULL), TS_NMEA_OK);
    CHECK_EQ_INT(ts_nmea_verify(no_star, strlen(no_star), NULL), TS_NMEA_ERR_FORMAT);
    CHECK_EQ_INT(ts_nmea_verify(not_hex, strlen(not_hex), NULL), TS_NMEA_ERR_FORMAT);
    CHECK_EQ_INT(ts_nmea_verify(no_dollar, strlen(no_dollar), NULL), TS_NMEA_ERR_FORMAT);
    CHECK_EQ_INT(ts_nmea_verify(two_star, strlen(two_star), NULL), TS_NMEA_ERR_FORMAT);
    CHECK_EQ_INT(ts_nmea_verify("$*", 2, NULL), TS_NMEA_ERR_FORMAT);
}

/* ---- 受信組立て ---- */

static ts_nmea_rx_status_t push_str(ts_nmea_rx_t *rx, const char *s, const char **out, size_t *len,
                                    int *discards)
{
    ts_nmea_rx_status_t last = TS_NMEA_RX_NONE;
    while (*s != '\0') {
        last = ts_nmea_rx_push(rx, *s++, out, len);
        if (last == TS_NMEA_RX_DISCARDED) {
            (*discards)++;
        }
    }
    return last;
}

static void test_rx_assembly(void)
{
    ts_nmea_rx_t rx;
    const char *s = NULL;
    size_t len = 0;
    int discards = 0;

    ts_nmea_rx_init(&rx);
    CHECK_EQ_INT(push_str(&rx, "noise\r\n$GPRMC,1*00\r\n", &s, &len, &discards), TS_NMEA_RX_SENTENCE);
    CHECK_EQ_STR(s, "$GPRMC,1*00");
    CHECK_EQ_INT(len, 11);
    CHECK_EQ_INT(discards, 0);

    /* 途中で次の'$'が来たら前の文は捨てる */
    CHECK_EQ_INT(push_str(&rx, "$GPRMC,trunc$GPGGA,2*00\n", &s, &len, &discards), TS_NMEA_RX_SENTENCE);
    CHECK_EQ_STR(s, "$GPGGA,2*00");
    CHECK_EQ_INT(discards, 1);

    /* 制御文字は文を壊したものとして捨てる */
    discards = 0;
    CHECK_EQ_INT(push_str(&rx, "$GPRMC,\x01,x*00\n", &s, &len, &discards), TS_NMEA_RX_NONE);
    CHECK_EQ_INT(discards, 1);
}

static void test_rx_length_limit(void)
{
    ts_nmea_rx_t rx;
    const char *s = NULL;
    size_t len = 0;
    int discards = 0;
    char line[TS_NMEA_RX_MAX_BODY + 8];

    /* '$'からchecksum末尾まで120文字ちょうどは受け付ける */
    memset(line, 'A', sizeof(line));
    line[0] = '$';
    line[TS_NMEA_RX_MAX_BODY] = '\r';
    line[TS_NMEA_RX_MAX_BODY + 1] = '\n';
    line[TS_NMEA_RX_MAX_BODY + 2] = '\0';
    ts_nmea_rx_init(&rx);
    CHECK_EQ_INT(push_str(&rx, line, &s, &len, &discards), TS_NMEA_RX_SENTENCE);
    CHECK_EQ_INT(len, TS_NMEA_RX_MAX_BODY);

    /* 121文字は捨てる */
    memset(line, 'A', sizeof(line));
    line[0] = '$';
    line[TS_NMEA_RX_MAX_BODY + 1] = '\n';
    line[TS_NMEA_RX_MAX_BODY + 2] = '\0';
    CHECK_EQ_INT(push_str(&rx, line, &s, &len, &discards), TS_NMEA_RX_NONE);
    CHECK_EQ_INT(discards, 1);
}

static void test_rx_um982_rmc(void)
{
    ts_nmea_rx_t rx;
    const char *s = NULL;
    size_t len = 0;
    int discards = 0;
    char line[128];
    ts_nmea_rmc_t r;

    snprintf(line, sizeof(line), "%s\r\n", UM982_RMC);
    ts_nmea_rx_init(&rx);
    CHECK_EQ_INT(push_str(&rx, line, &s, &len, &discards), TS_NMEA_RX_SENTENCE);
    CHECK_EQ_INT(discards, 0);
    CHECK_EQ_INT(len, strlen(UM982_RMC));
    CHECK_EQ_INT(ts_nmea_parse_rmc(s, len, &r), TS_NMEA_OK);
    CHECK_EQ_STR(r.talker, "GN");
    CHECK_EQ_INT(r.centisecond, 0);
    CHECK_EQ_INT(r.status, 'A');
    CHECK_EQ_STR(r.lat, "4004.73893635");
    CHECK_EQ_STR(r.lon, "11614.19823325");
    CHECK_EQ_STR(r.mag_var, "6.9");
    CHECK_EQ_INT(r.mode, 'A');
}

/* ---- RMC解析 ---- */

static void test_parse_classic(void)
{
    ts_nmea_rmc_t r;

    CHECK_EQ_INT(parse("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", &r), TS_NMEA_OK);
    CHECK_EQ_STR(r.talker, "GP");
    CHECK_EQ_INT(r.utc.hour, 12);
    CHECK_EQ_INT(r.utc.minute, 35);
    CHECK_EQ_INT(r.utc.second, 19);
    CHECK_EQ_INT(r.utc.day, 23);
    CHECK_EQ_INT(r.utc.month, 3);
    CHECK_EQ_INT(r.utc.year, 2094); /* 2桁年は2000～2099として解釈する */
    CHECK_EQ_INT(r.status, 'A');
    CHECK_EQ_STR(r.lat, "4807.038");
    CHECK_EQ_INT(r.lat_hemi, 'N');
    CHECK_EQ_STR(r.lon, "01131.000");
    CHECK_EQ_INT(r.lon_hemi, 'E');
    CHECK_EQ_STR(r.speed_kn, "022.4");
    CHECK_EQ_STR(r.course_deg, "084.4");
    CHECK_EQ_STR(r.mag_var, "003.1");
    CHECK_EQ_INT(r.mag_var_dir, 'W');
    CHECK_EQ_INT(r.mode, '\0');
    CHECK(!r.leap_second);
}

static void test_parse_gnrmc_with_mode(void)
{
    ts_nmea_rmc_t r;

    /* mode indicatorとnavigational status付き（NMEA 4.1） */
    CHECK_EQ_INT(parse(with_checksum("GNRMC,013211.25,A,3150.7820,N,11711.9293,E,0.00,0.00,200421,,,A,V"), &r),
                 TS_NMEA_OK);
    CHECK_EQ_STR(r.talker, "GN");
    CHECK_EQ_INT(r.centisecond, 25);
    CHECK_EQ_INT(r.utc.year, 2021);
    CHECK_EQ_INT(r.mode, 'A');
    CHECK_EQ_STR(r.mag_var, "");
    CHECK_EQ_INT(r.mag_var_dir, '\0');

    CHECK_EQ_INT(parse(with_checksum("GNRMC,013211.5,V,,,,,,,200421,,,N"), &r), TS_NMEA_OK);
    CHECK_EQ_INT(r.status, 'V');
    CHECK_EQ_INT(r.centisecond, 50);
    CHECK_EQ_STR(r.lat, "");
    CHECK_EQ_INT(r.lat_hemi, '\0');
}

static void test_parse_errors(void)
{
    ts_nmea_rmc_t r;

    CHECK_EQ_INT(parse(with_checksum("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"), &r),
                 TS_NMEA_ERR_NOT_RMC);
    CHECK_EQ_INT(parse(with_checksum("GPRMC,,V,,,,,,,,,"), &r), TS_NMEA_ERR_FIELD);            /* 時刻なし */
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519,V,,,,,,,,,"), &r), TS_NMEA_ERR_FIELD);      /* 日付なし */
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519,,,,,,,,230394,,"), &r), TS_NMEA_ERR_FIELD); /* statusなし */
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519,X,,,,,,,230394,,"), &r), TS_NMEA_ERR_FIELD);
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519,A,,,,,,,230394,"), &r), TS_NMEA_ERR_FIELD); /* field不足 */
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519,A,,,,,,,230394,,,A,V,X"), &r), TS_NMEA_ERR_FIELD); /* 過多 */
    CHECK_EQ_INT(parse(with_checksum("GPRMC,12351,A,,,,,,,230394,,"), &r), TS_NMEA_ERR_FIELD);  /* 5桁 */
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519.,A,,,,,,,230394,,"), &r), TS_NMEA_ERR_FIELD);
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519.1234,A,,,,,,,230394,,"), &r), TS_NMEA_ERR_FIELD);
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519,A,48x7.038,N,,,,,230394,,"), &r), TS_NMEA_ERR_FIELD);
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519,A,,Q,,,,,230394,,"), &r), TS_NMEA_ERR_FIELD);
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123519,A,,,,,,,300221,,"), &r), TS_NMEA_ERR_DATETIME); /* 2/30 */
    CHECK_EQ_INT(parse(with_checksum("GPRMC,243519,A,,,,,,,230394,,"), &r), TS_NMEA_ERR_DATETIME); /* 24時 */
    CHECK_EQ_INT(parse(with_checksum("GPRMC,123560,A,,,,,,,230394,,"), &r), TS_NMEA_ERR_DATETIME); /* 12:35:60 */
    CHECK_EQ_INT(parse(with_checksum("gpRMC,123519,A,,,,,,,230394,,"), &r), TS_NMEA_ERR_FORMAT);
    CHECK_EQ_INT(parse("$GPRMC,123519,A,,,,,,,230394,,*00", &r), TS_NMEA_ERR_CHECKSUM);
}

static void test_parse_leap_second(void)
{
    ts_nmea_rmc_t r;

    CHECK_EQ_INT(parse(with_checksum("GPRMC,235960,A,,,,,,,311216,,"), &r), TS_NMEA_OK);
    CHECK(r.leap_second);
    CHECK_EQ_INT(r.utc.second, 60);
    TODO("P-17: 入力NMEAのうるう秒（23:59:60）を受けたときの状態機械・出力の扱いが未決定");
}

/* ---- RMC生成 ---- */

static void test_build_with_position(void)
{
    ts_nmea_rmc_t pos;
    char out[96];

    CHECK_EQ_INT(parse("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", &pos), TS_NMEA_OK);
    const ts_nmea_rmc_out_t in = {
        .talker = "GP",
        .utc = {2026, 10, 3, 12, 34, 56},
        .status = 'A',
        .position = &pos,
        .mode = 'A',
        .frac_digits = 2,
    };
    const size_t n = ts_nmea_build_rmc(out, sizeof(out), &in);
    const char *body = "GPRMC,123456.00,A,4807.038,N,01131.000,E,022.4,084.4,031026,003.1,W,A";
    char expected[96];
    snprintf(expected, sizeof(expected), "%s\r\n", with_checksum(body));
    CHECK_EQ_STR(out, expected);
    CHECK_EQ_INT(n, strlen(expected));
}

static void test_build_um982_position(void)
{
    ts_nmea_rmc_t pos;
    char out[96];
    char expected[96];

    CHECK_EQ_INT(parse(UM982_RMC, &pos), TS_NMEA_OK);
    const ts_nmea_rmc_out_t in = {
        .talker = "GP", .utc = {2021, 12, 30, 5, 47, 33}, .status = 'A', .position = &pos, .mode = 'A',
        .frac_digits = 2,
    };
    /* 緯度経度は分の小数5桁へ切り捨てて82文字に収める */
    const size_t n = ts_nmea_build_rmc(out, sizeof(out), &in);
    snprintf(expected, sizeof(expected), "%s\r\n",
             with_checksum("GPRMC,054733.00,A,4004.73893,N,11614.19823,E,0.002,155.1,301221,6.9,W,A"));
    CHECK_EQ_STR(out, expected);
    CHECK_EQ_INT(n, strlen(expected));

    /* 速度・方位・磁気偏差が長くなっても収まる */
    strcpy(pos.speed_kn, "999.999");
    strcpy(pos.course_deg, "359.9");
    strcpy(pos.mag_var, "10.0");
    CHECK(ts_nmea_build_rmc(out, sizeof(out), &in) > 0u);
}

static void test_build_holdover_without_position(void)
{
    char out[96];
    const ts_nmea_rmc_out_t in = {
        .talker = "GN",
        .utc = {2026, 1, 2, 3, 4, 5},
        .status = 'V',
        .position = NULL,
        .mode = 'N',
        .frac_digits = 0,
    };
    char expected[96];

    CHECK(ts_nmea_build_rmc(out, sizeof(out), &in) > 0u);
    snprintf(expected, sizeof(expected), "%s\r\n", with_checksum("GNRMC,030405,V,,,,,,,020126,,,N"));
    CHECK_EQ_STR(out, expected);
    TODO("P-09: holdover中・RESYNC中のposition fieldとstatusが未決定（現在は呼出し側が選ぶ）");
}

static void test_build_roundtrip(void)
{
    ts_nmea_rmc_t pos;
    ts_nmea_rmc_t back;
    char out[96];

    CHECK_EQ_INT(parse(with_checksum("GNRMC,013211.00,A,3150.7820,N,11711.9293,E,0.00,0.00,200421,,,A,V"), &pos),
                 TS_NMEA_OK);
    const ts_nmea_rmc_out_t in = {
        .talker = "GN", .utc = {2021, 4, 20, 1, 32, 11}, .status = 'A', .position = &pos, .mode = 'A',
        .frac_digits = 3,
    };
    const size_t n = ts_nmea_build_rmc(out, sizeof(out), &in);
    CHECK(n > 2u);
    CHECK_EQ_INT(ts_nmea_parse_rmc(out, n - 2u, &back), TS_NMEA_OK);
    CHECK(dt_equal(&back.utc, &in.utc));
    CHECK_EQ_STR(back.lat, pos.lat);
    CHECK_EQ_STR(back.lon, pos.lon);
    CHECK_EQ_INT(back.status, 'A');
    CHECK_EQ_INT(back.mode, 'A');
}

static void test_build_rejects(void)
{
    char out[96];
    char small[20];
    ts_nmea_rmc_out_t in = {
        .talker = "GP", .utc = {2026, 1, 1, 0, 0, 0}, .status = 'A', .position = NULL, .mode = '\0',
        .frac_digits = 0,
    };

    CHECK(ts_nmea_build_rmc(small, sizeof(small), &in) == 0u); /* cap不足 */
    CHECK(ts_nmea_build_rmc(out, 1, &in) == 0u);
    in.status = 'X';
    CHECK(ts_nmea_build_rmc(out, sizeof(out), &in) == 0u);
    in.status = 'A';
    in.utc.year = 2100; /* 2桁年で表せない */
    CHECK(ts_nmea_build_rmc(out, sizeof(out), &in) == 0u);
    in.utc.year = 2026;
    in.utc.month = 2;
    in.utc.day = 30;
    CHECK(ts_nmea_build_rmc(out, sizeof(out), &in) == 0u);
    in.utc.day = 1;
    in.frac_digits = 4;
    CHECK(ts_nmea_build_rmc(out, sizeof(out), &in) == 0u);
    in.frac_digits = 0;
    in.talker[0] = 'g';
    CHECK(ts_nmea_build_rmc(out, sizeof(out), &in) == 0u);
}

static void test_build_length_limit(void)
{
    ts_nmea_rmc_t pos;
    char out[128];

    /* 各fieldを最大長にすると82文字を超えるので0を返す */
    memset(&pos, 0, sizeof(pos));
    memset(pos.lat, '1', TS_NMEA_FIELD_MAX);
    memset(pos.lon, '2', TS_NMEA_FIELD_MAX);
    memset(pos.speed_kn, '3', TS_NMEA_FIELD_MAX);
    memset(pos.course_deg, '4', TS_NMEA_FIELD_MAX);
    pos.lat_hemi = 'N';
    pos.lon_hemi = 'E';
    const ts_nmea_rmc_out_t in = {
        .talker = "GP", .utc = {2026, 1, 1, 0, 0, 0}, .status = 'A', .position = &pos, .mode = 'A',
        .frac_digits = 2,
    };
    CHECK(ts_nmea_build_rmc(out, sizeof(out), &in) == 0u);
}

/* ---- VLP +1秒（UT-07） ---- */

static void test_vlp_plus_one_every_day(void)
{
    int mismatches = 0;
    int days = 0;

    for (uint16_t y = 2000; y <= 2099; y++) {
        for (uint8_t m = 1; m <= 12; m++) {
            for (uint8_t d = 1; d <= ts_days_in_month(y, m); d++) {
                ts_nmea_rmc_out_t in = {
                    .talker = "GP", .utc = {y, m, d, 23, 59, 59}, .status = 'A', .position = NULL,
                    .mode = '\0', .frac_digits = 2,
                };
                char out[96];
                ts_nmea_rmc_t back;
                bool ok = false;

                if (ts_datetime_add_seconds(&in.utc, 1)) {
                    const size_t n = ts_nmea_build_rmc(out, sizeof(out), &in);
                    if (y == 2099 && m == 12 && d == 31) {
                        ok = n == 0u; /* 2100年は2桁年で出せない */
                    } else if (n > 2u && ts_nmea_parse_rmc(out, n - 2u, &back) == TS_NMEA_OK) {
                        const uint8_t next_d = d == ts_days_in_month(y, m) ? 1u : (uint8_t)(d + 1u);
                        const uint8_t next_m = next_d != 1u ? m : (m == 12u ? 1u : (uint8_t)(m + 1u));
                        const uint16_t next_y = (next_d == 1u && m == 12u) ? (uint16_t)(y + 1u) : y;
                        ok = back.utc.year == next_y && back.utc.month == next_m && back.utc.day == next_d &&
                             back.utc.hour == 0u && back.utc.minute == 0u && back.utc.second == 0u;
                    }
                }
                if (!ok && mismatches++ < 5) {
                    fprintf(stderr, "VLP +1 mismatch at %04u-%02u-%02u\n", y, m, d);
                }
                days++;
            }
        }
    }
    CHECK_EQ_INT(mismatches, 0);
    CHECK_EQ_INT(days, 36525);
}

int main(void)
{
    RUN_TEST(test_checksum_known);
    RUN_TEST(test_verify_errors);
    RUN_TEST(test_rx_assembly);
    RUN_TEST(test_rx_length_limit);
    RUN_TEST(test_rx_um982_rmc);
    RUN_TEST(test_parse_classic);
    RUN_TEST(test_parse_gnrmc_with_mode);
    RUN_TEST(test_parse_errors);
    RUN_TEST(test_parse_leap_second);
    RUN_TEST(test_build_with_position);
    RUN_TEST(test_build_um982_position);
    RUN_TEST(test_build_holdover_without_position);
    RUN_TEST(test_build_roundtrip);
    RUN_TEST(test_build_rejects);
    RUN_TEST(test_build_length_limit);
    RUN_TEST(test_vlp_plus_one_every_day);
    TEST_EXIT();
}
