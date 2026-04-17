#include <time.h>
#include <unistd.h>
#include "base/core.h"
#include "os/time.h"

U32 os_first_weekday (U32 year, U32 month) {
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    mktime(&t);
    return t.tm_wday;
}

U32 os_days_in_month (U32 year, U32 month) {
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = month;
    t.tm_mday = 0;
    mktime(&t);
    return t.tm_mday;
}

Date os_get_date () {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm local;
    localtime_r(&ts.tv_sec, &local);

    return (Date){
        .year  = local.tm_year + 1900,
        .month = local.tm_mon + 1,
        .day   = local.tm_mday,
        .wday  = local.tm_wday,
    };
}

Void os_sleep_ms (U64 msec) {
    struct timespec ts_sleep = { msec/1000, (msec % 1000) * 1000000 };
    nanosleep(&ts_sleep, NULL);
}

U64 os_get_time_ms () {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return cast(U64, (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

Time os_get_wall_time () {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm local;
    localtime_r(&ts.tv_sec, &local);

    return (Time){
        .hours    = cast(U32, local.tm_hour),
        .minutes  = cast(U32, local.tm_min),
        .seconds  = cast(U32, local.tm_sec),
        .mseconds = cast(U32, ts.tv_nsec / 1000000),
    };
}
