// kernel/fs.c
#include "types.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "printf.h"

#define ROOTINO 1
#define IPB (BSIZE / sizeof(struct dinode))
#define IBLOCK(i, sb) ((sb).inodestart + (i) / IPB)

static struct superblock sb;
static struct inode inodes[200];

void iinit(void)
{
    // initialize simple superblock layout on our simulated disk
    sb.magic = 0x10203040;
    sb.size = 1024;            // NBLOCKS in bio.c
    sb.nblocks = sb.size - 10; // simple
    sb.ninodes = 200;
    sb.nlog = 0;
    sb.logstart = 2;
    sb.inodestart = 10;
    sb.bmapstart = sb.inodestart + (sb.ninodes / IPB) + 1;
    // init in-memory inode table
    for (int i = 0; i < (int)(sizeof(inodes) / sizeof(inodes[0])); i++)
    {
        inodes[i].dev = 0;
        inodes[i].inum = i;
        inodes[i].ref = 0;
        initlock(&inodes[i].lock, "inode");
        inodes[i].valid = 0;
    }
}

struct inode *iget(uint dev, uint inum)
{
    if (inum >= sb.ninodes)
        return 0;
    struct inode *ip = &inodes[inum];
    ip->ref++;
    return ip;
}

void iput(struct inode *ip)
{
    if (!ip)
        return;
    if (ip->ref <= 0)
        panic("iput: ref<=0");
    ip->ref--;
}

struct inode *ialloc(uint dev, short type)
{
    for (uint inum = 1; inum < sb.ninodes; inum++)
    {
        struct inode *ip = &inodes[inum];
        if (!ip->valid)
        {
            ip->valid = 1;
            ip->type = type;
            ip->nlink = 1;
            ip->size = 0;
            for (int i = 0; i < NDIRECT + 1; i++)
                ip->addrs[i] = 0;
            ip->ref = 1;
            // logging removed for lab7
            return ip;
        }
    }
    // logging removed for lab7
    return 0;
}

void ilock(struct inode *ip)
{
    acquire(&ip->lock);
}

void iunlock(struct inode *ip)
{
    release(&ip->lock);
}

// map logical block to physical block: naive allocation on write
static uint bmap(struct inode *ip, uint bn)
{
    if (bn < NDIRECT)
    {
        if (ip->addrs[bn] == 0)
        {
            // allocate a block: choose first free block after bmapstart
            for (uint b = sb.bmapstart + 1; b < sb.size; b++)
            {
                // very simple free test: scan disk block content for zero
                struct buf *bb = bread(0, b);
                int allzero = 1;
                for (int i = 0; i < BSIZE; i++)
                    if (bb->data[i])
                    {
                        allzero = 0;
                        break;
                    }
                if (allzero)
                {
                    ip->addrs[bn] = b;
                    if (ip->addrs[bn] >= NBLOCKS)
                    {
                        printf("bmap direct OOR: bn=%d b=%d NBLOCKS=%d\n", (int)bn, (int)ip->addrs[bn], (int)NBLOCKS);
                        brelse(bb);
                        panic("bmap: assigned block out of range");
                    }
                    brelse(bb);
                    return b;
                }
                brelse(bb);
            }
            panic("bmap: out of blocks");
        }
        return ip->addrs[bn];
    }
    // indirect
    bn -= NDIRECT;
    if (ip->addrs[NDIRECT] == 0)
    {
        // allocate indirect block
        for (uint b = sb.bmapstart + 1; b < sb.size; b++)
        {
            struct buf *bb = bread(0, b);
            int allzero = 1;
            for (int i = 0; i < BSIZE; i++)
                if (bb->data[i])
                {
                    allzero = 0;
                    break;
                }
            if (allzero)
            {
                ip->addrs[NDIRECT] = b;
                if (ip->addrs[NDIRECT] >= NBLOCKS)
                {
                    printf("bmap indir tbl OOR: b=%d NBLOCKS=%d\n", (int)ip->addrs[NDIRECT], (int)NBLOCKS);
                    brelse(bb);
                    panic("bmap: indirect table out of range");
                }
                brelse(bb);
                break;
            }
            brelse(bb);
        }
        if (ip->addrs[NDIRECT] == 0)
            panic("bmap: out of blocks for indirect");
    }
    struct buf *ib = bread(0, ip->addrs[NDIRECT]);
    uint *a = (uint *)ib->data;
    if (a[bn] == 0)
    {
        for (uint b = sb.bmapstart + 1; b < sb.size; b++)
        {
            struct buf *bb = bread(0, b);
            int allzero = 1;
            for (int i = 0; i < BSIZE; i++)
                if (bb->data[i])
                {
                    allzero = 0;
                    break;
                }
            if (allzero)
            {
                // 避免将当前 inode 的间接表块再次当作数据块分配，导致表被文件数据覆盖
                if (b == ip->addrs[NDIRECT])
                {
                    // logging removed for lab7: skipping indirect table block
                    brelse(bb);
                    continue;
                }
                a[bn] = b;
                bwrite(ib);
                if (a[bn] >= NBLOCKS)
                {
                    printf("bmap indirect OOR: bn=%d b=%d NBLOCKS=%d\n", (int)bn, (int)a[bn], (int)NBLOCKS);
                    brelse(bb);
                    brelse(ib);
                    panic("bmap: assigned indirect block out of range");
                }
                brelse(bb);
                brelse(ib);
                return a[bn];
            }
            brelse(bb);
        }
        panic("bmap: out of blocks indirect2");
    }
    brelse(ib);
    return a[bn];
}

