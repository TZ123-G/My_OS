#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "printf.h"
#include "defs.h"

// 局部用户内存写入助手：用现有的 walkaddr + memmove 实现 copyout
static int
copyout_user(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
    uint64 n, va0, pa0;

    while (len > 0)
    {
        va0 = PGROUNDDOWN(dstva);
        if (va0 >= MAXVA)
            return -1;

        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;

        n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;
        memmove((void *)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

struct cpu cpu;
struct proc proc[NPROC];
struct spinlock proc_lock;
struct spinlock wait_lock;

// 初始化进程系统
void procinit(void)
{
    initlock(&proc_lock, "proc_lock");
    initlock(&wait_lock, "wait_lock");

    for (int i = 0; i < NPROC; i++)
    {
        initlock(&proc[i].lock, "proc");
        proc[i].state = UNUSED;
        proc[i].kstack = 0;
        proc[i].trapframe = 0;
        proc[i].pagetable = 0;
        proc[i].sz = 0;
        proc[i].pid = 0;
        proc[i].parent = 0;
        proc[i].name[0] = 0;
        proc[i].killed = 0;
    }
}

// 获取当前进程
struct proc *
myproc(void)
{
    push_off();
    struct cpu *c = &cpu;
    struct proc *p = c->proc;
    pop_off();
    return p;
}

// 设置当前运行的进程
void setproc(struct proc *p)
{
    push_off();
    cpu.proc = p;
    pop_off();
}

// 分配进程结构
struct proc *
allocproc(void)
{
    struct proc *p;

    acquire(&proc_lock);
    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->state == UNUSED)
        {
            goto found;
        }
        else
        {
            release(&p->lock);
        }
    }
    release(&proc_lock);
    return 0;

found:
    // 分配进程ID
    static int nextpid = 1;
    p->pid = nextpid++;

    // 分配内核栈
    if ((p->kstack = (uint64)alloc_page()) == 0)
    {
        release(&p->lock);
        release(&proc_lock);
        return 0;
    }

    // 分配陷阱帧
    p->trapframe = (struct trapframe *)(p->kstack + PGSIZE - sizeof(*p->trapframe));

    // 设置上下文，返回到forkret
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    release(&proc_lock);
    return p;
}

// 进程创建
int fork(void)
{
    struct proc *np;
    struct proc *p = myproc();

    // 分配新进程
    if ((np = allocproc()) == 0)
    {
        return -1;
    }

    // 复制用户内存：使用已有的 copy_pagetable_mapping 来复制映射
    if (copy_pagetable_mapping(p->pagetable, np->pagetable, 0, p->sz) < 0)
    {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // 复制陷阱帧
    *(np->trapframe) = *(p->trapframe);
    np->trapframe->a0 = 0; // 子进程返回0

    // 设置父进程
    np->parent = p;

    // 复制进程名（没有 safestrcpy 时使用 strncpy 并确保 NUL 结尾）
    strncpy(np->name, p->name, sizeof(p->name));
    np->name[sizeof(p->name) - 1] = '\0';

    // 标记为可运行
    acquire(&proc_lock);
    np->state = RUNNABLE;
    release(&np->lock);
    release(&proc_lock);

    return np->pid;
}

// 进程退出
void exit(int status)
{
    struct proc *p = myproc();

    if (p == &proc[1]) // init进程不能退出
        panic("init exiting");
    // 不在此处释放内核栈或页表：资源应由父进程在 wait()/freeproc() 中回收。
    // 在调用 sched() 前必须持有 p->lock（sched() 要求如此）。
    acquire(&p->lock);

    // 设置退出状态并标记为 ZOMBIE
    p->xstate = status;
    p->state = ZOMBIE;

    // 唤醒父进程让其可以在 wait() 中收集此子进程
    acquire(&wait_lock);
    wakeup(p->parent);
    release(&wait_lock);

    // 切换到其他进程（sched 要求 p->lock 被持有）
    sched();

    // sched() 不应返回；若返回则为严重错误
    panic("exit: sched returned");
}

// 等待子进程退出
int wait(uint64 addr)
{
    struct proc *pp;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;)
    {
        havekids = 0;
        for (pp = proc; pp < &proc[NPROC]; pp++)
        {
            if (pp->parent == p)
            {
                acquire(&pp->lock);
                if (pp->state == ZOMBIE)
                {
                    pid = pp->pid;
                    if (addr != 0 && copyout_user(p->pagetable, addr, (char *)&pp->xstate, sizeof(pp->xstate)) < 0)
                    {
                        release(&pp->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    freeproc(pp);
                    release(&pp->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&pp->lock);
                havekids = 1;
            }
        }

        if (!havekids || p->killed)
        {
            release(&wait_lock);
            return -1;
        }

        sleep(p, &wait_lock);
    }
}

// 杀死进程
int kill(int pid)
{
    struct proc *p;

    acquire(&proc_lock);
    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->pid == pid)
        {
            p->killed = 1;
            if (p->state == SLEEPING)
            {
                p->state = RUNNABLE;
            }
            release(&p->lock);
            release(&proc_lock);
            return 0;
        }
        release(&p->lock);
    }
    release(&proc_lock);
    return -1;
}

// 释放进程资源
void freeproc(struct proc *p)
{
    // The trapframe resides in the kernel stack page (allocated as p->kstack)
    // and must not be freed separately. Only free the page table if present.
    p->trapframe = 0;

    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);

    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

// 释放进程页表以及用户内存页（如果有）
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
    // 遍历用户虚拟地址空间，释放所有叶子页并清除映射
    for (uint64 a = 0; a < sz; a += PGSIZE)
    {
        pte_t *pte = walk_lookup(pagetable, a);
        if (pte && (*pte & PTE_V))
        {
            uint64 pa = PTE2PA(*pte);
            *pte = 0;
            free_page((void *)pa);
        }
    }

    // 释放页表结构本身
    destroy_pagetable(pagetable);
}

// 进程睡眠
void sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    acquire(&p->lock);
    release(lk);

    p->chan = chan;
    p->state = SLEEPING;

    sched();

    p->chan = 0;
    release(&p->lock);
    acquire(lk);
}

