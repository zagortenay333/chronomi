#pragma once

#include "util/mem.h"
#include "util/core.h"
#include "util/string.h"
#include "os/time.h"

struct Alarm {
    Minutes time;
    Minutes snooze;
    String message;
    String sound;
    Bool enabled;
    Bool playing;
    U8 days; // bitflags; least sig bit = Sunday
};

struct SnoozedAlarm {
    Alarm *alarm;
    Minutes time;
};

struct AlarmApplet {
    Mem *mem;
    Minutes time;
    Array<Alarm*> alarms;
    Array<SnoozedAlarm> snoozed_alarms;
};

AlarmApplet *alarm_new      (Mem *);
Bool         alarm_tic      (AlarmApplet *);
Alarm       *alarm_add      (AlarmApplet *, Alarm *);
Void         alarm_del      (AlarmApplet *, Alarm *);
Void         alarm_toggle   (AlarmApplet *, Alarm *);
Void         alarm_snooze   (AlarmApplet *, Alarm *);
Void         alarm_unsnooze (AlarmApplet *, Alarm *);
