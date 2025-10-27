# 实验一：RISC-V引导与裸机启动

文件列表如下：
```text
.
├── kernel
│   ├── defs.h
│   ├── types.h
│   ├── entry.S
│   ├── start.c
│   ├── main.c
│   └── uart.c
├── kernel.ld
└── Makefile
```
## 1、entry.S
entry.S 文件包含启动代码，如设置栈指针、调用 C 函数等。作为操作系统的入口汇编代码，负责最基本的初始化工作，因为我的os版本只需要支持单核cpu所以分配了固定的栈空间4KB，而不像xv6需要支持多核cpu，所以分配的栈空间会根据cpu核数动态分配。
```S
.section .text
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        la sp, stack0
        li a0, 1024*4
        add sp, sp, a0
        # jump to start() in start.c
        call start
spin:
        j spin
```
## 2、kernel.ld
kernel.ld 文件是链接器脚本，用于定义内存布局和链接对象，控制各段的排列顺序与地址分配。我的os中去除了trampoline的部分，因为我暂时还未实现用户态和内核态之间的转换问题
## 3、defs.h
defs.h 文件声明了各模块的接口和部分常量的定义，以方便模块的互相调用/
## 4、types.h
types.h 文件为内核提供标准化的数据类型定义。
## 5、start.c
start.c 文件是内核的入口，负责初始化内存、初始化设备、调用 main 函数。实现了栈空间的分配和.bss段的清零，以及硬件的初始化，我的测试相关代码也放在了start.c文件中。
```c
#include "defs.h"

extern char _bss_start[], _bss_end[];

void main();

// stack0 的值（地址）由链接器自动确定，其定义为数组的根本原因只是为了
// 在 .bss 段中分配一份足够大的空间作为栈空间而已
__attribute__((aligned(16))) char stack0[4096];

void start()
{
    // 清零 .bss 段
    for (char *p = _bss_start; p < _bss_end; p++)
    {
        *p = 0;
    }
    uartinit();

    uart_puts("Hello OS!\n");

    main();
}
```
## 6、uart.c
uart.c 文件是UART串口通信的底层驱动程序。配置UART寄存器直接套用了xv6的源码。在第一个任务中，我只实现了uart_puts函数这个基本的字符串输出的功能，通过遍历字符串，逐个调用uart_putc函数输出字符，uart_putc函数中，通过写UART寄存器将字符输出到串口。
```c
#include "types.h"

#define UART0 0x10000000L
#define UART0_IRQ 10
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0 // receive holding register (for input bytes)
#define THR 0 // transmit holding register (for output bytes)
#define IER 1 // interrupt enable register
#define IER_RX_ENABLE (1 << 0)
#define IER_TX_ENABLE (1 << 1)
#define FCR 2 // FIFO control register
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR (3 << 1) // clear the content of the two FIFOs
#define ISR 2                   // interrupt status register
#define LCR 3                   // line control register
#define LCR_EIGHT_BITS (3 << 0)
#define LCR_BAUD_LATCH (1 << 7) // special mode to set baud rate
#define LSR 5                   // line status register
#define LSR_RX_READY (1 << 0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1 << 5)    // THR can accept another character to send

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

void uartputc_sync(int c)
{
    volatile uint8 *lsr = (volatile uint8 *)(UART0 + LSR);

    // 等待发送寄存器为空
    while ((*lsr & LSR_TX_IDLE) == 0)
        ;

    volatile uint8 *thr = (volatile uint8 *)(UART0 + THR);
    *thr = c;
}

void uartinit(void)
{
    // disable interrupts.
    WriteReg(IER, 0x00);

    // special mode to set baud rate.
    WriteReg(LCR, LCR_BAUD_LATCH);

    // LSB for baud rate of 38.4K.
    WriteReg(0, 0x03);

    // MSB for baud rate of 38.4K.
    WriteReg(1, 0x00);

    // leave set-baud mode,
    // and set word length to 8 bits, no parity.
    WriteReg(LCR, LCR_EIGHT_BITS);

    // reset and enable FIFOs.
    WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

    // enable transmit and receive interrupts.
    WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

    // initlock(&tx_lock, "uart");
}

void uart_putc(char c)
{
    while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
        ;

    WriteReg(THR, c);
}

void uart_puts(char *s)
{
    while (*s)
    {
        uart_putc(*s);
        s++;
    }
}
```
## 7、main.c
main.c 文件是操作系统内核主程序的入口文件，在内核初始化完成之后运行，在我的代码中测试相关的代码已经在start.c初始化中实现所以main就仅包含了一个空函数。

