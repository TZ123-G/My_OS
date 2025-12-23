// kernel/log.c
#include "types.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "printf.h"

#define MAXLOG 100

struct logheader
{
    int n;
    int blocknos[MAXLOG];
};

struct log
{
    struct spinlock lock;
    int start;                  /* log start block */
    int size;                   /* log size (blocks) */
    int outstanding;            /* number of outstanding file system operations */
    int committing;             /* in commit */
    struct buf *logbuf[MAXLOG]; /* registered bufs */
    struct logheader lh;
} log;

static int num_logged_blocks = 0;

void log_init(void)
{
    initlock(&log.lock, "log");
    log.start = sb.logstart;
    log.size = sb.nlog;
    log.outstanding = 0;
    log.committing = 0;
    num_logged_blocks = 0;
    /* recovery: if log header indicates committed entries, install them */
    if (log.size > 0)
    {
        struct buf *b = bread(0, log.start);
        struct logheader *h = (struct logheader *)b->data;
        if (h->n > 0)
        {
            /* install */
            for (int i = 0; i < h->n; i++)
            {
                struct buf *l = bread(0, log.start + 1 + i);
                struct buf *dst = bread(0, h->blocknos[i]);
                memmove(dst->data, l->data, BSIZE);
                bwrite(dst);
                brelse(dst);
                brelse(l);
            }
            /* clear log header */
            h->n = 0;
            bwrite(b);
        }
        brelse(b);
    }
}

static void write_log_blocks(void)
{
    /* write logged blocks to log area */
    for (int i = 0; i < num_logged_blocks; i++)
    {
        struct buf *b = bread(0, log.start + 1 + i);
        memmove(b->data, log.logbuf[i]->data, BSIZE);
        bwrite(b);
        brelse(b);
    }
}

static void write_log_header(int n)
{
    struct buf *b = bread(0, log.start);
    struct logheader *h = (struct logheader *)b->data;
    h->n = n;
    for (int i = 0; i < n; i++)
        h->blocknos[i] = log.logbuf[i]->blockno;
    bwrite(b);
    brelse(b);
}

static void install_from_log(void)
{
    struct buf *b;
    struct logheader lh;
    struct buf *head = bread(0, log.start);
    memmove(&lh, head->data, sizeof(lh));
    brelse(head);
    for (int i = 0; i < lh.n; i++)
    {
        struct buf *l = bread(0, log.start + 1 + i);
        struct buf *dst = bread(0, lh.blocknos[i]);
        memmove(dst->data, l->data, BSIZE);
        bwrite(dst);
        brelse(dst);
        brelse(l);
    }
}

static void clear_log(void)
{
    struct buf *b = bread(0, log.start);
    struct logheader *h = (struct logheader *)b->data;
    h->n = 0;
    bwrite(b);
    brelse(b);
    num_logged_blocks = 0;
}

void begin_op(void)
{
    acquire(&log.lock);
    while (1)
    {
        if (log.committing)
        {
            sleep(&log, &log.lock);
        }
        else
        {
            /* conservative reservation: assume at most 1 block per op in this simple FS */
            if (num_logged_blocks + 1 < log.size)
            {
                log.outstanding++;
                release(&log.lock);
                return;
            }
            sleep(&log, &log.lock);
        }
    }
}

void end_op(void)
{
    acquire(&log.lock);
    log.outstanding--;
    if (log.outstanding == 0)
    {
        /* commit */
        log.committing = 1;
        /* write log */
        write_log_blocks();
        write_log_header(num_logged_blocks);
        /* install */
        install_from_log();
        /* clear */
        clear_log();
        log.committing = 0;
        wakeup(&log);
    }
    release(&log.lock);
}

void log_write(struct buf *b)
{
    if (log.committing)
        panic("log: log_write during commit");
    /* deduplicate */
    for (int i = 0; i < num_logged_blocks; i++)
    {
        if (log.logbuf[i]->blockno == b->blockno)
            return;
    }
    if (num_logged_blocks >= MAXLOG)
        panic("log: too many log blocks");
    log.logbuf[num_logged_blocks++] = b;
}
