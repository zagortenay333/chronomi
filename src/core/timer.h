#pragma once

#include "util/mem.h"
#include "util/core.h"
#include "util/string.h"
#include "os/time.h"

enum TimerState {
    TIMER_RESET,
    TIMER_PAUSED,
    TIMER_RUNNING,
    TIMER_NOTIF,
};

struct Timer {
    Millisec preset;
    String sound;
    String message;
    I64 clock_size;
    Millisec start;
    Millisec remaining;
    TimerState state;
};

struct TimerApplet {
    Mem *mem;
    Array<Timer*> timers;
};

TimerApplet *timer_new    (Mem *);
Bool         timer_tic    (TimerApplet *);
Timer       *timer_add    (TimerApplet *, Millisec time, String sound, I64 clock_size, String msg);
Void         timer_del    (TimerApplet *, Timer *);
Void         timer_start  (TimerApplet *, Timer *);
Void         timer_pause  (TimerApplet *, Timer *);
Void         timer_reset  (TimerApplet *, Timer *);
