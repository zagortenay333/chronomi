#include "core/pomodoro.h"
#include "os/time.h"

PomodoroApplet *pomodoro_new (Mem *mem) {
    Auto applet = mem_new(mem, PomodoroApplet);
    applet->mem = mem;
    array_init(&applet->pomodoros, mem);
    array_init(&applet->phase_changes, mem);
    return applet;
}

// This function updates the remaining time of each
// running pomodoro.
//
// Any pomodoro that changed phase (timer ran out)
// will be moved to the start of applet->pomodoros,
// as well as added into applet->phase_changes.
//
// Returns whether there are any running pomodoros.
Bool pomodoro_tic (PomodoroApplet *applet) {
    Bool running = false;
    applet->phase_changes.count = 0;

    array_iter (pomo, &applet->pomodoros) {
        if (! pomo->running) continue;

        running = true;
        U64 now = os_monotonic_time_ms();
        pomo->remaining = sat_sub64(pomo->remaining, now - pomo->start);
        pomo->start = now;

        if (pomo->remaining == 0) {
            if (pomo->phase != POMO_PHASE_WORK) {
                pomo->phase = POMO_PHASE_WORK;
                pomo->remaining = pomo->work_length;
            } else if (pomo->pomos_until_long_break == 1) {
                pomo->n_completed_pomos++;
                pomo->phase = POMO_PHASE_LONG_BREAK;
                pomo->remaining = pomo->long_break_length;
                pomo->pomos_until_long_break = pomo->long_break_every_n_pomos;
            } else {
                pomo->n_completed_pomos++;
                pomo->phase = POMO_PHASE_SHORT_BREAK;
                pomo->remaining = pomo->short_break_length;
                pomo->pomos_until_long_break--;
            }

            array_push(&applet->phase_changes, pomo);
            array_remove(&applet->pomodoros, ARRAY_IDX--);
        }
    }

    array_insert_many(&applet->pomodoros, applet->phase_changes, 0);
    return running;
}

Pomodoro *pomodoro_add (PomodoroApplet *applet, PomodoroPhase phase, String sound, Millisec work_length, Millisec short_break_length, Millisec long_break_length, U64 clock_size, String msg, U64 n_completed_pomos, U64 pomos_until_long_break, U64 long_break_every_n_pomos) {
    Auto pomo                      = mem_new(applet->mem, Pomodoro);
    pomo->sound_file               = str_copy(applet->mem, sound);
    pomo->message                  = str_copy(applet->mem, msg);
    pomo->work_length              = work_length;
    pomo->short_break_length       = short_break_length;
    pomo->long_break_length        = long_break_length;
    pomo->clock_size               = clock_size;
    pomo->n_completed_pomos        = n_completed_pomos;
    pomo->pomos_until_long_break   = pomos_until_long_break;
    pomo->long_break_every_n_pomos = long_break_every_n_pomos;
    array_push(&applet->pomodoros, pomo);
    pomodoro_set_phase(applet, pomo, phase);
    return pomo;
}

Void pomodoro_del (PomodoroApplet *applet, Pomodoro *pomo) {
    array_find_remove(&applet->pomodoros, [&](Pomodoro *it){ return it == pomo; });
    array_maybe_decrease_capacity(&applet->pomodoros);
    mem_free(applet->mem, .old_ptr=pomo->message.data, .old_size=pomo->message.count);
    mem_free(applet->mem, .old_ptr=pomo->sound_file.data, .old_size=pomo->sound_file.count);
    mem_free(applet->mem, .old_ptr=pomo, .old_size=sizeof(Pomodoro));
}

Void pomodoro_start (PomodoroApplet *applet, Pomodoro *pomo) {
    pomo->running = true;
    pomo->start = os_monotonic_time_ms();
}

Void pomodoro_pause (PomodoroApplet *applet, Pomodoro *pomo) {
    pomo->running = false;
}

Void pomodoro_set_phase (PomodoroApplet *applet, Pomodoro *pomo, PomodoroPhase phase) {
    pomo->phase = phase;

    switch (phase) {
    case POMO_PHASE_WORK:        pomo->remaining = pomo->work_length; break;
    case POMO_PHASE_SHORT_BREAK: pomo->remaining = pomo->short_break_length; break;
    case POMO_PHASE_LONG_BREAK:  pomo->remaining = pomo->long_break_length; break;
    }
}
