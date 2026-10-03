#include "ts_time.h"

#define SECONDS_PER_DAY 86400

bool ts_is_leap_year(uint16_t year)
{
    return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u;
}

uint8_t ts_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month < 1u || month > 12u) {
        return 0u;
    }
    if (month == 2u && ts_is_leap_year(year)) {
        return 29u;
    }
    return days[month - 1u];
}

bool ts_datetime_is_valid(const ts_datetime_t *dt)
{
    return dt->year >= TS_MIN_YEAR && dt->year <= TS_MAX_YEAR &&
           dt->month >= 1u && dt->month <= 12u &&
           dt->day >= 1u && dt->day <= ts_days_in_month(dt->year, dt->month) &&
           dt->hour <= 23u && dt->minute <= 59u && dt->second <= 59u;
}

int64_t ts_days_from_civil(int32_t year, uint32_t month, uint32_t day)
{
    const int32_t y = year - (month <= 2u ? 1 : 0);
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);
    const uint32_t mp = month > 2u ? month - 3u : month + 9u;
    const uint32_t doy = (153u * mp + 2u) / 5u + day - 1u;
    const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;

    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

/* H. Hinnantのcivil_from_days */
static void civil_from_days(int64_t days, int64_t *year, uint32_t *month, uint32_t *day)
{
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint32_t mp = (5u * doy + 2u) / 153u;

    *day = doy - (153u * mp + 2u) / 5u + 1u;
    *month = mp < 10u ? mp + 3u : mp - 9u;
    *year = (int64_t)yoe + era * 400 + (*month <= 2u ? 1 : 0);
}

bool ts_datetime_to_unix(const ts_datetime_t *dt, int64_t *unix_sec)
{
    if (!ts_datetime_is_valid(dt)) {
        return false;
    }
    *unix_sec = ts_days_from_civil(dt->year, dt->month, dt->day) * SECONDS_PER_DAY +
                (int64_t)dt->hour * 3600 + (int64_t)dt->minute * 60 + dt->second;
    return true;
}

bool ts_unix_to_datetime(int64_t unix_sec, ts_datetime_t *dt)
{
    int64_t year;
    uint32_t month;
    uint32_t day;

    if (unix_sec < 0 || unix_sec > TS_MAX_UNIX) {
        return false;
    }
    const uint32_t sec_of_day = (uint32_t)(unix_sec % SECONDS_PER_DAY);
    civil_from_days(unix_sec / SECONDS_PER_DAY, &year, &month, &day);

    dt->year = (uint16_t)year;
    dt->month = (uint8_t)month;
    dt->day = (uint8_t)day;
    dt->hour = (uint8_t)(sec_of_day / 3600u);
    dt->minute = (uint8_t)(sec_of_day / 60u % 60u);
    dt->second = (uint8_t)(sec_of_day % 60u);
    return true;
}

bool ts_datetime_add_seconds(ts_datetime_t *dt, int64_t delta)
{
    int64_t t;
    ts_datetime_t result;

    if (!ts_datetime_to_unix(dt, &t)) {
        return false;
    }
    if ((delta > 0 && t > TS_MAX_UNIX - delta) || (delta < 0 && t < -delta)) {
        return false;
    }
    if (!ts_unix_to_datetime(t + delta, &result)) {
        return false;
    }
    *dt = result;
    return true;
}

bool ts_unix_in_rtc_range(int64_t unix_sec)
{
    return unix_sec >= TS_RTC_MIN_UNIX && unix_sec <= TS_RTC_MAX_UNIX;
}
