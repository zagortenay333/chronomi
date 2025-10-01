#pragma once

#include "util/core.h"
#include "util/mem.h"
#include "util/array.h"
#include "util/string.h"
#include "os/time.h"

enum PomodoroPhase {
    POMO_PHASE_WORK,
    POMO_PHASE_SHORT_BREAK,
    POMO_PHASE_LONG_BREAK,
};

struct Pomodoro {
    U64 clock_size;
    String message;
    String sound_file;
    Millisec work_length;
    Millisec short_break_length;
    Millisec long_break_length;
    U64 long_break_every_n_pomos;
    U64 n_completed_pomos;
    U64 pomos_until_long_break;
    PomodoroPhase phase;
    Bool running;
    Millisec start;
    Millisec remaining;
};

struct PomodoroApplet {
    Mem *mem;
    Array<Pomodoro*> pomodoros;
    Array<Pomodoro*> phase_changes;
};

PomodoroApplet *pomodoro_new       (Mem *);
Bool            pomodoro_tic       (PomodoroApplet *);
Pomodoro       *pomodoro_add       (PomodoroApplet *, PomodoroPhase, String, Millisec, Millisec, Millisec, U64, String, U64, U64, U64); // @todo Shorten this signature...
Void            pomodoro_del       (PomodoroApplet *, Pomodoro *);
Void            pomodoro_start     (PomodoroApplet *, Pomodoro *);
Void            pomodoro_pause     (PomodoroApplet *, Pomodoro *);
Void            pomodoro_set_phase (PomodoroApplet *, Pomodoro *, PomodoroPhase);
