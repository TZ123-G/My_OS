// kernel/spinlock.c
#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "printf.h"

// 获取当前CPU指针
struct cpu *
mycpu(void)
{
    // 返回在 proc.c 中定义的全局 cpu 结构（单核实现）
    extern struct cpu cpu;
    return &cpu;
}

// 初始化锁
void initlock(struct spinlock *lk, char *name)
{
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
}

// 推送中断状态（禁用中断并记录之前的状态）
void push_off(void)
{
    int old = intr_get();

    intr_off();
    struct cpu *c = mycpu();
    if (c->noff == 0)
        c->intena = old;
    c->noff += 1;
}

// 弹出中断状态（恢复之前的状态）
void pop_off(void)
{
    struct cpu *c = mycpu();
    if (intr_get())
        panic("pop_off - interruptible");
    if (c->noff < 1)
        panic("pop_off");
    c->noff -= 1;
    if (c->noff == 0 && c->intena)
        intr_on();
}

// 获取锁
void acquire(struct spinlock *lk)
{
    push_off(); // 在单核上禁用中断以避免死锁

    if (holding(lk))
        panic("acquire");

    // 在RISC-V上，sync_lock_test_and_set会发出acq内存序
    while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
        ;

    // 告诉C编译器和处理器不要在锁周围移动加载或存储
    __sync_synchronize();

    // 记录关于此锁的信息
    lk->cpu = mycpu();
}

// 释放锁
void release(struct spinlock *lk)
{
    if (!holding(lk))
        panic("release");

    lk->cpu = 0;

    // 告诉C编译器和处理器不要在锁周围移动加载或存储
    __sync_synchronize();

    // 在RISC-V上，sync_lock_release会发出rel内存序
    __sync_lock_release(&lk->locked);

    pop_off();
}

// 检查当前CPU是否持有锁
int holding(struct spinlock *lk)
{
    int r;
    push_off();
    r = (lk->locked && lk->cpu == mycpu());
    pop_off();
    return r;
}