# 实验二：内核printf与清屏功能实现
文件列表如下：
```text
.
├── kernel
│   ├── defs.h
│   ├── types.h
│   ├── entry.S
│   ├── printf.c
│   ├── console.c
│   ├── start.c
│   ├── main.c
│   └── uart.c
├── kernel.ld
└── Makefile
```
## 1、新增各模块关系
printf提供解析格式字符串转换为字符串的功能，console提供控制台输出、统一的字符输出接口，uart提供串口输出功能。三层的架构更利于功能的维护与拓展
## 2、console.c
console.c提供控制台输出功能，通过uart.c的uart_putc()实现。首先consputc函数通过uart_putc()将字符输出到串口，我直接搬的源码。
```c
void
consputc(int c)
{
  if(c == BACKSPACE){
    // 如果用户输入退格，用空格覆盖并再次退格
    uart_putc('\b');
    uart_putc(' ');
    uart_putc('\b');
  } else {
    uart_putc(c);
  }
}
```
而控制台的初始化也就是通过uart_init()函数来初始化UART硬件。
```c
void
consoleinit(void)
{
  uartinit();
}
```
通过ANSI转义序列来实现清屏功能 \033[2J  清除整个屏幕  \033[H 光标回到左上角
```c
void clear_screen(void)
{
  // \033[2J 清屏，\033[H 将光标移动到左上角
  consputc('\033');
  consputc('[');
  consputc('2');
  consputc('J');

  consputc('\033');
  consputc('[');
  consputc('H');
}
```
使用ANSI转义序列 \033[y;xH 实现光标定位,将输入的数字转换为字符进行输出。
```c
void
goto_xy(int x, int y)
{
  // ANSI 转义序列: \033[y;xH
  consputc('\033');
  consputc('[');
  
  // 转换行号
  if(y >= 10) {
    consputc('0' + y/10);
    consputc('0' + y%10);
  } else {
    consputc('0' + y);
  }
  
  consputc(';');
  
  // 转换列号
  if(x >= 10) {
    consputc('0' + x/10);
    consputc('0' + x%10);
  } else {
    consputc('0' + x);
  }
  
  consputc('H');
}
```
使用ANSI转义序列 \033[2K 清除整行，发送回车符 \r 将光标移回行首
```c
void
clear_line(void)
{
  // ANSI 转义序列: \033[2K
  consputc('\033');
  consputc('[');
  consputc('2');
  consputc('K');
  
  // 将光标移回行首
  consputc('\r');
}
```
set_color 函数用于设置颜色和属性参数分别表示前景色、背景色和属性。使用ANSI转义序列 \033[attr;3x;4ym 设置颜色：
attr: 文本属性 (0=重置, 1=粗体, 4=下划线等)但并未实现
3x: 前景色 (30-37对应不同颜色)
4y:保持接口一致性 背景色 (240-47对应不同颜色)
```c
// 颜色定义
#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

void
set_color(int fg, int bg,)
{
  // ANSI 转义序列: \033[attr;fg;bgm
  consputc('\033');
  consputc('[');
  consputc('0');
  
  // 前景色
  if(fg >= 0) {
    consputc(';');
    consputc('3');
    consputc('0' + fg);
  }
  
  // 背景色
  if(bg >= 0) {
    consputc(';');
    consputc('4');
    consputc('0' + bg);
  }
  
  consputc('m');
}
```

## 3、printf.c
对于printf.c提供了解析格式字符串转换数字的功能。首先的printint和printptr我直接使用了xv6的源码，这两个函数分别是将整数转换为指定进制的字符串并输出，以十六进制格式打印指针地址
```c
static void
printint(int xx, int base, int sign)
{
    char buf[16];
    int i;
    uint x;

    if (sign && (sign = (xx < 0)))
        x = -xx;
    else
        x = xx;

    i = 0;
    do
    {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i++] = '-';

    while (--i >= 0)
        consputc(buf[i]);
}

static void
printptr(uint64 x)
{
    int i;
    consputc('0');
    consputc('x');
    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
        consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}
```
printf函数支持基本数据类型的格式化输出 通过遍历格式字符串`fmt`识别格式说明符
- 使用`switch`语句处理不同的格式字符：
  - `%d`：调用`printint`以十进制格式打印整数
  - `%x`：调用`printint`以十六进制格式打印整数
  - `%p`：调用`printptr`打印指针地址
  - `%s`：逐字符打印字符串
  - `%c`：打印单个字符
  - `%%`：打印百分号本身
```c
void printf(char *fmt, ...)
{
    va_list ap;
    int i, c;
    char *s;

    va_start(ap, fmt);
    for (i = 0; (c = fmt[i] & 0xff) != 0; i++)
    {
        if (c != '%')
        {
            consputc(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if (c == 0)
            break;
        switch (c)
        {
        case 'd':
            printint(va_arg(ap, int), 10, 1);
            break;
        case 'x':
            printint(va_arg(ap, int), 16, 0);
            break;
        case 'p':
            printptr(va_arg(ap, uint64));
            break;
        case 's':
            if ((s = va_arg(ap, char *)) == 0)
                s = "(null)";
            for (; *s; s++)
                consputc(*s);
            break;
        case 'c':
            // 从可变参数中获取字符并打印
            consputc((char)va_arg(ap, int));
            break;
        case '%':
            consputc('%');
            break;
        default:
            // 打印未知 % 序列以引起注意
            consputc('%');
            consputc(c);
            break;
        }
    }
    va_end(ap);
}
```
printf_color的实现比printf多了一个调用console中setcolor的函数来设置字体颜色和背景颜色
```c
void printf_color(int fg, int bg, char *fmt, ...)
{
    va_list ap;
    int i, c;
    char *s;

    // 设置颜色
    set_color(fg, bg);

    va_start(ap, fmt);
    for (i = 0; (c = fmt[i] & 0xff) != 0; i++)
    {
        if (c != '%')
        {
            consputc(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if (c == 0)
            break;
        switch (c)
        {
        case 'd':
            printint(va_arg(ap, int), 10, 1);
            break;
        case 'x':
            printint(va_arg(ap, int), 16, 0);
            break;
        case 'p':
            printptr(va_arg(ap, uint64));
            break;
        case 's':
            if ((s = va_arg(ap, char *)) == 0)
                s = "(null)";
            for (; *s; s++)
                consputc(*s);
            break;
        case 'c':
            // 从可变参数中获取字符并打印
            consputc((char)va_arg(ap, int));
            break;
        case '%':
            consputc('%');
            break;
        default:
            // 打印未知 % 序列以引起注意
            consputc('%');
            consputc(c);
            break;
        }
    }
    va_end(ap);

    // 重置颜色
    set_color(-1, -1);
}
```

# 实验三：页表与内存管理
文件列表如下：
```text
.
├── kernel
│   ├── defs.h
│   ├── types.h
│   ├── param.h
│   ├── entry.S
│   ├── printf.c
│   ├── console.c
│   ├── start.c
│   ├── main.c
│   ├── memlayout.h
│   ├── kalloc.c
│   ├── vm.c
│   ├── string.c
│   ├── assert.h
|   ├── riscv.h
│   └── uart.c
├── kernel.ld
└── Makefile
```
## 1、新增各模块简介
riscv.h定义了RISC-V架构的相关定义，比如页面大小、页表项标志位等，kalloc.c主要是实现了物理内存的分配和释放，管理空闲页链表，vm.c主要建立和管理虚拟地址到物理地址的映射关系。
## 2、riscv.h
riscv.h声明了页面管理的相关定义，页表项标志位的定义，页表操作宏，以及部分操作函数。我是直接套用的xv6的源码，选择了我需要的部分。
```c
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

// 寄存器读写函数
static inline uint64
r_satp()
{
    uint64 x;
    asm volatile("csrr %0, satp" : "=r"(x));
    return x;
}

static inline void
w_satp(uint64 x)
{
    asm volatile("csrw satp, %0" : : "r"(x));
}

static inline void
sfence_vma()
{
    asm volatile("sfence.vma zero, zero");
}

// 其他 CSR 操作函数可以根据需要添加
static inline void
w_mstatus(uint64 x)
{
    asm volatile("csrw mstatus, %0" : : "r"(x));
}
```
## 3、kalloc.c
pmem_init()函数将物理内存区域分区并通过调用free_page将每一页加入到空闲链表中。
```c
void pmem_init(void)
{
    // initlock(&kmem.lock, "kmem");

    // 初始化统计信息
    kmem.total_pages = 0;
    kmem.allocated_pages = 0;
    kmem.free_pages = 0;

    // 从内核结束地址到PHYSTOP的内存加入空闲链表
    char *p = (char *)PGROUNDUP((uint64)end);
    for (; p + PGSIZE <= (char *)PHYSTOP; p += PGSIZE)
    {
        free_page(p);
        kmem.total_pages++;
    }

    kmem.free_pages = kmem.total_pages;
    printf("Physical memory initialized: %d pages available\n", kmem.free_pages);
}
```
alloc_page()函数分配单个物理页面，从空闲链表中取出第一个页，并将其从链表中删除，同时更新分配的统计信息，最后返回页面地址。
```c
void *alloc_page(void)
{
    struct run *r;

    // acquire(&kmem.lock);
    r = kmem.freelist;
    if (r)
    {
        kmem.freelist = r->next;
        kmem.allocated_pages++;
        kmem.free_pages--;
    }
    // release(&kmem.lock);

    if (r)
    {
        // 填充调试模式值，帮助检测未初始化内存
        memset((char *)r, 0xAA, PGSIZE);
    }

    return (void *)r;
}
```
alloc_pages()函数分配多个物理页面，从空闲链表中取出指定数量的页，并将其从链表中删除，更新分配的统计信息，最后返回第一个页的地址。每次先分配第一个页面，然后依次分配后续页面并检查是否物理地址连续。若发现不连续，则释放已分配的页面重新尝试。
```c
void *alloc_pages(int n)
{
    if (n <= 0)
        return 0;

    if (n == 1)
        return alloc_page();

    void *pages[n];
    int consecutive = 0;

    for (int attempt = 0; attempt < 10; attempt++)
    { // 最多尝试10次
        // 分配第一页
        pages[0] = alloc_page();
        if (!pages[0])
            return 0;

        // 尝试分配连续的后续页面
        consecutive = 1;
        for (int i = 1; i < n; i++)
        {
            pages[i] = alloc_page();
            if (!pages[i] ||
                (uint64)pages[i] != (uint64)pages[i - 1] + PGSIZE)
            {
                // 不连续，释放已分配的页面
                for (int j = 0; j < consecutive; j++)
                {
                    free_page(pages[j]);
                }
                consecutive = 0;
                break;
            }
            consecutive++;
        }

        if (consecutive == n)
        {
            return pages[0]; // 成功分配到连续页面
        }
    }

    return 0; // 多次尝试后仍失败
}
```
free_page()函数释放单个物理页面，先对释放的物理页面进行多项安全检查，然后将页面内容清零，之后将当前页面设置为新的空闲页链表头结点。
```c
void free_page(void *pa)
{
    struct run *r;

    // 参数检查
    if (!pa)
        panic("free_page: null pointer");

    if (((uint64)pa % PGSIZE) != 0)
        panic("free_page: not page aligned");

    if ((char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("free_page: out of range");

    // 安全检查：清空页面内容，防止信息泄漏
    memset(pa, 0, PGSIZE);

    r = (struct run *)pa;

    // acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    kmem.allocated_pages--;
    kmem.free_pages++;
    // release(&kmem.lock);
}
```
## 4、vm.c
首先vm.c定义了页表项和页表类型，内核页表指针，各宏定义和页表统计结构体。
```c
// 页表类型定义
typedef uint64 pte_t;
typedef uint64 *pagetable_t;

// 全局内核页表
pagetable_t kernel_pagetable;

// 虚拟地址空间限制（Sv39规范）
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

// 从虚拟地址提取各级VPN
#define VPN_SHIFT(level) (12 + 9 * (level))
#define VPN(va, level) (((va) >> VPN_SHIFT(level)) & 0x1FF)

static struct pagetable_stats pt_stats;
```
vm.c使用walk()来遍历整个页表，根据虚拟地址查找对应的页表项，并返回页表项的指针，也提供了分配新页表的功能。检查虚拟地址是否超出最大范围,遍历三级页表,如果页表项有效则进入下一级，否则根据alloc参数决定是否分配新页表,返回最后一级页表中对应项的指针.
```c
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
    if (va >= MAXVA)
    {
        vm_debug("walk: virtual address %p too large\n", va);
        return 0;
    }

    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pagetable[VPN(va, level)];

        if (*pte & PTE_V)
        {
            // 页表项有效，进入下一级
            pagetable = (pagetable_t)PTE2PA(*pte);
        }
        else
        {
            // 页表项无效
            if (!alloc)
            {
                return 0;
            }

            // 分配新页表
            pagetable = (pagetable_t)alloc_page();
            if (pagetable == 0)
            {
                vm_debug("walk: kalloc failed for level %d page table\n", level);
                return 0;
            }

            // 清零新页表
            memset(pagetable, 0, PGSIZE);
            pt_stats.total_pt_pages++;

            // 设置父页表项
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }

    return &pagetable[VPN(va, 0)];
}
```
walk_lookup仅查找页表项，不创建新的页表。
```c
pte_t *
walk_lookup(pagetable_t pagetable, uint64 va)
{
    return walk(pagetable, va, 0);
}
```
walk_create()创建并分配新的页表项。
```c
pte_t *
walk_create(pagetable_t pagetable, uint64 va)
{
    return walk(pagetable, va, 1);
}
```
create_pagetable()创建空页表。
```c
pagetable_t
create_pagetable(void)
{
    pagetable_t pagetable = (pagetable_t)alloc_page();
    if (pagetable == 0)
    {
        vm_debug("create_pagetable: kalloc failed\n");
        return 0;
    }

    memset(pagetable, 0, PGSIZE);
    pt_stats.total_pt_pages++;

    vm_debug("Created new pagetable at %p\n", pagetable);
    return pagetable;
}
```
mappages()函数将物理页面映射到虚拟地址空间。对地址按页对齐处理,遍历每一页，调用walk_create()创建页表项,检查是否已映射，避免重复映射,建立映射关系并设置权限。map_page()函数调用mappages实现单页映射，unmap_page()函数调用walk_lookup()实现单页取消映射。
```c
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
    uint64 a, last;
    pte_t *pte;

    if (size == 0)
    {
        vm_debug("mappages: zero size\n");
        return -1;
    }

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);

    vm_debug("mappages: va=%p to pa=%p, size=%d, perm=0x%x\n",
             va, pa, size, perm);

    for (;;)
    {
        // 查找或创建页表项
        if ((pte = walk_create(pagetable, a)) == 0)
        {
            vm_debug("mappages: walk_create failed for va=%p\n", a);
            return -1;
        }

        // 检查是否已映射
        if (*pte & PTE_V)
        {
            vm_debug("mappages: remap detected at va=%p\n", a);
            return -1;
        }

        // 建立映射
        if (perm & PTE_U)
        {
            *pte = PA2PTE(pa) | perm | PTE_V;
        }
        else
        {
            *pte = PA2PTE(pa) | perm | PTE_V;
        }

        pt_stats.total_mappings++;

        if (a == last)
        {
            break;
        }
        a += PGSIZE;
        pa += PGSIZE;
    }

    return 0;
}

int map_page(pagetable_t pagetable, uint64 va, uint64 pa, int perm)
{
    return mappages(pagetable, va, PGSIZE, pa, perm);
}

void unmap_page(pagetable_t pagetable, uint64 va)
{
    pte_t *pte = walk_lookup(pagetable, va);
    if (pte == 0)
    {
        return;
    }

    if (*pte & PTE_V)
    {
        *pte = 0; // 清除页表项
        pt_stats.total_mappings--;

        // 注意：这里不释放物理页面，由调用者负责
        vm_debug("unmap_page: unmapped va=%p\n", va);
    }
}
```
free_pagetable_recursive()递归释放页表，遍历页表所有512个项，如果是中间级页表且有效，则递归释放下级页表。destroy_pagetable() 调用free_pagetable_recursive()销毁页表。
```c
static void
free_pagetable_recursive(pagetable_t pagetable, int level)
{
    // 遍历所有512个页表项
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = pagetable[i];
        if (pte & PTE_V)
        {
            if (level > 0)
            {
                // 中间级页表，递归释放
                free_pagetable_recursive((pagetable_t)PTE2PA(pte), level - 1);
            }
            // 注意：不释放叶子节点指向的物理页面
        }
    }

    // 释放当前页表页面
    free_page((void *)pagetable);
    pt_stats.total_pt_pages--;
}


void destroy_pagetable(pagetable_t pagetable)
{
    if (pagetable == 0)
    {
        return;
    }

    vm_debug("destroy_pagetable: freeing pagetable %p\n", pagetable);
    free_pagetable_recursive(pagetable, 2); // Sv39有3级，从第2级开始
}
```
walkaddr()实现虚拟地址到物理地址转换。首先使用walk_lookup()查找页表项，然后检查页表项是否有效，最后提取物理页地址并加上页内偏移。
```c
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;
    uint64 pa;

    if (va >= MAXVA)
    {
        return 0;
    }

    pte = walk_lookup(pagetable, va);
    if (pte == 0)
    {
        return 0;
    }

    if ((*pte & PTE_V) == 0)
    {
        return 0;
    }

    pa = PTE2PA(*pte);

    // 加上页内偏移
    pa |= (va & (PGSIZE - 1));

    return pa;
}
```
kvminit()初始化内核页表，kvminithart()激活内核页表。
```c
void kvminit(void)
{
    vm_debug("Initializing kernel page table\n");

    kernel_pagetable = create_pagetable();
    if (kernel_pagetable == 0)
    {
        panic("kvminit: create_pagetable failed");
    }

    pt_stats.kernel_pt_pages = 1; // 根页表

    // 获取内核段边界（这些符号在链接脚本中定义）
    extern char etext[]; // 内核代码结束
    extern char end[];   // 内核数据结束

    vm_debug("Kernel segments: text=0x%p-0x%p, data=0x%p-0x%p\n",
             KERNBASE, etext, etext, end);

    // 映射内核代码段 (R+X权限)
    if (mappages(kernel_pagetable, KERNBASE, (uint64)etext - KERNBASE,
                 KERNBASE, PTE_R | PTE_X) < 0)
    {
        panic("kvminit: kernel text mapping failed");
    }

    // 映射内核数据段 (R+W权限)
    uint64 data_start = PGROUNDUP((uint64)etext); // 从下一个页面边界开始
    if (data_start < (uint64)end)
    { // 只有在有数据段需要映射时才映射
        if (mappages(kernel_pagetable, data_start, (uint64)end - data_start,
                     data_start, PTE_R | PTE_W) < 0)
        {
            panic("kvminit: kernel data mapping failed");
        }
    }

    // 映射剩余的物理内存
    uint64 remaining_start = PGROUNDUP((uint64)end);
    if (remaining_start < PHYSTOP)
    {
        if (mappages(kernel_pagetable, remaining_start, PHYSTOP - remaining_start,
                     remaining_start, PTE_R | PTE_W) < 0)
        {
            panic("kvminit: remaining memory mapping failed");
        }
    }

    // 映射设备内存
    // UART
    if (mappages(kernel_pagetable, UART0, PGSIZE, UART0, PTE_R | PTE_W) < 0)
    {
        panic("kvminit: UART mapping failed");
    }

    vm_debug("Kernel page table initialized successfully\n");
}

/**
 * 激活内核页表
 */
void kvminithart(void)
{
    vm_debug("Activating kernel page table\n");

    // 写入SATP寄存器
    w_satp(MAKE_SATP(kernel_pagetable));

    // 刷新TLB
    sfence_vma();

    vm_debug("Virtual memory enabled\n");
}
```
