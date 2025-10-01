#include "core/timer.h"
#include "os/time.h"

TimerApplet *timer_new (Mem *mem) {
    Auto applet = mem_new(mem, TimerApplet);
    applet->mem = mem;
    array_init(&applet->timers, mem);
    return applet;
}

Bool timer_tic (TimerApplet *applet) {
    tmem_new(tm);
    Auto expired = array_new<Timer*>(tm);

    array_iter (timer, &applet->timers) {
        if (timer->state != TIMER_RUNNING) continue;
        U64 now = os_monotonic_time_ms();
        timer->remaining = sat_sub64(timer->remaining, now - timer->start);
        timer->start = now;
        if (timer->remaining == 0) {
            timer->state = TIMER_NOTIF;
            array_push(&expired, timer);
            array_remove(&applet->timers, ARRAY_IDX--);
        }
    }

    array_insert_many(&applet->timers, expired, 0);
    return expired.count;
}

Timer *timer_add (TimerApplet *applet, U64 preset, String sound, I64 clock_size, String msg) {
    Auto timer        = mem_new(applet->mem, Timer);
    timer->preset     = preset;
    timer->clock_size = clock_size;
    timer->sound      = str_copy(applet->mem, sound);
    timer->message    = str_copy(applet->mem, msg);
    array_push(&applet->timers, timer);
    timer_reset(applet, timer);
    return timer;
}

Void timer_del (TimerApplet *applet, Timer *timer) {
    array_find_remove(&applet->timers, [&](Timer *it){ return it == timer; });
    array_maybe_decrease_capacity(&applet->timers);
    mem_free(applet->mem, .old_ptr=timer->message.data, .old_size=timer->message.count);
    mem_free(applet->mem, .old_ptr=timer->sound.data, .old_size=timer->sound.count);
    mem_free(applet->mem, .old_ptr=timer, .old_size=sizeof(Timer));
}

Void timer_start (TimerApplet *applet, Timer *timer) {
    timer->state = TIMER_RUNNING;
    timer->start = os_monotonic_time_ms();
}

Void timer_pause (TimerApplet *applet, Timer *timer) {
    timer->state = TIMER_PAUSED;
}

Void timer_reset (TimerApplet *applet, Timer *timer) {
    timer->state = TIMER_RESET;
    timer->remaining = timer->preset;
}
