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
    consoleinit();

    // 清屏
    clear_screen();

    // 打印欢迎信息
    printf("Hello, RISC-V OS!\n");

    // 测试光标定位
    goto_xy(5, 3); // 移动到第3行第5列
    printf("This text starts at (5,3)");

    // 测试颜色输出
    printf_color(COLOR_RED, COLOR_BLACK, ATTR_BOLD, "\nRed bold text on black background\n");
    printf_color(COLOR_GREEN, -1, ATTR_UNDERLINE, "Green underlined text\n");
    printf_color(COLOR_BLUE, COLOR_YELLOW, ATTR_BLINK, "Blue blinking text on yellow background\n");

    // 测试清除行
    printf("This is a long line that will be partially cleared...");
    clear_line();
    printf("This is the new content after clearing the line\n");

    // 组合使用功能
    goto_xy(1, 10); // 移动到第10行第1列
    printf_color(COLOR_CYAN, -1, ATTR_BOLD, "Combined: positioned text with color");

    main();
}