#include <stdio.h>
#include "ui/main.h"
#include "util/core.h"
#include "util/mem.h"
#include "util/log.h"

#include "core/config.h"
#include "os/fs.h"
#include "os/time.h"
#include "core/markup.h"

Int main (Int argc, Char **argv) {
    tmem_setup(&mem_root, 1*MB);
    log_setup(&mem_root, 4*KB);
    return ui_main(argc, argv);
}
