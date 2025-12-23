// kernel/fs.h
#ifndef FS_H
#define FS_H

#include "types.h"
#include "spinlock.h"

#define BSIZE 4096   // block size
#define NBLOCKS 1024 // total blocks in simulated disk (bio.c)
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// on-disk inode
struct dinode
{
    short type;
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT + 1]; // last one is indirect
};

// superblock
struct superblock
{
    uint magic;
    uint size; // blocks
    uint nblocks;
    uint ninodes;
    uint nlog;
    uint logstart;
    uint inodestart;
    uint bmapstart;
};

extern struct superblock sb;

// in-memory inode
struct inode
{
    uint dev;
    uint inum;
    int ref;
    struct spinlock lock;
    int valid;
    short type;
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT + 1];
};

// buffer structure
struct buf
{
    int valid;
    int disk; // dirty
    uint dev;
    uint blockno;
    int refcnt;
    struct spinlock lock;
    uchar data[BSIZE];
};

// on-disk dirent
#define DIRSIZ 14
struct dirent
{
    ushort inum;
    char name[DIRSIZ];
};

void fs_init(void);
void binit(void);
struct buf *bread(uint dev, uint blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);

// namei/create for simple pathname handling
struct inode *namei(const char *path);
struct inode *create(const char *path, short type);

// debug helpers
void read_superblock(struct superblock *out);
int count_free_blocks(void);
int count_free_inodes(void);
int buffer_cache_hits(void);
int buffer_cache_misses(void);
int disk_read_count(void);
int disk_write_count(void);

struct inode *iget(uint dev, uint inum);
void iinit(void);
struct inode *ialloc(uint dev, short type);
void iupdate(struct inode *ip);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
void iput(struct inode *ip);
int readi(struct inode *ip, char *dst, uint off, uint n);
int writei(struct inode *ip, char *src, uint off, uint n);

// file layer
void fileinit(void);
struct file *filealloc(void);

// log
void log_init(void);

// inode table inspection helpers (for tests)
int fs_inode_count(void);
struct inode *fs_inode_at(int idx);

#endif
