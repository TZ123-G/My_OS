// trap.c
#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "printf.h"

// 外部汇编函数声明
extern void kernelvec(void);
extern void timervec(void);
extern void machine_exception_handler(void);
// machine mode exception handler helper (C)
// NOTE: machine_exception_handler was a temporary debug helper; remove it to restore original flow.

// 全局变量定义
uint64 ticks = 0;
struct spinlock tickslock;

// M-mode 异常计数器，用于测试代码检测异常已被处理
volatile uint64 mexception_count = 0;
// 控制定时器中断处理时的打印（打印会严重影响性能测量）
volatile int timer_verbose = 0;

// 定时器 scratch 区域布局（与汇编 timervec 期望的偏移一致）
// layout: saved regs at 0/8/16, then interval @24, and mtimecmp addr @32
struct timer_scratch
{
    uint64 saved0;   // offset 0
    uint64 saved1;   // offset 8
    uint64 saved2;   // offset 16
    uint64 interval; // offset 24
    uint64 mtimecmp; // offset 32
};

struct timer_scratch timer_scratch;

// 中断原因常量定义 (根据 RISC-V 特权规范)
#define CAUSE_MISALIGNED_FETCH 0x0
#define CAUSE_FETCH_ACCESS 0x1
#define CAUSE_ILLEGAL_INSTRUCTION 0x2
#define CAUSE_BREAKPOINT 0x3
#define CAUSE_MISALIGNED_LOAD 0x4
#define CAUSE_LOAD_ACCESS 0x5
#define CAUSE_MISALIGNED_STORE 0x6
#define CAUSE_STORE_ACCESS 0x7
#define CAUSE_USER_ECALL 0x8
#define CAUSE_SUPERVISOR_ECALL 0x9
#define CAUSE_MACHINE_ECALL 0xB
#define CAUSE_INSTRUCTION_PAGE_FAULT 0xC
#define CAUSE_LOAD_PAGE_FAULT 0xD
#define CAUSE_STORE_PAGE_FAULT 0xF

// 中断号定义
#define IRQ_S_SOFT 1
#define IRQ_S_TIMER 5
#define IRQ_S_EXT 9

// 内存映射的定时器寄存器地址 (QEMU virt 平台)
#define CLINT_MTIMECMP 0x2004000

// 设置下一次定时器中断
void timer_set_next(void)
{
    // 设置下一次比较器值 (内存映射寄存器)
    uint64 next = r_time() + timer_scratch.interval;
    uint64 mcmp_addr = timer_scratch.mtimecmp; // 这是映射到 CLINT 的虚拟地址
    *(uint64 *)mcmp_addr = next;
    // 注意：MTIP 由硬件（CLINT）根据 mtime/mtimecmp 设置，软件不应直接清除 MTIP 位。
}

// 定时器初始化
void timer_init(void)
{
    // 每个CPU定时器中断间隔 (QEMU使用10MHz时钟)
    // 将默认间隔缩短以便测试更快看到中断（从 10,000,000 ~1s 缩短为 1,000,000 ~0.1s）
    uint64 interval = 10000; // 更短间隔以便在短测量内触发中断

    // 设置机器模式定时器向量
    w_mtvec((uint64)timervec);

    // 准备定时器 scratch 区域
    timer_scratch.saved0 = 0;
    timer_scratch.saved1 = 0;
    timer_scratch.saved2 = 0;
    timer_scratch.interval = interval;
    timer_scratch.mtimecmp = CLINT_MTIMECMP; // hart 0

    // 设置mscratch指向我们的scratch区域 (machine 模式使用物理地址，
    // 在这个内核实现中内核虚拟地址等于物理地址，所以直接写入虚拟地址即可)
    uint64 mscratch_pa = (uint64)&timer_scratch;
    w_mscratch(mscratch_pa);

    // 调试输出：打印 timer_scratch 布局信息（虚拟地址、物理 mscratch、interval 和 mtimecmp）
    printf("timer_init: mscratch_va=%p mscratch_pa=%p interval=%d mtimecmp_addr=%p\n", &timer_scratch, (void *)mscratch_pa, (int)timer_scratch.interval, (void *)timer_scratch.mtimecmp);

    // 启用机器模式定时器中断
    w_mie(r_mie() | MIE_MTIE);

    // 全局使能机器模式中断（允许 M-mode 接受中断）
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // 设置第一次定时器中断
    timer_set_next();
}

