# 实验六：系统调用

文件列表如下：
```text
.
├── kernel
│   ├── defs.h
│   ├── types.h
│   ├── param.h
│   ├── entry.S
│   ├── printf.h
│   ├── printf.c
│   ├── console.c
│   ├── start.c
│   ├── main.c
│   ├── memlayout.h
│   ├── kalloc.c
│   ├── vm.h
│   ├── vm.c
│   ├── string.c
│   ├── assert.h
│   ├── riscv.h
│   ├── spinlock.c
│   ├── spinlock.h
│   ├── kernelvec.S
│   ├── trampoline.S
│   ├── swtch.S
│   ├── trap.c
│   ├── proc.h
│   ├── proc.c
│   ├── process_api.c
│   ├── syscall.h
│   ├── syscall.c
│   ├── sync_test.c
│   └── uart.c
├── user
│   └── usys.S
├── kernel.ld
├── Makefile
└── results/
```

## 1、新增各模块简介
本实验在已有进程与陷阱基础上，打通“用户态 → 内核态 → 用户态”的系统调用通路，提供最小可用的调用集与参数传递机制。

- **syscall.h / syscall.c**：定义系统调用号与分发表；实现参数获取（`argint/argaddr/argstr`）与具体内核服务（如 `sys_write`、`sys_getpid`、`sys_exit`、`sys_fork`、`sys_wait`）。
- **user/usys.S**：生成用户态调用桩（stubs），将调用号置于 `a7` 并执行 `ecall` 进入内核。
- **trap.c**：在监督态陷阱处理路径中识别用户态 `ecall`（`CAUSE_USER_ECALL`），推进 `sepc` 后调用 `syscall()` 完成分发与返回值设置。
- **trampoline.S**：用户态陷阱入口 `uservec` 保存用户寄存器并切换到内核页表；返回入口 `userret` 恢复寄存器与用户页表，最终 `sret` 回到用户态。

## 2、系统调用号与用户桩（usys.S）
调用号在 [kernel/syscall.h](kernel/syscall.h) 中定义：
```c
#define SYS_write  1
#define SYS_getpid 2
#define SYS_exit   3
#define SYS_fork   4
#define SYS_wait   5
```
用户态桩在 [user/usys.S](user/usys.S) 中为每个调用生成统一序列：
```asm
write:   li a7, SYS_write;   ecall; ret
getpid:  li a7, SYS_getpid;  ecall; ret
exit:    li a7, SYS_exit;    ecall; ret
fork:    li a7, SYS_fork;    ecall; ret
wait:    li a7, SYS_wait;    ecall; ret
```

## 3、陷阱与分发（trap.c → syscall.c）
监督态陷阱在 [kernel/trap.c](kernel/trap.c) 中识别用户 `ecall`，并在推进 `sepc` 后进入分发：
```c
// 伪代码：检测 CAUSE_USER_ECALL，推进 sepc 并调用 syscall();
// 实际推进与返回在 usertrap/usertrapret/trampoline 协作路径中完成。
```
分发与参数获取在 [kernel/syscall.c](kernel/syscall.c) 中完成：
```c
static uint64 (*syscalls[])(void) = {
  [SYS_write]  sys_write,
  [SYS_getpid] sys_getpid,
  [SYS_exit]   sys_exit,
  [SYS_fork]   sys_fork,
  [SYS_wait]   sys_wait,
};

void syscall(void) {
  int num = myproc()->trapframe->a7;
  if (valid(num)) myproc()->trapframe->a0 = syscalls[num]();
  else myproc()->trapframe->a0 = -1;
}
```
参数从 `trapframe` 的 `a0..a5` 读取并转换：
```c
uint64 argraw(int n) { /* 从 trapframe->a0..a5 取第 n 个参数 */ }
void   argint(int n, int *ip);
void   argaddr(int n, uint64 *ip);
int    argstr(int n, char *buf, int max);
```

## 4、内核实现示例（syscall.c）
- **写输出 `sys_write`**：支持 `fd=1/2`，先 `copyin_user` 将用户缓冲读入内核临时缓冲，再逐字输出到控制台；若 `copyin_user` 失败，允许使用“内核指针”作为测试退路。
- **进程标识 `sys_getpid`**：直接返回当前进程 `pid`。
- **退出 `sys_exit`**：读取状态码并调用 `exit(status)`，不返回。
- **创建 `sys_fork`**：委托已有 `fork()` 逻辑。
- **等待 `sys_wait`**：用户传入状态指针地址 `addr`，返回子进程 `pid` 或错误码。
 

核心辅助：
```c
int copyin_user(pagetable_t, char *dst, uint64 srcva, uint64 len);
int copyout_user(pagetable_t, uint64 dstva, char *src, uint64 len);
int copyinstr_user(pagetable_t, char *buf, uint64 srcva, int max);
```
这些函数通过 `walkaddr()` 分页地转换用户虚拟地址到物理地址，处理跨页与越界。

