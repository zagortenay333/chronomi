#pragma once

#include "util/core.h"
#include "util/mem.h"
#include "util/array.h"
#include "util/string.h"
#include "os/time.h"

enum StopwatchState {
    STOPWATCH_RESET,
    STOPWATCH_PAUSED,
    STOPWATCH_RUNNING,
};

struct Lap {
    Millisec lap;
    Millisec total;
};

struct Stopwatch {
    String message;
    I64 clock_size;
    Millisec lap;
    Millisec current;
    Millisec monotonic;
    StopwatchState state;
    Array<Lap> laps;
};

struct StopwatchApplet {
    Mem *mem;
    Array<Stopwatch*> stopwatches;
};

StopwatchApplet *stopwatch_new   (Mem *);
Void             stopwatch_tic   (StopwatchApplet *);
Void             stopwatch_add   (StopwatchApplet *, I64, String);
Void             stopwatch_edit  (StopwatchApplet *, Stopwatch *, I64, String);
Void             stopwatch_del   (StopwatchApplet *, Stopwatch *);
Void             stopwatch_start (StopwatchApplet *, Stopwatch *);
Void             stopwatch_pause (StopwatchApplet *, Stopwatch *);
Void             stopwatch_reset (StopwatchApplet *, Stopwatch *);
Void             stopwatch_lap   (StopwatchApplet *, Stopwatch *);
