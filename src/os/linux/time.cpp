#include <time.h>
#include <unistd.h>
#include "os/time.h"

Void os_sleep_ms (U64 msec) {
    struct timespec ts_sleep = { static_cast<I64>(msec/1000), static_cast<I64>((msec % 1000) * 1000000) };
    nanosleep(&ts_sleep, NULL);
}

U64 os_monotonic_time_ms () {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<U64>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

DateTime os_get_date_time () {
    time_t now = time(0);
    struct tm tm = {};
    localtime_r(&now, &tm);
    return {
        .sec   = static_cast<U8>(tm.tm_sec),
        .min   = static_cast<U8>(tm.tm_min),
        .hour  = static_cast<U8>(tm.tm_hour),
        .day   = static_cast<U8>(tm.tm_mday),
        .month = static_cast<U8>(tm.tm_mon + 1),
        .wday  = static_cast<Day>(tm.tm_wday),
        .year  = tm.tm_year + 1900,
    };
}

Minutes os_get_time_in_minutes () {
    DateTime dt = os_get_date_time();
    return dt.hour*60 + dt.min;
}
