// kernel/log.c
#include "types.h"
#include "defs.h"
#include "fs.h"

// Minimal stub logging to satisfy dependencies from task.md.
static int log_inited = 0;

void log_init(void)
{
    if (log_inited)
        return;
    log_inited = 1;
}

void begin_op(void) {}
void end_op(void) {}
void log_write(struct buf *b) {}
