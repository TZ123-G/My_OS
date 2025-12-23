// kernel/bio.c
#include "types.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "printf.h"

// Simple in-memory disk simulation and minimal buffer cache.
#define NBLOCKS 1024
static uchar disk[NBLOCKS * BSIZE];

static struct buf bufs[16];
static int g_cache_hits = 0;
static int g_cache_misses = 0;
static int g_disk_reads = 0;
static int g_disk_writes = 0;

void binit(void)
{
    for (int i = 0; i < (int)(sizeof(bufs) / sizeof(bufs[0])); i++)
    {
        bufs[i].valid = 0;
        bufs[i].disk = 0;
        bufs[i].dev = 0;
        bufs[i].blockno = 0;
        bufs[i].refcnt = 0;
        initlock(&bufs[i].lock, "buf");
    }
}

static struct buf *findbuf(uint dev, uint blockno)
{
    for (int i = 0; i < (int)(sizeof(bufs) / sizeof(bufs[0])); i++)
    {
        if (bufs[i].refcnt > 0 && bufs[i].dev == dev && bufs[i].blockno == blockno)
            return &bufs[i];
    }
    return 0;
}

struct buf *bread(uint dev, uint blockno)
{
    if (blockno >= NBLOCKS)
    {
        panic("bread: blockno out of range");
    }
    struct buf *b = findbuf(dev, blockno);
    if (b)
    {
        g_cache_hits++;
        b->refcnt++;
        return b;
    }
    // allocate free buf
    for (int i = 0; i < (int)(sizeof(bufs) / sizeof(bufs[0])); i++)
    {
        if (bufs[i].refcnt == 0)
        {
            b = &bufs[i];
            b->dev = dev;
            b->blockno = blockno;
            b->refcnt = 1;
            b->valid = 1;
            b->disk = 0;
            // copy from disk
            memmove(b->data, &disk[blockno * BSIZE], BSIZE);
            g_cache_misses++;
            g_disk_reads++;
            return b;
        }
    }
    panic("bread: no free buffers");
    return 0;
}

void bwrite(struct buf *b)
{
    if (!b || !b->valid)
        return;
    // write to simulated disk
    if (b->blockno >= NBLOCKS)
        panic("bwrite: out of range");
    memmove(&disk[b->blockno * BSIZE], b->data, BSIZE);
    b->disk = 0;
    g_disk_writes++;
}

void brelse(struct buf *b)
{
    if (!b)
        return;
    if (b->refcnt <= 0)
        panic("brelse: refcnt");
    b->refcnt--;
    // if no refs, keep buffer valid
}

int buffer_cache_hits(void)
{
    return g_cache_hits;
}

int buffer_cache_misses(void)
{
    return g_cache_misses;
}

int disk_read_count(void)
{
    return g_disk_reads;
}

int disk_write_count(void)
{
    return g_disk_writes;
}
