# 实验四：中断处理与时钟管理

文件列表如下：
```text
.
├── kernel
│   ├── defs.h
│   ├── types.c
│   ├── param.h
│   ├── entry.S
│   ├── printf.h
│   ├── print.c
│   ├── console.c
│   ├── start.c
│   ├── main.c
│   ├── memlayout.h
│   ├── kalloc.c
│   ├── vm.c
│   ├── string.c
│   ├── assert.h
│   ├── riscv.h
│   ├── spinlock.c
│   ├── spinlock.h
│   ├── kernelvec.S
│   ├── trap.c
│   ├── proc.c
│   └── uart.c
├── kernel.ld
├── Makefile
└── results/
```

## 1、新增各模块简介
本实验围绕 RISC‑V 特权态的陷阱处理（Traps）、异常（Exceptions）与定时器中断（Timer Interrupts），完成从 M 模式到 S 模式的委托、向量设置、寄存器保存与恢复、以及基本的中断服务例程与测试。

- **riscv.h**：扩展 CSR 读写与中断相关的常量（`SSTATUS_SIE`、`MSTATUS_MIE`、`SIE_*`、`r_time()`、`w_mtvec()`、`w_stvec()`、`w_medeleg()`、`w_mideleg()` 等），为陷阱与中断初始化提供底层接口。
- **trap.c**：核心的陷阱与中断处理逻辑，包含定时器初始化、机器模式异常处理、监督模式 `kerneltrap()` 分发、以及 `trap_init()` 中的中断/异常委托配置。
- **kernelvec.S**：监督模式陷阱入口 `kernelvec`（保存/恢复通用寄存器并调用 C 层 `kerneltrap()`），以及机器模式通用入口 `mtrapvec` 和定时器中断入口 `timervec`（与 `mscratch` 配合）。
- **entry.S**：系统入口 `_entry`，设置初始栈并调用 `start()`。
- **start.c**：提供多个独立测试用例（定时器中断、非法指令、写空指针、除零、中断开销测量），驱动 `trap_init()` 并观测输出。
- **proc.c**：简化的进程管理占位实现（单核环境），为后续调度与阻塞/唤醒留出接口。
- **vm.c**：延续上一实验的页表管理与 `kvminit()/kvminithart()`，与本实验的陷阱机制并行存在，支持内核地址空间与设备映射（如 UART）。

## 2、riscv.h 关键扩展
新增或使用的 CSR/位定义与读写函数，支撑陷阱与中断：
```c
#define SSTATUS_SIE (1L << 1)
#define MSTATUS_MIE (1L << 3)
#define SIE_SSIE    (1L << 1)
#define SIE_STIE    (1L << 5)
#define SIE_SEIE    (1L << 9)

static inline uint64 r_time(void) { uint64 x; asm volatile("rdtime %0" : "=r"(x)); return x; }
static inline void   w_mtvec(uint64 x) { asm volatile("csrw mtvec, %0" :: "r"(x)); }
static inline void   w_stvec(uint64 x) { asm volatile("csrw stvec, %0" :: "r"(x)); }
static inline void   w_medeleg(uint64 x) { asm volatile("csrw medeleg, %0" :: "r"(x)); }
static inline void   w_mideleg(uint64 x) { asm volatile("csrw mideleg, %0" :: "r"(x)); }
static inline void   w_mscratch(uint64 x) { asm volatile("csrw mscratch, %0" :: "r"(x)); }
```
这些接口在 `trap_init()` 与 `timer_init()` 中被频繁使用。

## 3、kernelvec.S：监督/机器模式陷阱入口
监督模式入口负责保存现场并调用 C 处理函数：
```asm
kernelvec:
    addi sp, sp, -256
    sd ra, 40(sp)
    ...
    mv a0, sp
    call kerneltrap
    ...
    addi sp, sp, 256
    sret
```
机器模式通用入口负责区分中断/异常、分发到 `timervec` 或打印后停机：
```asm
mtrapvec:
    csrr t0, mcause
    li t1, 0x8000000000000000
    and t2, t0, t1           # 判断中断
    beqz t2, 2f              # 否则异常路径
    li t3, 7
    andi t4, t0, 0xff
    beq t4, t3, timervec     # 机器定时器中断到 timervec
2:
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    call mtrap_print
    call mtrap_halt
```

