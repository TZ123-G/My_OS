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

## 8、测试函数
通过uartinit()和uart_puts("\nHello, RISC-V OS!\n");来测试内核是否正常启动，并输出Hello, RISC-V OS!
测试结果如下：
![lab1测试1.png](results/lab1测试1.png)