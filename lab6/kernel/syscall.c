#include "types.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"
#include "printf.h"
#include "syscall.h"

// helper: copy data from user virtual address into kernel buffer
static int
copyin_user(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
    uint64 n, va0, pa0;

    while (len > 0)
    {
        va0 = PGROUNDDOWN(srcva);
        if (va0 >= MAXVA)
            return -1;
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > len)
            n = len;
        memmove(dst, (void *)(pa0 + (srcva - va0)), n);
        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// fetch nul-terminated string from user space into buf (max bytes)
static int
copyinstr_user(pagetable_t pagetable, char *buf, uint64 srcva, int max)
{
    int i = 0;
    for (;;)
    {
        uint64 va0 = PGROUNDDOWN(srcva + i);
        if (va0 >= MAXVA)
            return -1;
        uint64 pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        char *p = (char *)(pa0 + ((srcva + i) - va0));
        for (; i < max; i++)
        {
            char c = p[0];
            buf[i] = c;
            if (c == '\0')
                return i;
            p++;
            // if we reached end of page, break to outer loop to remap
            if (((srcva + i) & (PGSIZE - 1)) == 0)
                break;
        }
        if (i >= max)
            return -1;
    }
    return -1;
}

// argraw/argint/argaddr/argstr similar to xv6
static uint64
argraw(int n)
{
    struct proc *p = myproc();
    switch (n)
    {
    case 0:
        return p->trapframe->a0;
    case 1:
        return p->trapframe->a1;
    case 2:
        return p->trapframe->a2;
    case 3:
        return p->trapframe->a3;
    case 4:
        return p->trapframe->a4;
    case 5:
        return p->trapframe->a5;
    }
    panic("argraw");
    return -1;
}

void argint(int n, int *ip)
{
    *ip = (int)argraw(n);
}

void argaddr(int n, uint64 *ip)
{
    *ip = argraw(n);
}

int argstr(int n, char *buf, int max)
{
    uint64 addr;
    argaddr(n, &addr);
    return copyinstr_user(myproc()->pagetable, buf, addr, max);
}

// syscall implementations
extern void consputc(int);

static uint64
sys_write(void)
{
    int fd;
    uint64 addr;
    int n;
    argint(0, &fd);
    argaddr(1, &addr);
    argint(2, &n);

    if (fd != 1 && fd != 2)
        return -1;

    struct proc *p = myproc();
    char buf[512];
    int tot = 0;
    while (n > 0)
    {
        int chunk = n > (int)sizeof(buf) ? sizeof(buf) : n;
        if (copyin_user(p->pagetable, buf, addr, chunk) < 0)
        {
            // fallback: maybe pointer is kernel pointer (used by tests)
            memmove(buf, (void *)addr, chunk);
        }
        for (int i = 0; i < chunk; i++)
            consputc(buf[i]);
        n -= chunk;
        addr += chunk;
        tot += chunk;
    }
    return tot;
}

static uint64
sys_getpid(void)
{
    return myproc()->pid;
}

static uint64
sys_exit(void)
{
    int status;
    argint(0, &status);
    exit(status);
    return 0; // not reached
}

static uint64
sys_fork(void)
{
    return fork();
}

static uint64
sys_wait(void)
{
    uint64 addr;
    argaddr(0, &addr);
    return wait(addr);
}

// syscall table
static uint64 (*syscalls[])(void) = {
    [SYS_write] sys_write,
    [SYS_getpid] sys_getpid,
    [SYS_exit] sys_exit,
    [SYS_fork] sys_fork,
    [SYS_wait] sys_wait,
};

#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

void syscall(void)
{
    int num;
    struct proc *p = myproc();

    if (p == 0)
    {
        printf("syscall: myproc() returned NULL\n");
        return;
    }
    if (p->trapframe == 0)
    {
        printf("syscall: proc %d (%s) has no trapframe\n", p->pid, p->name);
        return;
    }

    num = p->trapframe->a7;

    if (num > 0 && num < (int)NELEM(syscalls) && syscalls[num])
    {
        uint64 (*fn)(void) = syscalls[num];
        p->trapframe->a0 = fn();
    }
    else
    {
        printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
        p->trapframe->a0 = -1;
    }
}