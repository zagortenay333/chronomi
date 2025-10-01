#include "core/alarm.h"
#include "os/time.h"

AlarmApplet *alarm_new (Mem *mem) {
    Auto applet = mem_new(mem, AlarmApplet);
    applet->mem = mem;
    applet->time = os_get_time_in_minutes();
    array_init(&applet->alarms, mem);
    array_init(&applet->snoozed_alarms, mem);
    return applet;
}

// This function checks whether any alarms went off at this point.
// For each alarm that went off it sets it's alarm->playing to true.
// It returns true if any alarm went from not playing to playing.
Bool alarm_tic (AlarmApplet *applet) {
    DateTime dt   = os_get_date_time();
    U8 day_flag   = (1 << dt.wday);
    Minutes now   = dt.hour * 60 + dt.min;
    bool went_off = false;

    if (now == applet->time) {
        return false; // Only tic once per minute.
    } else {
        applet->time = now;
    }

    array_iter (alarm, &applet->alarms) {
        if ((alarm->time == now) && alarm->enabled && (alarm->days & day_flag) && !alarm->playing) {
            alarm->playing = true;
            went_off = true;
        }
    }

    array_iter_ptr (alarm, &applet->snoozed_alarms) {
        if (alarm->time == now) {
            alarm->alarm->playing = true;
            array_remove_fast(&applet->snoozed_alarms, ARRAY_IDX--);
            went_off = true;
        }
    }

    return went_off;
}

Alarm *alarm_add (AlarmApplet *applet, Alarm *input) {
    Auto alarm = mem_new(applet->mem, Alarm);
    alarm->time = input->time;
    alarm->message = str_copy(applet->mem, input->message);
    alarm->sound = str_copy(applet->mem, input->sound);
    alarm->enabled = input->enabled;
    alarm->snooze = input->snooze;
    alarm->playing = input->playing;
    alarm->days = input->days;
    U64 idx = array_find(&applet->alarms, [&](Alarm *it){ return it->time > alarm->time; });
    if (idx == ARRAY_NIL_IDX) idx = applet->alarms.count;
    array_insert(&applet->alarms, alarm, idx);
    return alarm;
}

Void alarm_del (AlarmApplet *applet, Alarm *alarm) {
    alarm_unsnooze(applet, alarm);
    array_find_remove(&applet->alarms, [&](Alarm *it){ return it == alarm; });
    array_maybe_decrease_capacity(&applet->alarms);
    mem_free(applet->mem, .old_ptr=alarm->message.data, .old_size=alarm->message.count);
    mem_free(applet->mem, .old_ptr=alarm->sound.data, .old_size=alarm->sound.count);
    mem_free(applet->mem, .old_ptr=alarm, .old_size=sizeof(Alarm));
}

Void alarm_snooze (AlarmApplet *applet, Alarm *alarm) {
    alarm->playing = false;
    alarm_unsnooze(applet, alarm);
    Minutes now = os_get_time_in_minutes();
    Minutes then = (now + alarm->snooze) % (24 * 60);
    array_push_lit(&applet->snoozed_alarms, .alarm=alarm, .time=then);
}

Void alarm_toggle (AlarmApplet *applet, Alarm *alarm) {
    alarm->enabled = !alarm->enabled;
    alarm_unsnooze(applet, alarm);
}

Void alarm_unsnooze (AlarmApplet *applet, Alarm *alarm) {
    array_find_remove_fast(&applet->snoozed_alarms, [&](SnoozedAlarm it){ return it.alarm == alarm; });
}