int readi(struct inode *ip, char *dst, uint off, uint n)
{
    if (off > ip->size)
        return 0;
    if (off + n > ip->size)
        n = ip->size - off;
    uint tot = 0;
    while (tot < n)
    {
        uint bn = off / BSIZE;
        uint boff = off % BSIZE;
        uint toread = BSIZE - boff;
        if (toread > n - tot)
            toread = n - tot;
        uint bnum = 0;
        if (bn < NDIRECT)
            bnum = ip->addrs[bn];
        else
        {
            struct buf *ib = bread(0, ip->addrs[NDIRECT]);
            uint *a = (uint *)ib->data;
            bnum = a[bn - NDIRECT];
            brelse(ib);
        }
        if (bnum == 0)
        {
            // hole
            for (uint i = 0; i < toread; i++)
                dst[tot + i] = 0;
        }
        else
        {
            struct buf *b = bread(0, bnum);
            memmove(dst + tot, b->data + boff, toread);
            brelse(b);
        }
        tot += toread;
        off += toread;
    }
    return tot;
}

int writei(struct inode *ip, char *src, uint off, uint n)
{
    if (off > ip->size)
        return -1;
    uint tot = 0;
    while (tot < n)
    {
        uint bn = off / BSIZE;
        uint boff = off % BSIZE;
        uint towrite = BSIZE - boff;
        if (towrite > n - tot)
            towrite = n - tot;
        uint bnum = bmap(ip, bn);
        struct buf *b = bread(0, bnum);
        memmove(b->data + boff, src + tot, towrite);
        bwrite(b);
        brelse(b);
        tot += towrite;
        off += towrite;
    }
    if (off > ip->size)
        ip->size = off;
    return tot;
}

void fs_init(void)
{
    binit();
    iinit();
    // create root inode if necessary
    struct inode *r = &inodes[ROOTINO];
    if (!r->valid)
    {
        r->valid = 1;
        r->type = 1; // dir
        r->nlink = 1;
        r->size = 0;
        r->ref = 1;
    }
}

// ---- debug helpers ----
void read_superblock(struct superblock *out)
{
    if (out)
        *out = sb;
}

int count_free_inodes(void)
{
    int freec = 0;
    for (int i = 0; i < (int)(sizeof(inodes) / sizeof(inodes[0])); i++)
    {
        if (!inodes[i].valid)
            freec++;
    }
    return freec;
}