// 唤醒进程
void wakeup(void *chan)
{
    struct proc *p;

    acquire(&proc_lock);
    for (p = proc; p < &proc[NPROC]; p++)
    {
        if (p != myproc())
        {
            acquire(&p->lock);
            if (p->state == SLEEPING && p->chan == chan)
            {
                p->state = RUNNABLE;
            }
            release(&p->lock);
        }
    }
    release(&proc_lock);
}

// 让出CPU
void yield(void)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

// 调度当前进程
void sched(void)
{
    struct proc *p = myproc();

    if (!holding(&p->lock))
        panic("sched p->lock");
    if (cpu.noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    swtch(&p->context, &cpu.context);
}

// fork返回，切换到用户空间
void forkret(void)
{
    release(&myproc()->lock);

    // 第一次返回到用户空间
    usertrapret();
}

// 调度器
void scheduler(void)
{
    struct proc *p;
    struct cpu *c = &cpu;

    c->proc = 0;
    for (;;)
    {
        intr_on();

        acquire(&proc_lock);
        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                p->state = RUNNING;
                c->proc = p;
                release(&proc_lock);
                swtch(&c->context, &p->context);
                // re-acquire proc_lock after coming back from the process
                acquire(&proc_lock);
                c->proc = 0;
            }
            release(&p->lock);
        }
        release(&proc_lock);
    }
}

// 调试：打印进程表中所有非 UNUSED 条目，包含可读状态名
void debug_proc(void)
{
    static const char *state_names[] = {
        "UNUSED",
        "USED",
        "SLEEPING",
        "RUNNABLE",
        "RUNNING",
        "ZOMBIE",
    };

    printf("=== Process Table ===\n");

    acquire(&proc_lock);
    for (int i = 0; i < NPROC; i++)
    {
        struct proc *p = &proc[i];
        acquire(&p->lock);
        if (p->state != UNUSED)
        {
            const char *s = "?";
            if (p->state >= UNUSED && p->state <= ZOMBIE)
                s = state_names[p->state];
            printf("PID:%d State:%s Name:%s\n", p->pid, s, p->name);
        }
        release(&p->lock);
    }
    release(&proc_lock);
}