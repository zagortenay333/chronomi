#include "core/stopwatch.h"
#include "os/time.h"

StopwatchApplet *stopwatch_new (Mem *mem) {
    Auto applet = mem_new(mem, StopwatchApplet);
    applet->mem = mem;
    array_init(&applet->stopwatches, mem);
    return applet;
}

Void stopwatch_tic (StopwatchApplet *applet) {
    array_iter (sw, &applet->stopwatches) {
        if (sw->state != STOPWATCH_RUNNING) continue;
        U64 now = os_monotonic_time_ms();
        sw->current += now - sw->monotonic;
        sw->lap += now - sw->monotonic;
        sw->monotonic = now;
    }
}

Void stopwatch_add (StopwatchApplet *applet, I64 clock_size, String msg) {
    Auto sw = mem_new(applet->mem, Stopwatch);
    sw->message = str_copy(applet->mem, msg);
    sw->clock_size = clock_size;
    array_init(&sw->laps, applet->mem);
    array_push(&applet->stopwatches, sw);
}

Void stopwatch_edit (StopwatchApplet *applet, Stopwatch *sw, I64 clock_size, String msg) {
    mem_free(applet->mem, .old_ptr=sw->message.data, .old_size=sw->message.count);
    sw->clock_size = clock_size;
    sw->message = str_copy(applet->mem, msg);
}

Void stopwatch_del (StopwatchApplet *applet, Stopwatch *sw) {
    array_find_remove(&applet->stopwatches, [&](Stopwatch *it){ return it == sw; });
    array_maybe_decrease_capacity(&applet->stopwatches);
    mem_free(applet->mem, .old_ptr=sw->message.data, .old_size=sw->message.count);
    array_free(&sw->laps);
    mem_free(applet->mem, .old_ptr=sw, .old_size=sizeof(Stopwatch));
}

Void stopwatch_start (StopwatchApplet *applet, Stopwatch *sw) {
    sw->state = STOPWATCH_RUNNING;
    sw->monotonic = os_monotonic_time_ms();
}

Void stopwatch_pause (StopwatchApplet *applet, Stopwatch *sw) {
    sw->state = STOPWATCH_PAUSED;
}

Void stopwatch_reset (StopwatchApplet *applet, Stopwatch *sw) {
    sw->state = STOPWATCH_RESET;
    sw->current = 0;
    sw->laps.count = 0;
}

Void stopwatch_lap (StopwatchApplet *applet, Stopwatch *sw) {
    array_insert(&sw->laps, Lap{ .lap=sw->lap, .total=sw->current }, 0);
    sw->lap = 0;
}
