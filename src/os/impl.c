#include "base/core.h"

#if OS_LINUX
    #include "os/linux/fs.c"
    #include "os/linux/time.c"
    #include "os/linux/info.c"
    #include "os/linux/threads.c"
#else
    #error "Bad os."
#endif

Time os_ms_to_time (Millisec ms) {
    return (Time){
        .hours    = ms / 3600000,
        .minutes  = (ms / 60000) % 60,
        .seconds  = (ms / 1000) % 60,
        .mseconds = ms % 1000,
    };
}

Millisec os_time_to_ms (Time time) {
    return time.hours*3600000 +
           time.minutes*60000 +
           time.seconds*1000 +
           time.mseconds;
}
