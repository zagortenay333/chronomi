#pragma once

#include "util/core.h"

typedef U64 Millisec;
typedef U64 Minutes;

enum Day {
    SUN,
    MON,
    TUE,
    WED,
    THU,
    FRI,
    SAT,
};

struct DateTime {
    U8 sec;   // [0, 60]
    U8 min;   // [0, 59]
    U8 hour;  // [0, 23]
    U8 day;   // [1, 31]
    U8 month; // [1, 12]
    Day wday; // [0, 6] (Sunday = 0)
    Int year;
};

Void     os_sleep_ms            (U64 msec);
DateTime os_get_date_time       ();
U64      os_monotonic_time_ms   ();
Minutes  os_get_time_in_minutes ();
