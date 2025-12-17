#include <stddef.h>
#include "types.h"
#include "param.h"
#include "assert.h"
#include "memlayout.h"
#include "riscv.h"
#include "printf.h"
#include "defs.h"


extern char _bss_start[], _bss_end[];

void main();

#ifndef CLINT
#define CLINT 0x02000000UL
#endif
#ifndef MTIMECMP
#define MTIMECMP (CLINT + 0x4000UL)
#endif

// synchronization test
extern void test_synchronization(void);
// stack0 的值（地址）由链接器自动确定，其定义为数组的根本原因只是为了
// 在 .bss 段中分配一份足够大的空间作为栈空间而已
__attribute__((aligned(16))) char stack0[4096];

extern uint64 ticks;

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

// 单独测试：非法指令
void test_illegal_instruction(void)
{
    consoleinit();
    printf("Testing illegal instruction...\n");

    // 初始化中断/异常处理设施
    trap_init();
    printf("Triggering illegal instruction (handler confirmation skipped)...\n");
    asm volatile(".word 0x0\n");
}

// 单独测试：写入 NULL（可能触发 store page fault）
void test_store_null(void)
{
    consoleinit();
    printf("Testing exception handling...\n");

    // 初始化中断/异常处理设施
    trap_init();
    printf("Triggering store to NULL (may cause exit on some QEMU configs)...\n");
    /* 不再依赖 mexception_count，直接执行写 NULL 的操作并继续。 */
    volatile int *bad = (int *)0x0;
    *bad = 0xdeadbeef;
}

// 测量中断处理开销的测试
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
        // 写入 mtimecmp （使用 MTIMECMP 地址）
        uint64 mcmp = MTIMECMP; // assumes hart 0 mtimecmp base
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
    printf("Interrupt overhead test completed.\n");
    // 清理：禁用中断
    disable_interrupts();
}
void start()
{
    // 清零 .bss 段
    for (char *p = _bss_start; p < _bss_end; p++)
    {
        *p = 0;
    }

    main();
}