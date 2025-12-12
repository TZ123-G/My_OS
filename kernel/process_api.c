#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// extern from proc.c
extern struct spinlock proc_lock;

// trampoline called when a kernel-created thread starts running
// it retrieves the entry function pointer from p->trapframe->a0,
// calls it and then exits the process when the function returns.
void kernel_thread_trampoline(void)
{
    struct proc *p = myproc();
    if (!p)
        return;
    // If the scheduler switched to this kernel thread with p->lock held,
    // release it here (mirror what forkret does for fork-created procs).
    release(&p->lock);

    void (*fn)(void) = (void (*)(void))p->trapframe->a0;
    if (fn)
        fn();

    // If the thread function returns, exit the process with status 0
    exit(0);
}

// Allocate a proc (wrapper)
struct proc *alloc_process(void)
{
    return allocproc();
}

// Free proc (wrapper)
void free_process(struct proc *p)
{
    if (!p)
        return;
    freeproc(p);
}

// Create a kernel thread / process that will run `entry`.
// Returns pid on success, -1 on failure.
int create_process(void (*entry)(void))
{
    if (!entry)
        return -1;

    struct proc *p = allocproc();
    if (!p)
        return -1;

    // At this point allocproc returns with p->lock held.
    // Store the entry function pointer into trapframe->a0 so trampoline can read it.
    p->trapframe->a0 = (uint64)entry;
    // Set the parent to the current process so wait/wait_process can observe children
    p->parent = myproc();
    // Give the kernel thread a name of form "proc<pid>" (e.g. proc1)
    {
        char buf[16];
        int idx = 0;
        buf[idx++] = 'p';
        buf[idx++] = 'r';
        buf[idx++] = 'o';
        buf[idx++] = 'c';
        int x = p->pid;
        char tmp[12];
        int ti = 0;
        if (x == 0)
            tmp[ti++] = '0';
        while (x > 0 && ti < (int)sizeof(tmp))
        {
            tmp[ti++] = '0' + (x % 10);
            x /= 10;
        }
        for (int j = ti - 1; j >= 0; j--)
        {
            if (idx < (int)sizeof(buf) - 1)
                buf[idx++] = tmp[j];
        }
        buf[idx] = '\0';
        strncpy(p->name, buf, sizeof(p->name));
        p->name[sizeof(p->name) - 1] = '\0';
    }

    // Set the context so that when scheduled it starts at kernel_thread_trampoline
    p->context.ra = (uint64)kernel_thread_trampoline;
    p->context.sp = p->kstack + PGSIZE;

    // mark runnable (follow fork() ordering)
    acquire(&proc_lock);
    p->state = RUNNABLE;
    release(&p->lock);
    release(&proc_lock);

    return p->pid;
}

// Exit current process wrapper
void exit_process(int status)
{
    exit(status);
}

// Wait for a child process; status may be NULL
int wait_process(int *status)
{
    uint64 addr = 0;
    if (status)
        addr = (uint64)status;
    return wait(addr);
}
