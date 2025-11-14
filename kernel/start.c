#include "types.h"
#include "param.h"
#include "assert.h"
#include "memlayout.h"
#include "riscv.h"
#include "printf.h"
#include "defs.h"

extern char _bss_start[], _bss_end[];

void main();

// stack0 的值（地址）由链接器自动确定，其定义为数组的根本原因只是为了
// 在 .bss 段中分配一份足够大的空间作为栈空间而已
__attribute__((aligned(16))) char stack0[4096];

void uart_test()
{
    uartinit();
    uart_puts("\nHello, RISC-V OS!\n");
}

void printf_test()
{
    consoleinit();
    // 清屏
    clear_screen();

    // 测试光标定位
    goto_xy(5, 3); // 移动到第3行第5列
    printf("This text starts at (5,3)");
    // 测试颜色输出
    printf_color(COLOR_RED, COLOR_BLACK, "\nRed bold text on black background\n");
    printf_color(COLOR_GREEN, -1, "Green underlined text\n");
    printf_color(COLOR_BLUE, COLOR_YELLOW, "\nBlue blinking text on yellow background\n");

    // 测试清除行
    printf("This is a long line that will be partially cleared...");
    clear_line();
    printf("This is the new content after clearing the line\n");

    // 组合使用功能
    goto_xy(1, 10); // 移动到第10行第1列
    printf_color(COLOR_CYAN, -1, "Combined: positioned text with color");
}

void test_physical_memory(void)
{
    pmem_init();
    // 测试基本分配和释放
    void *page1 = alloc_page();
    void *page2 = alloc_page();
    assert(page1 != page2);
    assert(((uint64)page1 & 0xFFF) == 0); // 页对齐检查
    // 测试数据写入
    *(int *)page1 = 0x12345678;
    assert(*(int *)page1 == 0x12345678);
    // 测试释放和重新分配
    free_page(page1);
    void *page3 = alloc_page();
    // page3可能等于page1（取决于分配策略）
    free_page(page2);
    free_page(page3);
}

void test_pagetable(void)
{
    pmem_init();
    pagetable_t pt = create_pagetable();
    // 测试基本映射
    uint64 va = 0x1000000;
    uint64 pa = (uint64)alloc_page();
    assert(map_page(pt, va, pa, PTE_R | PTE_W) == 0);
    // 测试地址转换
    pte_t *pte = walk_lookup(pt, va);
    assert(pte != 0 && (*pte & PTE_V));
    assert(PTE2PA(*pte) == pa);
    // 测试权限位
    assert(*pte & PTE_R);
    assert(*pte & PTE_W);
    assert(!(*pte & PTE_X));
    printf("test success!");
}

void test_virtual_memory(void)
{
    printf("Before enabling paging...\n");

    // 初始化物理内存管理器
    pmem_init();

    // 启用分页
    kvminit();
    kvminithart();

    printf("After enabling paging...\n");

    // 测试内核代码仍然可执行
    // 通过调用一个函数来验证代码执行正常
    printf("Testing kernel code execution... ");
    consoleinit();
    printf("OK\n");

    // 测试内核数据仍然可访问
    // 通过访问全局变量来验证数据访问正常
    printf("Testing kernel data access... ");
    extern char end[];
    printf("end address: %p ", end);
    printf("OK\n");

    // 测试设备访问仍然正常
    // 通过调用UART函数来验证设备访问正常
    printf("Testing device access... ");
    uartinit();
    uart_putc('T');
    uart_putc('e');
    uart_putc('s');
    uart_putc('t');
    uart_putc('\n');
    printf("OK\n");

    // 测试内存分配和页表操作
    printf("Testing memory allocation and page mapping... ");
    void *page = alloc_page();
    assert(page != 0);
    printf("Allocated page at %p ", page);

    // 测试页表映射
    uint64 va = 0x2000000;    // 虚拟地址
    uint64 pa = (uint64)page; // 物理地址
    extern pagetable_t kernel_pagetable;
    assert(map_page(kernel_pagetable, va, pa, PTE_R | PTE_W | PTE_X) == 0);

    // 测试地址转换
    uint64 converted_pa = walkaddr(kernel_pagetable, va);
    assert(converted_pa == pa);

    printf("Page mapping and address translation OK\n");

    printf("Virtual memory test completed successfully!\n");
}

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
    extern volatile uint64 mexception_count;

    printf("Triggering illegal instruction (should be caught and resumed)...\n");
    volatile uint64 start = mexception_count;
    asm volatile(".word 0x0\n");
    // 等待 handler 更新计数器，带超时以避免永久阻塞
    for (int i = 0; i < 1000000 && mexception_count == start; i++)
        ;
    if (mexception_count != start)
        printf(" -> returned from illegal instruction handler\n");
    else
        printf(" -> timeout waiting for illegal instruction handler\n");
}

