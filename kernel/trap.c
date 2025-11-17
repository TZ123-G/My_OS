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

// 全局变量定义
uint64 ticks = 0;
struct spinlock tickslock;

// 定时器相关结构 - 布局需与汇编 timervec 期望的偏移一致
// 汇编中：
//  sd a1, 0(a0)
//  sd a2, 8(a0)
//  sd a3, 16(a0)
//  ld a1, 24(a0)  # interval
//  ld a2, 32(a0)  # mtimecmp address (scratch)
// 因此 C 侧结构需要在前面保留 3 个 8 字节的槽，然后是 interval 和 mtimecmp 地址
struct timer_scratch
{
    uint64 reserved0; // offset 0
    uint64 reserved1; // offset 8
    uint64 reserved2; // offset 16
    uint64 interval;  // offset 24
    uint64 mtimecmp;  // offset 32
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
    // 读取回写入的值以验证
    uint64 readback = *(uint64 *)mcmp_addr;
    printf("timer_set_next: wrote next=%d readback=%d at addr=%p\n", (int)next, (int)readback, (void *)mcmp_addr);

    // 注意：MTIP 是由硬件（CLINT）根据 mtime/mtimecmp 设置的，软件不应直接清除 mip 中的 MTIP 位。
}

// 定时器初始化
void timer_init(void)
{
    // 每个CPU定时器中断间隔 (QEMU使用10MHz时钟)
    // 将默认间隔缩短以便测试更快看到中断（从 10,000,000 ~1s 缩短为 1,000,000 ~0.1s）
    uint64 interval = 1000000; // 快速测试用约0.1秒

    // 设置机器模式定时器向量
    w_mtvec((uint64)timervec);

    // 准备定时器scratch区域
    timer_scratch.reserved0 = 0;
    timer_scratch.reserved1 = 0;
    timer_scratch.reserved2 = 0;
    timer_scratch.interval = interval;
    // 对于单核/默认 hart 0，mtimecmp 地址就是 CLINT_MTIMECMP
    // 如果需要支持多核，应使用 hartid: CLINT_MTIMECMP + 8*hartid
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

    // 每100个tick打印一次（调试用）
    // 调试：每次中断都打印 ticks（便于观察）
    printf("timer_interrupt_handler: ticks=%d\n"); // 设置下一次定时器中断
    timer_set_next();
}

// kerneltrap: kernel-mode trap handler called from kernelvec.S
void kerneltrap(void)
{
    uint64 scause = r_scause();
    uint64 sepc = r_sepc();

    // 简单处理：打印并处理 supervisor 授权的中断（如 S-mode timer/software）
    if (scause & 0x8000000000000000L)
    {
        int irq = scause & 0xff;
        if (irq == IRQ_S_TIMER || irq == IRQ_S_SOFT)
        {
            timer_interrupt_handler();
            return;
        }
        else
        {
            printf("kerneltrap: unhandled interrupt irq=%d scause=%p sepc=%p\n", irq, scause, sepc);
            panic("kerneltrap: unhandled interrupt");
        }
    }
    else
    {
        printf("kerneltrap: exception scause=%p sepc=%p\n", scause, sepc);
        panic("kerneltrap: exception");
    }
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

// return-to-user path: set up sstatus/sepc/satp and jump to trampoline userret
void usertrapret(void)
{
    struct proc *p = myproc();

    // set S Previous Privilege mode to User, enable interrupts in user mode
    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
    x |= SSTATUS_SPIE; // enable interrupts in user mode
    w_sstatus(x);

    // set S Exception Program Counter to the saved user pc.
    w_sepc(p->trapframe->epc);

    // give the trampoline the user page table to switch to (satp in a0)
    uint64 satp = MAKE_SATP(p->pagetable);
    // compute address of trampoline userret entry
    extern char trampoline[];
    extern char userret[];
    uint64 userret_addr = (uint64)trampoline + ((uint64)userret - (uint64)trampoline);

    // jump to trampoline's userret with satp in a0
    register uint64 a0 asm("a0") = satp;
    register uint64 t1 asm("t1") = userret_addr;
    asm volatile("mv a0, %0; jr %1" : : "r"(a0), "r"(t1));
}
