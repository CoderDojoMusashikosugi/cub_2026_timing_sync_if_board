/*
 * UT-08 calendar⇔Unix秒変換、UT-07のうち日付繰上げの基礎
 *
 * 参照実装として、被試験moduleを使わずに1日ずつ数える素朴な暦を本fileに持つ。
 * "--dump-rollover"を付けて起動すると2000～2099年の全日の23:59:59と+1秒後を出力し、
 * tools/run_host_tests.py がPythonのdatetimeと照合する。
 */
#include <stdbool.h>
#include <stdlib.h>

#include "test_harness.h"
#include "ts_time.h"

/* ---- 参照実装（被試験moduleとは独立） ---- */

typedef struct {
    int year;
    int month;
    int day;
} ref_date_t;

static bool ref_leap(int y)
{
    if (y % 400 == 0) {
        return true;
    }
    if (y % 100 == 0) {
        return false;
    }
    return y % 4 == 0;
}

static int ref_dim(int y, int m)
{
    switch (m) {
    case 2:
        return ref_leap(y) ? 29 : 28;
    case 4:
    case 6:
    case 9:
    case 11:
        return 30;
    default:
        return 31;
    }
}

static void ref_next_day(ref_date_t *d)
{
    d->day++;
    if (d->day > ref_dim(d->year, d->month)) {
        d->day = 1;
        d->month++;
        if (d->month > 12) {
            d->month = 1;
            d->year++;
        }
    }
}

static bool same_date(const ts_datetime_t *dt, const ref_date_t *r)
{
    return dt->year == r->year && dt->month == r->month && dt->day == r->day;
}

static bool is_time(const ts_datetime_t *dt, int h, int m, int s)
{
    return dt->hour == h && dt->minute == m && dt->second == s;
}

/* ts_datetime_tにはpaddingがあるためmemcmpで比べない */
static bool dt_equal(const ts_datetime_t *a, const ts_datetime_t *b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day && a->hour == b->hour &&
           a->minute == b->minute && a->second == b->second;
}

/* ---- tests ---- */

static void test_leap_years(void)
{
    CHECK(ts_is_leap_year(2000));
    CHECK(!ts_is_leap_year(1900));
    CHECK(!ts_is_leap_year(2100));
    CHECK(ts_is_leap_year(2024));
    CHECK(!ts_is_leap_year(2026));
    CHECK_EQ_INT(ts_days_in_month(2024, 2), 29);
    CHECK_EQ_INT(ts_days_in_month(2026, 2), 28);
    CHECK_EQ_INT(ts_days_in_month(2026, 4), 30);
    CHECK_EQ_INT(ts_days_in_month(2026, 0), 0);
    CHECK_EQ_INT(ts_days_in_month(2026, 13), 0);
}

static void test_known_unix(void)
{
    const struct {
        ts_datetime_t dt;
        int64_t unix_sec;
    } cases[] = {
        {{1970, 1, 1, 0, 0, 0}, 0},
        {{2000, 1, 1, 0, 0, 0}, 946684800},
        {{2024, 2, 29, 12, 0, 0}, 1709208000},
        {{2038, 1, 19, 3, 14, 8}, 2147483648LL},
        {{2099, 12, 31, 23, 59, 59}, 4102444799LL},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int64_t t = -1;
        ts_datetime_t back;
        CHECK(ts_datetime_to_unix(&cases[i].dt, &t));
        CHECK_EQ_INT(t, cases[i].unix_sec);
        CHECK(ts_unix_to_datetime(cases[i].unix_sec, &back));
        CHECK(dt_equal(&back, &cases[i].dt));
    }
    CHECK_EQ_INT(TS_RTC_MIN_UNIX, 946684800);
    CHECK_EQ_INT(TS_RTC_MAX_UNIX, 4102444799LL);
}