int count_free_blocks(void)
{
    // scan blocks after bmapstart to end; consider a block free if all zero
    int freeb = 0;
    for (uint b = sb.bmapstart + 1; b < sb.size; b++)
    {
        struct buf *bb = bread(0, b);
        int allzero = 1;
        for (int i = 0; i < BSIZE; i++)
        {
            if (bb->data[i])
            {
                allzero = 0;
                break;
            }
        }
        if (allzero)
            freeb++;
        brelse(bb);
    }
    return freeb;
}

// ---- inode table inspection helpers ----
int fs_inode_count(void)
{
    return (int)(sizeof(inodes) / sizeof(inodes[0]));
}

struct inode *fs_inode_at(int idx)
{
    if (idx < 0 || idx >= (int)(sizeof(inodes) / sizeof(inodes[0])))
        return 0;
    return &inodes[idx];
}

// ------- simple pathname helpers (minimal) -------
// dir lookup: search directory inode dp for name, return iget of target inode
static struct inode *dirlookup(struct inode *dp, const char *name)
{
    if (!dp || !name)
        return 0;
    // read directory entries sequentially
    int off = 0;
    while (off + sizeof(struct dirent) <= (int)dp->size)
    {
        struct dirent de;
        int r = readi(dp, (char *)&de, off, sizeof(de));
        if (r != sizeof(de))
            break;
        if (de.inum != 0 && strcmp(de.name, name) == 0)
        {
            return iget(0, de.inum);
        }
        off += sizeof(de);
    }
    return 0;
}

// link a name in directory dp to inum
static int dirlink(struct inode *dp, const char *name, uint inum)
{
    if (!dp || !name)
        return -1;
    // do not allow duplicate
    if (dirlookup(dp, name))
        return -1;
    struct dirent de;
    de.inum = (ushort)inum;
    // copy name with DIRSIZ limit
    int i;
    for (i = 0; i < DIRSIZ && name[i]; i++)
        de.name[i] = name[i];
    for (; i < DIRSIZ; i++)
        de.name[i] = '\0';
    // append dirent
    int off = dp->size;
    writei(dp, (char *)&de, off, sizeof(de));
    return 0;
}

// parse simple path: only supports absolute paths like "/name" or "/dir/name"
struct inode *namei(const char *path)
{
    if (!path || path[0] != '/')
        return 0;
    // root
    struct inode *cur = iget(0, ROOTINO);
    if (!cur)
        return 0;
    // skip leading '/'
    const char *p = path + 1;
    if (*p == '\0')
        return cur;
    char name[DIRSIZ + 1];
    while (1)
    {
        int i = 0;
        while (*p && *p != '/' && i < DIRSIZ)
            name[i++] = *p++;
        name[i] = '\0';
        struct inode *next = dirlookup(cur, name);
        iput(cur);
        if (!next)
            return 0;
        cur = next;
        if (*p == '/')
            p++;
        if (*p == '\0')
            break;
    }
    return cur;
}

struct inode *create(const char *path, short type)
{
    if (!path || path[0] != '/')
        return 0;
    // find parent directory and final name
    const char *p = path + 1;
    char name[DIRSIZ + 1];
    struct inode *parent = iget(0, ROOTINO);
    if (!parent)
        return 0;
    // if path is just "/name"
    const char *slash = 0;
    const char *q = p;
    while (*q && *q != '/')
        q++;
    slash = (*q == '/') ? q : 0;
    int nlen = (slash ? (int)(slash - p) : (int)strlen(p));
    if (nlen <= 0 || nlen > DIRSIZ)
    {
        iput(parent);
        return 0;
    }
    for (int i = 0; i < nlen; i++)
        name[i] = p[i];
    name[nlen] = '\0';

    // check exists
    if (dirlookup(parent, name))
    {
        iput(parent);
        return 0; // already exists
    }

    struct inode *ip = ialloc(0, type);
    if (!ip)
    {
        iput(parent);
        return 0;
    }
    // link in parent
    dirlink(parent, name, ip->inum);
    iput(parent);
    return ip;
}
