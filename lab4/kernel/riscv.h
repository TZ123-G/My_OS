#ifndef TYPES_H
#define TYPES_H
#include "types.h"
#define PGSIZE 4096 // bytes per page
#define PGSHIFT 12  // bits of offset within a page

#define PGROUNDUP(sz) (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))
// 页表项标志位
#define PTE_V (1L << 0) // 有效位
#define PTE_R (1L << 1) // 可读
#define PTE_W (1L << 2) // 可写
#define PTE_X (1L << 3) // 可执行
#define PTE_U (1L << 4) // 用户可访问

// 页表项操作
#define PTE2PA(pte) (((pte) >> 10) << 12)
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// Sv39 相关
#define SATP_SV39 (8L << 60)
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// Supervisor Status Register, sstatus
#define SSTATUS_SPP (1L << 8)  // Previous mode, 1=Supervisor, 0=User
#define SSTATUS_SPIE (1L << 5) // Supervisor Previous Interrupt Enable
#define SSTATUS_UPIE (1L << 4) // User Previous Interrupt Enable
#define SSTATUS_SIE (1L << 1)  // Supervisor Interrupt Enable
#define SSTATUS_UIE (1L << 0)  // User Interrupt Enable
// Machine status MIE (global machine interrupt enable)
#define MSTATUS_MIE (1L << 3)

// 定时器中断
#define MIE_MTIE (1L << 7)
static inline uint64
r_sstatus(void)
{
    uint64 x;
    asm volatile("csrr %0, sstatus" : "=r"(x));
    return x;
}

static inline void
w_sstatus(uint64 x)
{
    asm volatile("csrw sstatus, %0" : : "r"(x));
}
// 寄存器读写函数
static inline uint64
r_satp()
{
    uint64 x;
    asm volatile("csrr %0, satp" : "=r"(x));
    return x;
}
static inline void
sfence_vma()
{
    asm volatile("sfence.vma zero, zero");
}

// CSR寄存器读写
static inline uint64
r_mstatus(void)
{
    uint64 x;
    asm volatile("csrr %0, mstatus" : "=r"(x));
    return x;
}

static inline void
w_mstatus(uint64 x)
{
    asm volatile("csrw mstatus, %0" : : "r"(x));
}

static inline void
w_mepc(uint64 x)
{
    asm volatile("csrw mepc, %0" : : "r"(x));
}

static inline uint64
r_mepc(void)
{
    uint64 x;
    asm volatile("csrr %0, mepc" : "=r"(x));
    return x;
}

static inline void
w_satp(uint64 x)
{
    asm volatile("csrw satp, %0" : : "r"(x));
}

static inline void
w_medeleg(uint64 x)
{
    asm volatile("csrw medeleg, %0" : : "r"(x));
}

static inline uint64
r_medeleg(void)
{
    uint64 x;
    asm volatile("csrr %0, medeleg" : "=r"(x));
    return x;
}

static inline void
w_mideleg(uint64 x)
{
    asm volatile("csrw mideleg, %0" : : "r"(x));
}

static inline uint64
r_mideleg(void)
{
    uint64 x;
    asm volatile("csrr %0, mideleg" : "=r"(x));
    return x;
}

static inline void
w_mie(uint64 x)
{
    asm volatile("csrw mie, %0" : : "r"(x));
}

static inline void
w_mtvec(uint64 x)
{
    asm volatile("csrw mtvec, %0" : : "r"(x));
}

static inline void
w_stvec(uint64 x)
{
    asm volatile("csrw stvec, %0" : : "r"(x));
}

static inline uint64
r_scause(void)
{
    uint64 x;
    asm volatile("csrr %0, scause" : "=r"(x));
    return x;
}

static inline uint64
r_sepc(void)
{
    uint64 x;
    asm volatile("csrr %0, sepc" : "=r"(x));
    return x;
}

static inline void
w_sepc(uint64 x)
{
    asm volatile("csrw sepc, %0" : : "r"(x));
}

static inline uint64
r_mip(void)
{
    uint64 x;
    asm volatile("csrr %0, mip" : "=r"(x));
    return x;
}

static inline void
w_mip(uint64 x)
{
    asm volatile("csrw mip, %0" : : "r"(x));
}

// mtimecmp 是内存映射寄存器，不是CSR
// 通过内存地址访问
#define MTIMECMP 0x2004000

// 中断控制
static inline void
intr_on(void)
{
    w_sstatus(r_sstatus() | SSTATUS_SIE);
}

static inline void
intr_off(void)
{
    w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

static inline int
intr_get(void)
{
    uint64 x = r_sstatus();
    return (x & SSTATUS_SIE) != 0;
}
static inline uint64
get_time(void)
{
    uint64 x;
    asm volatile("rdtime %0" : "=r"(x));
    return x;
}

// 添加 r_time 函数
static inline uint64
r_time(void)
{
    uint64 x;
    asm volatile("rdtime %0" : "=r"(x));
    return x;
}

// 添加 w_mscratch 函数
static inline void
w_mscratch(uint64 x)
{
    asm volatile("csrw mscratch, %0" : : "r"(x));
}

// 读取 mcause CSR
static inline uint64
r_mcause(void)
{
    uint64 x;
    asm volatile("csrr %0, mcause" : "=r"(x));
    return x;
}

// 添加 r_mie 函数
static inline uint64
r_mie(void)
{
    uint64 x;
    asm volatile("csrr %0, mie" : "=r"(x));
    return x;
}

// Supervisor interrupt-enable bits in SIE CSR
#define SIE_SSIE (1L << 1)
#define SIE_STIE (1L << 5)
#define SIE_SEIE (1L << 9)

// 读写 SIE CSR
static inline uint64
r_sie(void)
{
    uint64 x;
    asm volatile("csrr %0, sie" : "=r"(x));
    return x;
}

static inline void
w_sie(uint64 x)
{
    asm volatile("csrw sie, %0" : : "r"(x));
}

// clear bits in sip CSR
static inline void
csrc_sip(uint64 x)
{
    asm volatile("csrc sip, %0" : : "r"(x));
}
#endif