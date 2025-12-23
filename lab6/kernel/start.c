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
#include "fs.h"

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

// 适配内核的参数传递测试：通过伪造 proc/trapframe 调用 syscall()
void test_parameter_passing(void)
{
    printf("Running test_parameter_passing...\n");
    uartinit();
    consoleinit();
    pmem_init();
    kvminit();
    procinit();

    struct trapframe tf;
    struct proc fakep;
    memset(&tf, 0, sizeof(tf));
    memset(&fakep, 0, sizeof(fakep));
    fakep.trapframe = &tf;
    extern pagetable_t kernel_pagetable;
    fakep.pagetable = kernel_pagetable;
    fakep.pid = 2;
    strncpy(fakep.name, "ptest", sizeof(fakep.name));

    struct proc *old = myproc();
    setproc(&fakep);

    static char buffer[] = "Hello, World!\n";
    static char path[] = "/dev/console";

    // open
    tf.a7 = SYS_open;
    tf.a0 = (uint64)path;
    tf.a1 = 0; // flags (ignored in our simple sys_open)
    syscall();
    int fd = (int)tf.a0;
    printf("open returned %d\n", fd);

    if (fd >= 0)
    {
        // write
        tf.a7 = SYS_write;
        tf.a0 = fd;
        tf.a1 = (uint64)buffer;
        tf.a2 = (int)strlen(buffer);
        syscall();
        printf("write returned %d\n", (int)tf.a0);

        // close
        tf.a7 = SYS_close;
        tf.a0 = fd;
        syscall();
        printf("close returned %d\n", (int)tf.a0);
    }

    // boundary cases
    // invalid fd
    tf.a7 = SYS_write;
    tf.a0 = -1;
    tf.a1 = (uint64)buffer;
    tf.a2 = 10;
    syscall();
    printf("write(-1, buffer, 10) = %d\n", (int)tf.a0);

    // NULL pointer
    tf.a7 = SYS_write;
    tf.a0 = fd >= 0 ? fd : 1;
    tf.a1 = 0;
    tf.a2 = 10;
    syscall();
    printf("write(fd, NULL, 10) = %d\n", (int)tf.a0);

    // negative length
    tf.a7 = SYS_write;
    tf.a0 = fd >= 0 ? fd : 1;
    tf.a1 = (uint64)buffer;
    tf.a2 = -1;
    syscall();
    printf("write(fd, buffer, -1) = %d\n", (int)tf.a0);

    setproc(old);
}