// 单独测试：写入 NULL（可能触发 store page fault）
void test_store_null(void)
{
    consoleinit();
    printf("Testing exception handling...\n");

    // 初始化中断/异常处理设施
    trap_init();
    extern volatile uint64 mexception_count;
    printf("Triggering store to NULL (may cause exit on some QEMU configs)...\n");
    volatile uint64 start = mexception_count;
    volatile int *bad = (int *)0x0;
    // 可能会导致 QEMU 终止模拟；我们依旧尝试并等待短时响应
    *bad = 0xdeadbeef;
    for (int i = 0; i < 1000000 && mexception_count == start; i++)
        ;
    if (mexception_count != start)
        printf(" -> returned from memory access handler\n");
    else
        printf(" -> timeout or no handler invoked for memory access\n");
}

// 单独测试：整型除以零
void test_divide_by_zero(void)
{
    consoleinit();
    printf("Testing exception handling...\n");

    // 初始化中断/异常处理设施
    trap_init();
    extern volatile uint64 mexception_count;
    printf("Triggering integer divide-by-zero (behavior depends on platform)...\n");
    volatile uint64 start = mexception_count;
    volatile int a = 1, b = 0, c = 0;
    // 执行除法，某些实现不会产生陷阱
    c = a / b;
    for (int i = 0; i < 1000000 && mexception_count == start; i++)
        ;
    if (mexception_count != start)
        printf(" -> divide-by-zero caused trap and was handled\n");
    else
        printf(" -> no trap observed for divide-by-zero\n");
    (void)c;
}

// 测量中断处理开销的测试
void test_interrupt_overhead(void)
{
    consoleinit();
    printf("Testing interrupt overhead...\n");

    // 初始化中断/异常系统
    trap_init();

    // 关闭定时器中断打印以免影响测量
    extern volatile int timer_verbose;
    timer_verbose = 0;

    // 确保 ticks 初始值
    extern uint64 ticks;
    ticks = 0;

    // 基线测量：不启用中断时的空循环耗时
    const int iter = 5000000;
    uint64 t0 = get_time();
    for (volatile int i = 0; i < iter; i++)
        asm volatile("" ::: "memory");
    uint64 t1 = get_time();
    uint64 baseline = t1 - t0;
    printf("Baseline: %d iterations took %d cycles\n", iter, (int)baseline);

    // 给定时器一点时间确保 mtimecmp 已写入并生效，然后启用中断并测量
    for (volatile int w = 0; w < 1000000; w++)
        ;
    // 启用中断并测量同样循环的耗时，同时统计中断次数
    enable_interrupts();
    uint64 start_ticks = ticks;
    uint64 t2 = get_time();
    for (volatile int i = 0; i < iter; i++)
        asm volatile("" ::: "memory");
    uint64 t3 = get_time();
    uint64 end_ticks = ticks;
    uint64 elapsed = t3 - t2;
    uint64 irq_count = (end_ticks >= start_ticks) ? (end_ticks - start_ticks) : 0;

    printf("With interrupts enabled: %d cycles, interrupts=%d\n", (int)elapsed, (int)irq_count);
    if (irq_count > 0)
    {
        int avg_overhead = (int)((elapsed > baseline) ? ((elapsed - baseline) / irq_count) : 0);
        printf("Estimated average overhead per interrupt: %d cycles\n", avg_overhead);
    }
    else
    {
        printf("No interrupts observed during workload. Try increasing runtime or reducing interval.\n");
    }

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

    // 仅调用异常测试函数；start 仅负责调用这个测试函数
    test_interrupt_overhead();

    main();
}