// 启用中断
void enable_interrupts(void)
{
    w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// 禁用中断
void disable_interrupts(void)
{
    w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// 定时器中断处理
void timer_interrupt_handler(void)
{
    // 如果是通过软件中断到达，清除 SSIP，避免重复触发
    csrc_sip(1UL << 1);
    // 增加ticks计数
    acquire(&tickslock);
    ticks++;

    // 只有在调试模式下才打印（打印会显著影响测量）
    if (timer_verbose)
        printf("timer_interrupt_handler: ticks=%d\n", (int)ticks);

    // wakeup(&ticks);
    release(&tickslock);

    // 触发调度
    // yield();
}

// 机器模式异常处理器：当在 M-mode 执行非法指令或访问错误时，timervec
// 会把非定时中断/异常分发到这里。此处理器打印信息并把 mepc 前进，
// 以便测试可以继续执行后续代码。
void machine_exception_handler(void)
{
    uint64 mcause = r_mcause();
    uint64 mepc = r_mepc();

    // 如果 mepc 为 0，说明异常发生位置不可用或无效，记录并忽略以避免误恢复
    if (mepc == 0)
    {
        printf("machine_exception_handler: unexpected mepc==0 mcause=%p - ignoring\n", mcause);
        return;
    }

    printf("machine_exception_handler: mcause=%p mepc=%p\n", mcause, mepc);

    // 如果是异常（而不是中断）
    if ((mcause & 0x8000000000000000L) == 0)
    {
        int code = mcause & 0xff;
        if (code == CAUSE_ILLEGAL_INSTRUCTION)
        {
            printf("trap: illegal instruction (M-mode) at %p\n", mepc);
            w_mepc(mepc + 4);
            mexception_count++;
            return;
        }
        else if (code == CAUSE_LOAD_PAGE_FAULT)
        {
            printf("trap: load page fault (M-mode) at %p\n", mepc);
            w_mepc(mepc + 4);
            mexception_count++;
            return;
        }
        else if (code == CAUSE_STORE_PAGE_FAULT)
        {
            printf("trap: store page fault (M-mode) at %p\n", mepc);
            w_mepc(mepc + 4);
            mexception_count++;
            return;
        }
        else
        {
            printf("machine unexpected exception: code=%d mepc=%p\n", code, mepc);
            w_mepc(mepc + 4);
            return;
        }
    }

    // 对于中断，暂不特殊处理，直接返回；timervec 的中断路径会继续
}

// common trap handling logic, callable from kerneltrap() and machine_exception_handler
void kerneltrap(void)
{
    uint64 scause = r_scause();
    uint64 sepc = r_sepc();

    // 调试输出：打印 scause/irq 信息，观察陷阱是否到达
    printf("kerneltrap: scause=%p sepc=%p\n", scause, sepc);

    if (scause & 0x8000000000000000L)
    {
        int irq = scause & 0xff;
        if (irq == IRQ_S_TIMER || irq == IRQ_S_SOFT)
        {
            // 监督模式定时器或软件中断
            timer_interrupt_handler();
        }
        else
        {
            ;
        }
    }
    else if (scause == CAUSE_USER_ECALL || scause == CAUSE_SUPERVISOR_ECALL)
    {
        // 系统调用
        printf("trap: system call\n");
        w_sepc(sepc + 4); // 前进到下一个指令
    }
    else if (scause == CAUSE_ILLEGAL_INSTRUCTION)
    {
        printf("trap: illegal instruction at %p\n", sepc);
        w_sepc(sepc + 4);
    }
    else if (scause == CAUSE_LOAD_PAGE_FAULT)
    {
        printf("trap: load page fault at %p\n", sepc);
        // 跳过导致异常的指令，避免死循环（测试环境中直接跳过以便继续执行后续测试）
        w_sepc(sepc + 4);
    }
    else if (scause == CAUSE_STORE_PAGE_FAULT)
    {
        printf("trap: store page fault at %p\n", sepc);
        // 跳过导致异常的指令，避免死循环（测试环境中直接跳过以便继续执行后续测试）
        w_sepc(sepc + 4);
    }
    else
    {
        printf("unexpected trap: scause %p sepc=%p\n", scause, sepc);
        for (;;)
            ;
    }

    // 设置下一次定时器中断
    timer_set_next();
}

// 中断初始化
void trap_init(void)
{
    // 初始化ticks锁
    initlock(&tickslock, "time");

    // 设置监督模式陷阱向量
    w_stvec((uint64)kernelvec);

    // 委托中断和异常给监督模式
    w_mideleg((1 << IRQ_S_TIMER) | (1 << IRQ_S_EXT) | (1 << IRQ_S_SOFT));

    // 委托异常给监督模式
    // 增加对非法指令的委托，这样在 S 模式执行非法指令时会进入 kerneltrap
    w_medeleg((1 << CAUSE_USER_ECALL) |
              (1 << CAUSE_BREAKPOINT) |
              (1 << CAUSE_ILLEGAL_INSTRUCTION) |
              (1 << CAUSE_INSTRUCTION_PAGE_FAULT) |
              (1 << CAUSE_LOAD_PAGE_FAULT) |
              (1 << CAUSE_STORE_PAGE_FAULT));

    // 初始化定时器中断
    timer_init();

    // 使能 supervisor 层的各类中断位 (允许被委托的中断在 S 模式下触发)
    // 这里设置 STIE/SSIE/SEIE，确保 supervisor 在 S 模式下可以接收软件/计时器/外部中断
    w_sie(r_sie() | SIE_STIE | SIE_SSIE | SIE_SEIE);

    printf("trap: interrupt system initialized\n");
    printf("trap: stvec = %p\n", kernelvec);
    printf("trap: mtvec = %p\n", timervec);
}