// 适配内核的安全性测试：无效用户指针、缓冲区边界与权限检查
void test_security(void)
{
    printf("Running test_security...\n");
    uartinit();
    consoleinit();
    pmem_init();
    kvminit();
    procinit();

    struct trapframe tf;
    struct proc fakep;
    memset(&tf, 0, sizeof(tf));
    memset(&fakep, 0, sizeof(fakep));
    fakep.trapframe = &tf;
    extern pagetable_t kernel_pagetable;
    fakep.pagetable = kernel_pagetable;
    fakep.pid = 3;
    strncpy(fakep.name, "scktest", sizeof(fakep.name));

    struct proc *old = myproc();
    setproc(&fakep);

    // invalid pointer write
    char *invalid_ptr = (char *)0x1000000;
    tf.a7 = SYS_write;
    tf.a0 = 1;
    tf.a1 = (uint64)invalid_ptr;
    tf.a2 = 10;
    syscall();
    printf("Invalid pointer write result: %d\n", (int)tf.a0);

    // read into too-small buffer
    char small_buf[4];
    tf.a7 = SYS_read;
    tf.a0 = 0;
    tf.a1 = (uint64)small_buf;
    tf.a2 = 1000;
    syscall();
    printf("read(0, small_buf, 1000) = %d\n", (int)tf.a0);

    // permission check: open as read-only then try write
    static char cons[] = "/dev/console";
    tf.a7 = SYS_open;
    tf.a0 = (uint64)cons;
    tf.a1 = 0; // O_RDONLY (not enforced by our sys_open)
    syscall();
    int rfd = (int)tf.a0;
    printf("open(/dev/console,O_RDONLY) = %d\n", rfd);
    if (rfd >= 0)
    {
        tf.a7 = SYS_write;
        tf.a0 = rfd;
        tf.a1 = (uint64) "X";
        tf.a2 = 1;
        syscall();
        printf("write(readonly_fd, \"X\",1) = %d\n", (int)tf.a0);

        tf.a7 = SYS_close;
        tf.a0 = rfd;
        syscall();
    }

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

void test_filesystem_integrity(void)
{
    consoleinit();
    printf("Testing filesystem integrity...\n");

    fs_init();
    fileinit();

    // 直接使用内核 inode 接口创建并测试读写
    struct inode *ip = ialloc(0, 1); // type=1 (file/dir)
    assert(ip != 0);

    char wbuf[] = "Hello, filesystem!";
    int wlen = strlen(wbuf);
    int written = writei(ip, wbuf, 0, wlen);
    if (written != wlen)
    {
        printf("write failed: wrote %d expected %d\n", written, wlen);
        panic("filesystem test write failed");
    }

    char rbuf[64];
    int read = readi(ip, rbuf, 0, sizeof(rbuf) - 1);
    if (read < 0)
    {
        printf("read failed\n");
        panic("filesystem test read failed");
    }
    rbuf[read] = '\0';

    if (strcmp(wbuf, rbuf) != 0)
    {
        printf("mismatch: wrote '%s' read '%s'\n", wbuf, rbuf);
        panic("filesystem contents mismatch");
    }

    // 模拟删除：清除 inode 标记并释放引用
    ip->valid = 0;
    iput(ip);

    printf("Filesystem integrity test passed\n");
}

void concurrent_worker(void)
{
    // each worker will allocate/write/read/free inodes repeatedly
    struct proc *p = myproc();
    int pid = p ? p->pid : 0;
    printf("worker %d: started\n", pid);
    for (int j = 0; j < 100; j++)
    {
        struct inode *ip = ialloc(0, 1);
        if (!ip)
        {
            // allocation failed, back off and continue
            for (volatile int w = 0; w < 1000; w++)
                ;
            continue;
        }
        // write an int
        int val = j;
        int wrote = writei(ip, (char *)&val, 0, sizeof(val));
        if (wrote != sizeof(val))
        {
            printf("worker %d: write error at iter %d wrote=%d\n", pid, j, wrote);
        }
        // read it back
        int r = 0;
        int rd = readi(ip, (char *)&r, 0, sizeof(r));
        if (rd == sizeof(r) && r != val)
        {
            printf("worker %d: mismatch iter %d %d!=%d\n", pid, j, r, val);
        }
        // simulate unlink
        ip->valid = 0;
        iput(ip);
    }
    printf("worker %d: done\n", pid);
    exit_process(0);
}

void test_concurrent_access(void)
{
    consoleinit();
    printf("Testing concurrent file access...\n");

    pmem_init();
    procinit();
    // trap_init();
    // enable_interrupts();

    const int nworkers = 4;
    for (int i = 0; i < nworkers; i++)
    {
        int pid = create_process(concurrent_worker);
        if (pid <= 0)
            printf("test_concurrent_access: create_process failed for worker %d\n", i);
    }

    // enter scheduler to run workers; this does not return
    scheduler();

    // unreachable
}

// 文件系统调试信息打印：按用户需求实现
void debug_filesystem_state(void)
{
    printf("=== Filesystem Debug Info ===\n");
    // 初始化必要子系统以确保 API 可用
    fs_init();
    fileinit();

    // 超级块信息
    struct superblock sb;
    read_superblock(&sb);
    printf("Total blocks: %d\n", (int)sb.size);

    // 统计空闲块与空闲 inode
    printf("Free blocks: %d\n", count_free_blocks());
    printf("Free inodes: %d\n", count_free_inodes());

    // 缓存命中统计（读取器会在使用 bread 时变化）
    printf("Buffer cache hits: %d\n", buffer_cache_hits());
    printf("Buffer cache misses: %d\n", buffer_cache_misses());
}
// 新的测试函数：磁盘 I/O 统计打印
void debug_disk_io(void)
{
    printf("=== Disk I/O Statistics ===\n");
    // 可选：确保子系统初始化
    fs_init();
    fileinit();
    printf("Disk reads: %d\n", disk_read_count());
    printf("Disk writes: %d\n", disk_write_count());
}

void new_debug_disk_io(void)
{
    printf("=== Disk I/O Statistics ===\n");
    // 可选：确保子系统初始化
    fs_init();
    fileinit();
    // 触发一次读（选择一个未加载过的块）
    struct superblock sb;
    read_superblock(&sb);
    uint sample = sb.bmapstart + 1;
    struct buf *b = bread(0, sample);
    brelse(b);
    // 触发一次写（分配 inode 并写入一个整数）
    struct inode *ip = ialloc(0, 1);
    if (ip)
    {
        int val = 42;
        writei(ip, (char *)&val, 0, sizeof(val));
        // 不立即 iput，以便观察 ref>0 的情况；这里简洁起见不打印 inode 信息
    }
    printf("Disk reads: %d\n", disk_read_count());
    printf("Disk writes: %d\n", disk_write_count());
}

// 新的测试函数：改写 inode 使用情况打印
void fs_inode_usage(void)
{
    printf("=== Inode Usage ===\n");
    // 确保文件系统初始化
    fs_init();
    fileinit();
    int n = fs_inode_count();
    for (int i = 0; i < n; i++)
    {
        struct inode *ip = fs_inode_at(i);
        if (!ip)
            continue;
        if (ip->ref > 0)
        {
            printf("Inode %d: ref=%d, type=%d, size=%d\n",
                   (int)ip->inum, (int)ip->ref, (int)ip->type, (int)ip->size);
        }
    }
}

// 适配内核的文件系统性能测试：使用 inode 层 API，无文件名
void test_filesystem_performance(void)
{
    consoleinit();
    printf("Testing filesystem performance...\n");

    // 初始化文件系统与文件层
    fs_init();
    fileinit();

    // 大量小“文件”（inode）测试：每次分配一个 inode，写入 4 字节，再释放
    uint64 start_time = get_time();
    const int small_n = 1000;
    const char small_data[4] = {'t', 'e', 's', 't'};
    for (int i = 0; i < small_n; i++)
    {
        struct inode *ip = ialloc(0, 1); // type=1：文件
        if (!ip)
        {
            // 分配失败则跳过，避免测试中断
            continue;
        }
        // 写入 4 字节
        int wrote = writei(ip, (char *)small_data, 0, sizeof(small_data));
        (void)wrote; // 测试场景不强制校验返回值
        // 释放并模拟“unlink”
        ip->valid = 0;
        iput(ip);
    }
    uint64 small_files_time = get_time() - start_time;

    // 大文件测试：同一个 inode 连续写入 4KB * 1024 = 4MB
    start_time = get_time();
    struct inode *large = ialloc(0, 1);
    if (large)
    {
        char *large_buffer = (char *)alloc_page();
        if (large_buffer)
        {
            // 缓冲区内容无需特定值，保持未初始化即可
            for (int i = 0; i < 1024; i++)
            {
                // 每次写 4KB，偏移递增
                writei(large, large_buffer, i * BSIZE, BSIZE);
            }
            free_page(large_buffer);
        }
        // 释放并模拟“unlink”
        large->valid = 0;
        iput(large);
    }
    uint64 large_file_time = get_time() - start_time;

    printf("Small files (1000x4B): %d cycles\n", (int)small_files_time);
    printf("Large file (1x4MB): %d cycles\n", (int)large_file_time);
}

// Crash recovery test: 可直接在 start() 中调用
#define T_FILE 1

void test_crash_recovery(void)
{
    consoleinit();
    printf("=== Crash recovery test (kernel) ===\n");

    fs_init();
    fileinit();

    const int NUM = 100; // 可按需调整
    char name[64];
    const char *payload = "crash-test-data\n";
    int errors = 0;

    // 如果第一个文件已存在，认为是重启后的检查阶段
    if (namei("/crashf0") != 0)
    {
        printf("Post-crash: verifying %d files...\n", NUM);
        for (int i = 0; i < NUM; i++)
        {
            snprintf(name, sizeof(name), "/crashf%d", i);
            struct inode *ip = namei(name);
            if (!ip)
            {
                printf("MISSING %s\n", name);
                errors++;
                continue;
            }
            char buf[128];
            int n = readi(ip, buf, 0, sizeof(buf) - 1);
            iput(ip);
            if (n <= 0 || strstr(buf, "crash-test") == 0)
            {
                printf("CORRUPT %s (read=%d)\n", name, n);
                errors++;
            }
        }
        if (errors == 0)
            printf("RECOVERY OK: all %d files present and readable\n", NUM);
        else
            printf("RECOVERY FAIL: %d errors\n", errors);
        return;
    }

    // 首次运行：创建大量文件并写入，然后触发 panic 模拟崩溃
    printf("First run: creating %d files then triggering panic...\n", NUM);
    for (int i = 0; i < NUM; i++)
    {
        snprintf(name, sizeof(name), "/crashf%d", i);
        struct inode *ip = create(name, T_FILE);
        if (!ip)
        {
            printf("create failed %s\n", name);
            continue;
        }
        writei(ip, (char *)payload, 0, (int)strlen(payload));
        iput(ip);
        if ((i & 15) == 0)
        {
            printf(".");
        }
    }
    printf("\nFiles created. Triggering panic now to simulate crash.\n");
    panic("test_crash_recovery: user-triggered crash");
}

void start()
{
    // 清零 .bss 段
    for (char *p = _bss_start; p < _bss_end; p++)
    {
        *p = 0;
    }

    // Run syscall-related tests before crash recovery to ensure they execute
    test_parameter_passing();
    test_security();


    main();
}