## 5、用户态与返回路径（trampoline.S / usertrapret）
- **进入内核**：`uservec` 保存用户寄存器到 `TRAPFRAME`，切换 `satp` 到内核页表，最终 `jalr usertrap()`。
- **返回用户**：`userret` 在 `satp` 切回用户页表后恢复寄存器，并 `sret` 依据 `sepc` 返回。`usertrapret()` 在 [kernel/trap.c](kernel/trap.c) 中设置 `sstatus`/`sepc`/`satp` 并跳转到 `userret`。

## 6、测试用例与结果（start.c）
可在 QEMU 运行下观察行为：
```bash
make clean
make run
```
- **基础系统调用测试**通过调用syscall()验证系统调用分派与参数传递是否正确，具体覆盖了write(1, msg, len)用于输出字符串到控制台并检查返回字节数，以及getpid()用于读取当前进程PID并验证返回值写回机制：
  ```c
  void test_syscall(void)
    {
        printf("Testing system calls...\n");
        uartinit();
        consoleinit();
        // 初始化物理内存、页表和进程表（但不启动中断/定时器以降低失败概率）
        pmem_init();
        kvminit();
        procinit();
        struct trapframe tf;
        struct proc fakep;

        memset(&tf, 0, sizeof(tf));
        memset(&fakep, 0, sizeof(fakep));
        fakep.trapframe = &tf;
        // 使用内核页表作为简化的用户页表，这样 copyin/walkaddr 不会 deref NULL
        extern pagetable_t kernel_pagetable;
        fakep.pagetable = kernel_pagetable;
        fakep.pid = 1;
        strncpy(fakep.name, "fakes", sizeof(fakep.name));

        struct proc *old = myproc();
        setproc(&fakep);

        static char msg[] = "[syscall test] Hello from user-space\n";
        tf.a7 = SYS_write;
        tf.a0 = 1;
        tf.a1 = (uint64)msg;
        tf.a2 = (int)strlen(msg);
        syscall();
        printf("test_syscall: write returned %d\n", (int)tf.a0);

        tf.a7 = SYS_getpid;
        syscall();
        printf("test_syscall: getpid returned %d\n", (int)tf.a0);

        setproc(old);
    }
    ```
  测试结果示意：
  ![lab6测试1.png](results/lab6测试1.png)

- **fork/wait 流程测试**（通过SYS_fork验证系统调用分派与子进程创建是否成功（读取返回的子PID），随后在进程表中定位该子进程并将其状态人工设为ZOMBIE以模拟子进程退出，再唤醒父进程并调用SYS_wait验证等待机制与回收路径是否正确（检查wait返回的子PID）。
  ```c
    void test_syscall_fork(void)
    {
        printf("Testing fork/wait syscalls...\n");
        uartinit();
        consoleinit();
        pmem_init();
        kvminit();
        procinit();

        struct proc *p = allocproc();
        if (!p)
        {
            printf("test_syscall_fork: allocproc failed\n");
            return;
        }
        // 为父进程创建页表并设置基本字段
        p->pagetable = create_pagetable();
        p->sz = PGSIZE;        // minimal size
        p->trapframe->epc = 0; // not used
        strncpy(p->name, "parent", sizeof(p->name));

        // 设置为当前进程
        // allocproc() 返回时 p->lock 是持有状态；在继续之前释放它，
        // 否则后续的 allocproc()（由 fork() 调用）在遍历 proc 表时会遇到已持有的锁并触发 panic
        release(&p->lock);
        struct proc *old = myproc();
        setproc(p);

        // 发起 fork 系统调用
        p->trapframe->a7 = SYS_fork;
        syscall();
        int childpid = (int)p->trapframe->a0;
        printf("test_syscall_fork: fork returned %d\n", childpid);

        if (childpid <= 0)
        {
            printf("test_syscall_fork: fork failed\n");
            setproc(old);
            return;
        }

        // 在 proc 表中找到子进程，并将其标记为 ZOMBIE（模拟子进程已退出）
        extern struct proc proc[];
        extern struct spinlock wait_lock;
        struct proc *pp = 0;
        for (int i = 0; i < NPROC; i++)
        {
            if (proc[i].pid == childpid)
            {
                pp = &proc[i];
                break;
            }
        }
        if (!pp)
        {
            printf("test_syscall_fork: child not found\n");
            setproc(old);
            return;
        }
        acquire(&pp->lock);
        pp->xstate = 42;
        pp->state = ZOMBIE;
        release(&pp->lock);

        // 唤醒父进程（wait 中会检查）
        acquire(&wait_lock);
        wakeup(p);
        release(&wait_lock);

        // 父进程调用 wait 系统调用（传入 addr = 0 表示不拷贝出状态）
        p->trapframe->a7 = SYS_wait;
        p->trapframe->a0 = 0; // addr = NULL
        syscall();
        int waited = (int)p->trapframe->a0;
        printf("test_syscall_fork: wait returned %d\n", waited);

        setproc(old);
    }
    ```
  测试结果示意：
  ![lab6测试2.png](results/lab6测试2.png)
