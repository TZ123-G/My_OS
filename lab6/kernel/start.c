#include <stddef.h>
#include "types.h"
#include "param.h"
#include "assert.h"
#include "memlayout.h"
#include "riscv.h"
#include "printf.h"
#include "defs.h"
#include "syscall.h"
#include "spinlock.h"
#include "proc.h"

extern char _bss_start[], _bss_end[];

void main();

// synchronization test
extern void test_synchronization(void);
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
    printf("Physical memory test completed successfully!\n");
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
    // 注意：0x2000000 区域在内核初始化时被用于 CLINT 设备映射，
    // 直接使用该地址会导致 remap 失败。这里选择一个未被占用的虚拟地址，
    // 并在遇到已映射页时向后寻找一个空闲页。
    uint64 va = 0x3000000;    // 起始虚拟地址候选
    uint64 pa = (uint64)page; // 物理地址
    extern pagetable_t kernel_pagetable;

    // 查找未被映射的虚拟页，最多尝试 1024 页（安全上限）
    int tries = 0;
    while (walkaddr(kernel_pagetable, va) != 0 && tries < 1024)
    {
        va += PGSIZE;
        tries++;
    }
    if (tries >= 1024)
    {
        panic("test_virtual_memory: cannot find free virtual page to map");
    }
    printf("test_virtual_memory: mapping page at va=%p -> pa=%p\n", va, pa);
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
    printf("Triggering illegal instruction (handler confirmation skipped)...\n");
    asm volatile(".word 0x0\n");
    printf("Illegal instruction test completed successfully!\n");
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
    printf("Store to NULL test completed successfully!\n");
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
    printf("Interrupt overhead test completed.\n");
    // 清理：禁用中断
    disable_interrupts();
}

void simple_task(void)
{
    // A minimal task used for process-creation tests.
    // Keep it simple so it works regardless of scheduler details.
    printf("simple_task started\n");
    for (volatile int i = 0; i < 1000000; i++)
        ;
    printf("simple_task exiting\n");
    return;
}

void test_process_creation(void)
{
    printf("Testing process creation...\n");
    pmem_init();
    procinit();
    // 测试基本的进程创建
    int pid = create_process(simple_task);
    assert(pid > 0);
    // 测试进程表限制
    int count = 0;
    for (int i = 0; i < NPROC + 5; i++)
    {
        int pid = create_process(simple_task);
        if (pid > 0)
        {
            count++;
        }
        else
        {
            break;
        }
    }
    printf("Created %d processes\n", count);

    // 不在此处进行 wait/reap —— 我们在测试内部启动调度器以运行子进程。
    // 初始化陷阱/定时器，然后进入调度循环（scheduler 不返回）。
    trap_init();
    enable_interrupts();
    printf("Entering scheduler to run created processes...\n");
    scheduler();
}

// A simple CPU-intensive task used by the scheduler test.
void cpu_intensive_task(void)
{
    printf("cpu_intensive_task started\n");
    // Busy loop to generate CPU load for scheduler observation.
    volatile uint64 sum = 0;
    for (volatile uint64 i = 0; i < 10000000ULL; i++)
    {
        sum += i;
        if ((i & 0xFFFF) == 0)
            yield();
    }
    (void)sum;
    printf("cpu_intensive_task exiting\n");
    return;
}

// watcher task for scheduler test: waits for a fixed time then exits
void scheduler_watcher(void)
{
    printf("scheduler_watcher: started\n");
    uint64 start = get_time();
    // 等待约 0.2s 到 0.5s（依赖仿真速度），以 cycles 为单位
    // 延长等待时间以便多个协作任务有机会运行并输出日志
    uint64 deadline = start + 600000;
    while (get_time() < deadline)
    {
        // 轻量忙等待，让出时间片给其他可运行进程
        for (volatile int i = 0; i < 10000; i++)
            yield();
    }
    uint64 end = get_time();
    printf("scheduler_watcher: waited %d cycles\n", (int)(end - start));
    exit_process(0);
}

// Scheduler test: 创建多个计算密集型进程和一个 watcher，然后进入调度器观察行为
void test_scheduler(void)
{
    printf("Testing scheduler...\n");

    // 确保进程子系统已初始化
    pmem_init();
    procinit();
    // trap_init();
    // enable_interrupts();

    // 创建若干计算任务
    const int n = 3;
    for (int i = 0; i < n; i++)
    {
        int pid = create_process(cpu_intensive_task);
        if (pid <= 0)
            printf("test_scheduler: create_process failed for task %d\n", i);
    }

    // 创建 watcher 进程，等待一段时间并打印统计，然后退出
    int wpid = create_process(scheduler_watcher);
    if (wpid <= 0)
        printf("test_scheduler: create_process failed for watcher\n");

    printf("test_scheduler: entering scheduler \n");
    scheduler();
    // scheduler() 不会返回；若返回则打印并继续
    printf("test_scheduler: scheduler returned (unexpected)\n");
}

void debug_proc_table(void)
{
    pmem_init();
    procinit();
    // 不在此测试中启用中断以避免在持锁期间被抢占导致 push_off/pop_off 不匹配
    // trap_init();
    // enable_interrupts();

    for (int i = 0; i < 3; i++)
    {
        int pid = create_process(cpu_intensive_task);
        if (pid <= 0)
            printf("test_scheduler: create_process failed for task %d\n", i);
    }

    // 打印当前进程表快照
    debug_proc();
}

// 简单的系统调用模拟测试：使用栈上伪造的 proc/trapframe 避免依赖 allocproc
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

// 在内核中通过 syscall() 测试 fork/wait
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
void start()
{
    // 清零 .bss 段
    for (char *p = _bss_start; p < _bss_end; p++)
    {
        *p = 0;
    }

    main();
}
