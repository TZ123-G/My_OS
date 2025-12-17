// kernel/proc.c - 单核简化版进程管理
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 单核系统只需要一个CPU结构
struct cpu cpu;

// 进程表
struct proc proc[NPROC];

// 当前运行的进程
struct proc *current_proc;

// 进程锁
struct spinlock proc_lock;

// 初始化进程系统
void procinit(void)
{
    initlock(&proc_lock, "proc_lock");
    // 初始化所有进程为UNUSED状态
    for (int i = 0; i < NPROC; i++)
    {
        proc[i].state = UNUSED;
    }
    return;
}

// 获取当前进程
struct proc *
myproc(void)
{
    push_off();
    struct proc *p = current_proc;
    pop_off();
    return p;
}

// 分配PID
int allocpid()
{
    static int next_pid = 1;
    int pid;

    acquire(&proc_lock);
    pid = next_pid++;
    release(&proc_lock);

    return pid;
}

// 释放进程资源
static void
freeproc(struct proc *p)
{
    // 简化实现，实际系统中需要释放更多资源
    p->state = UNUSED;
}

// 唤醒在chan上等待的所有进程
void wakeup(void *chan)
{
    // 在单核系统中，简单实现
    // 实际系统中需要遍历进程表找到等待在chan上的进程
    // 这里暂时为空实现
}

// 进程睡眠
void sleep(void *chan, struct spinlock *lk)
{
    // 简化实现
    // 实际系统中需要：
    // 1. 释放传入的锁lk
    // 2. 设置进程状态为SLEEPING并设置chan
    // 3. 调用调度器
    // 4. 被唤醒后重新获取锁lk
    // 这里暂时为空实现
}

// 让出CPU
void yield(void)
{
    // 简化实现
    // 实际系统中需要：
    // 1. 设置进程状态为RUNNABLE
    // 2. 调用调度器
    // 这里暂时为空实现
}

// 杀死指定PID的进程
int kill(int pid)
{
    // 简化实现
    return -1;
}

// 退出当前进程
void exit(int status)
{
    // 简化实现
    for (;;)
        ;
}

// 等待子进程退出
int wait(uint64 *addr)
{
    // 简化实现
    return -1;
}

// 调度器（单核简化版）
void scheduler(void)
{
    // 简化实现
    // 实际系统中需要：
    // 1. 循环查找RUNNABLE状态的进程
    // 2. 切换到选中的进程运行
    for (;;)
    {
        // 空循环
    }
}