// kernel/file.c
#include "types.h"
#include "defs.h"
#include "fs.h"
#include "printf.h" // for panic prototype if needed
#include "log.h"

struct file
{
    int ref;
    int readable;
    int writable;
    uint off;
    struct inode *ip;
};

static struct file filetable[50];

void fileinit(void)
{
    for (int i = 0; i < (int)(sizeof(filetable) / sizeof(filetable[0])); i++)
    {
        filetable[i].ref = 0;
        filetable[i].ip = 0;
    }
}

struct file *filealloc(void)
{
    for (int i = 0; i < (int)(sizeof(filetable) / sizeof(filetable[0])); i++)
    {
        if (filetable[i].ref == 0)
        {
            filetable[i].ref = 1;
            filetable[i].off = 0;
            filetable[i].ip = 0;
            return &filetable[i];
        }
    }
    return 0;
}

void fileclose(struct file *f)
{
    if (f == 0)
        return;
    if (f->ref <= 0)
    {
        klog(LOG_LEVEL_ERROR, "fileclose: invalid ref=%d", f->ref);
        panic("fileclose: ref<=0");
    }
    f->ref--;
    if (f->ref == 0 && f->ip)
    {
        klog(LOG_LEVEL_INFO, "fileclose: inum=%d", (int)f->ip->inum);
        iput(f->ip);
        f->ip = 0;
    }
}

int fileread(struct file *f, char *addr, int n)
{
    if (!f || !f->readable)
        return -1;
    int r = readi(f->ip, addr, f->off, n);
    klog(LOG_LEVEL_DEBUG, "fileread inum=%d off=%d n=%d r=%d", (int)f->ip->inum, (int)f->off, n, r);
    if (r > 0)
        f->off += r;
    return r;
}

int filewrite(struct file *f, char *addr, int n)
{
    if (!f || !f->writable)
        return -1;
    int w = writei(f->ip, addr, f->off, n);
    klog(LOG_LEVEL_DEBUG, "filewrite inum=%d off=%d n=%d w=%d", (int)f->ip->inum, (int)f->off, n, w);
    if (w > 0)
        f->off += w;
    return w;
}