/* 1970-01-01～2099-12-31の全日で、日数・23:59:59・+1秒・-1秒を参照実装と照合する */
static void test_every_day_1970_2099(void)
{
    ref_date_t r = {1970, 1, 1};
    int64_t index = 0;
    int mismatches = 0;

    while (r.year <= 2099) {
        ts_datetime_t dt;
        ref_date_t next = r;
        ref_next_day(&next);

        const bool ok_days = ts_days_from_civil(r.year, (uint32_t)r.month, (uint32_t)r.day) == index;
        const bool ok_eod = ts_unix_to_datetime(index * 86400 + 86399, &dt) && same_date(&dt, &r) &&
                            is_time(&dt, 23, 59, 59);
        const bool ok_plus = ts_datetime_add_seconds(&dt, 1) && same_date(&dt, &next) && is_time(&dt, 0, 0, 0);
        const bool ok_minus = ts_datetime_add_seconds(&dt, -1) && same_date(&dt, &r) && is_time(&dt, 23, 59, 59);

        if (!(ok_days && ok_eod && ok_plus && ok_minus)) {
            if (mismatches++ < 5) {
                fprintf(stderr, "mismatch at %04d-%02d-%02d\n", r.year, r.month, r.day);
            }
        }
        r = next;
        index++;
    }
    CHECK_EQ_INT(mismatches, 0);
    CHECK_EQ_INT(index, 47482); /* 1970～2099年の日数 */
}

static void test_validity(void)
{
    const ts_datetime_t ok = {2026, 10, 3, 23, 59, 59};
    const ts_datetime_t bad[] = {
        {2023, 2, 29, 0, 0, 0},  /* 平年の2/29 */
        {2026, 4, 31, 0, 0, 0},  /* 4/31 */
        {2026, 13, 1, 0, 0, 0},  /* 13月 */
        {2026, 0, 1, 0, 0, 0},   /* 0月 */
        {2026, 1, 0, 0, 0, 0},   /* 0日 */
        {2026, 1, 1, 24, 0, 0},  /* 24時 */
        {2026, 1, 1, 0, 60, 0},  /* 60分 */
        {2026, 1, 1, 23, 59, 60}, /* うるう秒は本moduleでは無効 */
        {1969, 12, 31, 0, 0, 0}, /* 範囲外 */
    };
    int64_t t;

    CHECK(ts_datetime_is_valid(&ok));
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!ts_datetime_is_valid(&bad[i]));
        CHECK(!ts_datetime_to_unix(&bad[i], &t));
    }
}

static void test_rtc_range(void)
{
    CHECK(!ts_unix_in_rtc_range(TS_RTC_MIN_UNIX - 1));
    CHECK(ts_unix_in_rtc_range(TS_RTC_MIN_UNIX));
    CHECK(ts_unix_in_rtc_range(TS_RTC_MAX_UNIX));
    CHECK(!ts_unix_in_rtc_range(TS_RTC_MAX_UNIX + 1));
}

static void test_add_seconds_limits(void)
{
    ts_datetime_t dt = {1970, 1, 1, 0, 0, 0};
    const ts_datetime_t before = dt;

    CHECK(!ts_datetime_add_seconds(&dt, -1));
    CHECK(dt_equal(&dt, &before));

    ts_datetime_t end = {9999, 12, 31, 23, 59, 59};
    const ts_datetime_t end_before = end;
    CHECK(!ts_datetime_add_seconds(&end, 1));
    CHECK(dt_equal(&end, &end_before));

    ts_datetime_t x;
    CHECK(!ts_unix_to_datetime(-1, &x));
    CHECK(!ts_unix_to_datetime(TS_MAX_UNIX + 1, &x));
}

static int dump_rollover(void)
{
    const int64_t first = ts_days_from_civil(2000, 1, 1);
    const int64_t last = ts_days_from_civil(2099, 12, 31);

    for (int64_t d = first; d <= last; d++) {
        ts_datetime_t a;
        ts_datetime_t b;
        if (!ts_unix_to_datetime(d * 86400 + 86399, &a)) {
            return 1;
        }
        b = a;
        if (!ts_datetime_add_seconds(&b, 1)) {
            return 1;
        }
        printf("%04u-%02u-%02u %02u:%02u:%02u %04u-%02u-%02u %02u:%02u:%02u\n",
               a.year, a.month, a.day, a.hour, a.minute, a.second,
               b.year, b.month, b.day, b.hour, b.minute, b.second);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--dump-rollover") == 0) {
        return dump_rollover();
    }
    RUN_TEST(test_leap_years);
    RUN_TEST(test_known_unix);
    RUN_TEST(test_every_day_1970_2099);
    RUN_TEST(test_validity);
    RUN_TEST(test_rtc_range);
    RUN_TEST(test_add_seconds_limits);
    TEST_EXIT();
}
