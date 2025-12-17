# 实验五：进程管理与调度

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
│   ├── sync_test.c
│   └── uart.c
├── kernel.ld
├── Makefile
└── results/
```

## 1、新增各模块简介
本实验围绕“进程（`proc`）生命周期、上下文切换（`swtch`）、用户态与内核态切换（`trampoline`）、抢占式调度（`scheduler`）、以及睡眠/唤醒与退出/等待机制”，实现一个可运行的简化版进程子系统。

- **proc.h / proc.c**：定义 `struct proc`、`struct trapframe`、`struct context` 与进程状态枚举；实现 `procinit()` 初始化、`allocproc()` 分配进程、`fork()/exit()/wait()/kill()` 生命周期、`sleep()/wakeup()/yield()` 同步与让出、`scheduler()` 调度循环、`debug_proc()` 调试输出等。
- **swtch.S**：内核上下文切换例程 `swtch(old, new)`，保存/恢复 `ra/sp/s0..s11`，在调度器与进程之间切换。
- **trampoline.S**：用户态陷阱入口 `uservec` 与返回入口 `userret`，配合 `trapframe` 在用户态和内核态间切换，并与 `usertrapret()`（位于 `trap.c`）协作返回用户态。
- **process_api.c**：提供简化的“内核线程”创建 API（`create_process(entry)`），通过设置 `context.ra=kernel_thread_trampoline` 让进程从指定入口函数开始执行，便于测试调度与进程表。
- **trap.c**：延续实验四的中断初始化与定时器服务，增加在定时器中断后对当前进程的 `yield()`，从而形成抢占式调度；实现 `usertrapret()` 的返回用户态设置（`sstatus/sepc/satp`）。

## 2、核心数据结构（proc.h）
进程状态与上下文、陷阱帧定义：
```c
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct context {
	uint64 ra, sp;
	uint64 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

struct trapframe {
	uint64 kernel_satp;   // 内核页表
	uint64 kernel_sp;     // 进程内核栈顶
	uint64 kernel_trap;   // usertrap()
	uint64 epc;           // 用户PC
	uint64 kernel_hartid; // 保存 tp
	// 以及通用寄存器保存槽位 a0..a7、s0..s11、t0..t6 等
};

struct proc {
	struct spinlock lock;
	enum procstate state;
	void *chan; int killed; int xstate; int pid;
	struct proc *parent;
	uint64 kstack; uint64 sz; pagetable_t pagetable;
	struct trapframe *trapframe; struct context context;
	struct inode *cwd; char name[16];
};
```

说明：`trapframe` 由 `trampoline` 保存/恢复用户寄存器并承载返回路径参数（如 `kernel_sp/satp`）；`context` 是内核态上下文，用于 `swtch` 在调度器与进程之间切换。

## 3、上下文切换（swtch.S）与陷阱跳板（trampoline.S）
上下文切换保存/恢复被调用者保存寄存器：
```asm
swtch:
	sd ra, 0(a0); sd sp, 8(a0);
	sd s0,16(a0); sd s1,24(a0); ... sd s11,104(a0)
	ld ra, 0(a1); ld sp, 8(a1);
	ld s0,16(a1); ld s1,24(a1); ... ld s11,104(a1)
	ret
```
用户陷阱入口保存用户寄存器、切换到内核页表并跳到 `usertrap()`，返回路径由 `userret` 切换回用户页表、恢复寄存器并 `sret`：
```asm
uservec:
	li a0, TRAPFRAME; sd ra,40(a0); sd sp,48(a0); ...
	ld sp, 8(a0); ld t0,16(a0); ld t1,0(a0);  // kernel_sp/kernel_trap/kernel_satp
	csrw satp, t1; sfence.vma; jalr t0        // 进入 usertrap()

userret:
	sfence.vma; csrw satp, a0; sfence.vma     // 切回用户页表
	li a0, TRAPFRAME; ld ra,40(a0); ld sp,48(a0); ...
	ld a0,112(a0); sret
```

## 4、进程创建、退出与等待（proc.c / process_api.c）
分配进程与设定初始上下文：
```c
struct proc *allocproc(void) {
	// 选 UNUSED 槽位，分配 pid 与内核栈
	p->trapframe = (struct trapframe *)(p->kstack + PGSIZE - sizeof(*p->trapframe));
	memset(&p->context, 0, sizeof(p->context));
	p->context.ra = (uint64)forkret; // 首次调度返回到 forkret -> usertrapret()
	p->context.sp = p->kstack + PGSIZE;
}

int fork(void) {
	struct proc *np = allocproc();
	np->pagetable = create_pagetable();
	copy_pagetable_mapping(p->pagetable, np->pagetable, 0, p->sz);
	*(np->trapframe) = *(p->trapframe); np->trapframe->a0 = 0; // 子返回0
	np->parent = p; np->state = RUNNABLE;
}

void exit(int status) {
	acquire(&p->lock); p->xstate = status; p->state = ZOMBIE;
	wakeup(p->parent); sched();
}

int wait(uint64 addr) {
	// 轮询子进程，若发现 ZOMBIE 则回收并返回 pid
}
```
内核线程创建 API 便于测试：
```c
int create_process(void (*entry)(void)) {
	struct proc *p = allocproc();
	p->trapframe->a0 = (uint64)entry;             // 传递入口
	p->context.ra = (uint64)kernel_thread_trampoline; // 从入口运行
	p->state = RUNNABLE; return p->pid;
}

void kernel_thread_trampoline(void) {
	release(&myproc()->lock);                      // 对称 forkret
	void (*fn)(void) = (void(*)(void))myproc()->trapframe->a0;
	if (fn) fn(); exit(0);
}
```

## 5、睡眠/唤醒与让出（proc.c）
调度相关原语：
```c
void sleep(void *chan, struct spinlock *lk) {
	acquire(&p->lock); release(lk); p->chan = chan; p->state = SLEEPING; sched();
	p->chan = 0; release(&p->lock); acquire(lk);
}

void wakeup(void *chan) {
	for (p in proc[]) if (p->state==SLEEPING && p->chan==chan) p->state = RUNNABLE;
}

void yield(void) {
	acquire(&p->lock); p->state = RUNNABLE; sched(); release(&p->lock);
}

static void sched(void) {
	// 要求持有 p->lock；切到 CPU 的调度上下文
	swtch(&p->context, &cpu.context);
}
```

## 6、抢占式调度（proc.c / trap.c）
调度器循环：
```c
void scheduler(void) {
	for (;;) {
		intr_on();
		acquire(&proc_lock);
		for (p in proc[]) if (p->state == RUNNABLE) {
			p->state = RUNNING; cpu.proc = p; release(&proc_lock);
			swtch(&cpu.context, &p->context); acquire(&proc_lock); cpu.proc = 0;
		}
		release(&proc_lock);
	}
}
```
定时器中断触发让出，形成时间片：
```c
void kerneltrap(void) {
	if (interrupt && (irq==S_TIMER||S_SOFT)) {
		timer_interrupt_handler();
		if (myproc()!=0) yield();
		return;
	}
}
```
用户态返回设置（`usertrapret`）与 `trampoline` 配合：
```c
void usertrapret(void) {
	uint64 x = r_sstatus(); x &= ~SSTATUS_SPP; x |= SSTATUS_SPIE; w_sstatus(x);
	w_sepc(p->trapframe->epc);
	uint64 satp = MAKE_SATP(p->pagetable); // a0
	extern char trampoline[], userret[]; uint64 addr = (uint64)trampoline + ((uint64)userret - (uint64)trampoline);
	asm volatile("mv a0, %0; jr %1" :: "r"(satp), "r"(addr));
}
```

## 7、测试用例与结果（start.c）
可在 QEMU 运行下观察行为：
```bash
make clean
make run
```
- 验证进程创建机制是否工作，以及进程表的容量限制是否生效。
  ```c
  void simple_task(void){ printf("simple_task started\n"); /* busy */ printf("simple_task exiting\n"); }
  void test_process_creation(void){ pmem_init(); procinit(); int pid=create_process(simple_task); assert(pid>0); trap_init(); enable_interrupts(); scheduler(); }
  ```
  测试结果示意：
  ![lab5测试1.png](results/lab5测试1.png)

- 验证调度器在多可运行进程场景下的时间片轮转与进程退出路径是否正常。
  ```c
  void cpu_intensive_task(void){ /* 计算密集型循环，周期性 yield() */ }
  void scheduler_watcher(void){ /* 等待一段时间并退出 */ }
  void test_scheduler(void){ pmem_init(); procinit(); for(i=0;i<3;i++) create_process(cpu_intensive_task); create_process(scheduler_watcher); scheduler(); }
  ```
  测试结果示意：
  ![lab5测试2.png](results/lab5测试2.png)

- 调试和观察当前进程表的快照。遍历全局进程数组 proc[]，对每个非 UNUSED 的进程打印关键信息
  ```c
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
  void debug_proc_table(void){ pmem_init(); procinit(); for(i=0;i<3;i++) create_process(cpu_intensive_task); debug_proc(); }
  ```
  ![lab5测试3.png](results/lab5测试3.png)
- 创建消费者和生产者进程，并进入调度器以运行它们，缓冲区设置为1。
  ```c
    void test_synchronization(void)
    {
        printf("Starting synchronization test\n");

        // 初始化缓冲区与必要子系统（物理内存、进程表、陷阱/定时器）
        shared_buffer_init();
        pmem_init();
        procinit();
        //trap_init();
        //enable_interrupts();

        // 创建消费者和生产者进程
        int cpid = create_process(consumer_task);
        if (cpid <= 0)
            printf("test_synchronization: create_process failed for consumer\n");

        int ppid = create_process(producer_task);
        if (ppid <= 0)
            printf("test_synchronization: create_process failed for producer\n");


        // 进入调度器以运行创建的进程（scheduler 不返回）
        scheduler();
    }
    ```
    测试结果如下：
    ![lab5测试4.png](results/lab5测试4.png)