## 4、trap.c：定时器与陷阱核心逻辑
定时器初始化设置机器模式向量、`mscratch`、并开启 M 模式中断：
```c
void timer_init(void) {
    uint64 interval = 10000; // 缩短间隔便于测试
    w_mtvec((uint64)timervec);
    timer_scratch.interval = interval;
    timer_scratch.mtimecmp = 0x2004000; // CLINT_MTIMECMP
    w_mscratch((uint64)&timer_scratch);
    w_mie(r_mie() | MIE_MTIE);
    w_mstatus(r_mstatus() | MSTATUS_MIE);
    timer_set_next();
}
```
监督模式陷阱分发：识别中断/异常、系统调用，并适当推进 `sepc`（在测试场景中跳过异常指令以继续）：
```c
void kerneltrap(void) {
    uint64 scause = r_scause();
    uint64 sepc   = r_sepc();
    if (scause & (1ULL<<63)) {
        int irq = scause & 0xff;
        if (irq == IRQ_S_TIMER || irq == IRQ_S_SOFT) timer_interrupt_handler();
    } else if (scause == CAUSE_SUPERVISOR_ECALL) {
        w_sepc(sepc + 4);
    } else if (scause == CAUSE_ILLEGAL_INSTRUCTION) {
        w_sepc(sepc + 4);
    } else if (scause == CAUSE_LOAD_PAGE_FAULT || scause == CAUSE_STORE_PAGE_FAULT) {
        w_sepc(sepc + 4);
    }
    timer_set_next();
}
```
初始化委托与向量：
```c
void trap_init(void) {
    initlock(&tickslock, "time");
    w_stvec((uint64)kernelvec);
    w_mideleg((1 << IRQ_S_TIMER) | (1 << IRQ_S_EXT) | (1 << IRQ_S_SOFT));
    w_medeleg((1 << CAUSE_USER_ECALL) |
              (1 << CAUSE_BREAKPOINT) |
              (1 << CAUSE_ILLEGAL_INSTRUCTION) |
              (1 << CAUSE_INSTRUCTION_PAGE_FAULT) |
              (1 << CAUSE_LOAD_PAGE_FAULT) |
              (1 << CAUSE_STORE_PAGE_FAULT));
    timer_init();
    w_sie(r_sie() | SIE_STIE | SIE_SSIE | SIE_SEIE);
}
```

## 5、entry.S 与 start.c：入口与测试
入口设置栈并调用 `start()`：
```asm
_entry:
    la sp, stack0
    li a0, 1024*4
    add sp, sp, a0
    call start
```
测试样例位于 `start.c`，典型用例如下：
```c
void test_timer_interrupt(void) {
    procinit();
    trap_init();
    enable_interrupts();
    // 通过 ticks 计数观察定时器中断次数
    while (received < 5) { if (ticks != prev) { /* ... */ } }
}

void test_illegal_instruction(void) {
    consoleinit();
    trap_init();
    asm volatile(".word 0x0\n"); // 触发非法指令异常
}

void test_store_null(void) {
    consoleinit();
    trap_init();
    volatile int *bad = (int *)0x0; *bad = 0xdeadbeef;
}

void test_divide_by_zero(void) {
    consoleinit();
    trap_init();
    volatile int a = 1, b = 0; volatile int c = a / b;
}

void test_interrupt_overhead(void) {
    consoleinit();
    trap_init(); enable_interrupts();
    // 主动写 mtimecmp 触发近乎即时的中断，测量平均开销
}
```
说明：当前 `main()` 为空，仅用于占位；各测试函数可按需在 `start()` 里调用以验证不同路径的行为与日志输出。

## 6、与虚拟内存的关系
`vm.c` 延续上一实验的页表管理，初始化内核页表并映射内核段与设备（如 UART）。陷阱与中断机制与页表并行工作：
- 机器模式定时器中断通过 CLINT（`mtimecmp`）触发，随后由委托与向量机制在 S 模式处理。
- 监督模式异常（非法指令、页故障）依赖页表来定位异常地址并进行基本恢复（本实验中为了测试推进 `sepc`）。

