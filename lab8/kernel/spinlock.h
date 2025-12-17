// kernel/spinlock.h
#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

#include "types.h"

// 自旋锁结构
struct spinlock
{
    uint locked;     // 锁是否被持有
    char *name;      // 锁名称（用于调试）
    struct cpu *cpu; // 持有锁的CPU
};

// 函数声明
void initlock(struct spinlock *lk, char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
void push_off(void);
void pop_off(void);
int holding(struct spinlock *lk);

#endif // _SPINLOCK_H_