## 7、测试与结果
可运行 QEMU 观测日志：
```bash
make clean
make run
```
- 定时器中断测试,以 ticks 的递增作为“收到一次定时器中断”的判据，累计到 5 次后结束并统计耗时
    ```c
    void test_timer_interrupt(void)
    {
        procinit();
        trap_init();
        printf("Testing timer interrupt...\n");
        // 启用中断
        enable_interrupts();

        // 使用全局 ticks 变量（由 timer_interrupt_handler 增加）来检测中断
        // 记录起始 ticks
        uint64 start_time = get_time();
        extern uint64 ticks;
        uint64 prev_ticks = ticks;
        int received = 0;

        // 等待若干次 ticks 增加，每次 ticks 增加视为一次定时器中断
        while (received < 5)
        {
            if (ticks != prev_ticks)
            {
                prev_ticks = ticks;
                received++;
                printf("Received interrupt %d (ticks=%d)\n", received, (int)prev_ticks);
            }
            else
            {
                // 没有新中断时输出提示并稍作延时
                static int wait_print_ctr = 0;
                // 每 10 次循环打印一次，避免大量日志刷屏
                if ((wait_print_ctr++ % 10) == 0)
                    printf("Waiting for interrupt %d...\n", received + 1);
                for (volatile int i = 0; i < 1000000; i++)
                    ;
            }
        }

        uint64 end_time = get_time();
        printf("Timer test completed: %d interrupts in %d cycles\n", received, (int)(end_time - start_time));
    }
    ```
    测试结果如下:
    ![lab4测试1.png](results/lab4测试1.png)
- 测试非法指令异常，asm volatile(".word 0x0"): 向指令流插入一个无效的机器字，从而必然触发非法指令异常。
  ```c
  void test_illegal_instruction(void)
  {
    consoleinit();
    printf("Testing illegal instruction...\n");

    // 初始化中断/异常处理设施
    trap_init();
    printf("Triggering illegal instruction (handler confirmation skipped)...\n");
    asm volatile(".word 0x0\n");
    printf(" -> illegal instruction triggered (confirmation omitted)\n");
  }
  ```
  测试结果如下:
  ![lab4测试2.png](results/lab4测试2.png)
- 测试内存访问异常，volatile int *bad = (int *)0x0;: 创建一个指向 NULL 的指针，并尝试写入数据。
  ```c
    void test_store_null(void)
    {
        consoleinit();
        printf("Testing exception handling...\n");

        // 初始化中断/异常处理设施
        trap_init();
        printf("Triggering store to NULL (may cause exit on some QEMU configs)...\n");
        volatile int *bad = (int *)0x0;
        *bad = 0xdeadbeef;
        printf(" -> store to NULL executed (confirmation omitted)\n");
    }
  ```
  测试结果如下:
  ![lab4测试3.png](results/lab4测试3.png)
- 测试中断处理开销，共触发3个定时器中断，并记录处理开销
   ```c
   void test_interrupt_overhead(void)
    {
        consoleinit();
        printf("Testing interrupt overhead...\n");

        // 初始化中断/异常系统
        trap_init();

        // 关闭定时器中断打印以免影响测量（不引用外部符号，避免链接依赖）。

        // 确保 ticks 初始值
        extern uint64 ticks;
        ticks = 0;
        // 给定时器一点时间确保 mtimecmp 已写入并生效
        for (volatile int w = 0; w < 1000000; w++)
            ;

        // 主动触发若干次定时器中断来测量中断开销：直接写入 mtimecmp 请求即时中断
        enable_interrupts();
        extern uint64 ticks;

        // 小偏移，用于安排近乎立即的中断（cycles）
        const uint64 trigger_delta = 10;
        const int needed = 3; // 触发的中断数量
        uint64 prev = ticks;

        printf("%d timer interrupts (delta=%d cycles) to measure overhead...\n", needed, (int)trigger_delta);
        uint64 start_forced = get_time();
        int received = 0;
        for (int i = 0; i < needed; i++)
        {
            // 写入 mtimecmp （使用 riscv.h 中的 MTIMECMP 地址宏）
            uint64 mcmp = MTIMECMP;
            *(volatile uint64 *)mcmp = r_time() + trigger_delta;

            // 等待 ticks 增加（有超时保护）
            uint64 wait_deadline = get_time() + 200000000;
            while (ticks == prev && get_time() < wait_deadline)
                asm volatile("wfi");

            if (ticks != prev)
            {
                prev = ticks;
                received++;
                printf("Received tick %d (ticks=%d)\n", received, (int)prev);
            }
            else
            {
                printf("Timeout waiting for tick %d\n", i + 1);
            }
        }
        uint64 end_forced = get_time();

        if (received > 0)
        {
            uint64 total_cycles = end_forced - start_forced;
            printf("%d interrupts in %d cycles, avg %d cycles/interrupt\n", received, (int)total_cycles, (int)(total_cycles / received));
        }
        else
        {
            printf("No interrupts observed. Check timer configuration and CLINT mapping.\n");
        }

        // 清理：禁用中断
        disable_interrupts();
    }
  ```
  测试结果如下:
  ![lab4测试4.png](results/lab4测试4.png)

