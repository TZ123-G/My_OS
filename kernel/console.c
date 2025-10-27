// kernel/console.c
#include "types.h"
#include "defs.h"

#define BACKSPACE 0x100
// 颜色定义
#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7


// 向控制台输出一个字符
// 处理特殊字符如退格
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

// 初始化控制台
void
consoleinit(void)
{
  uartinit();
}

// 清屏函数 - 使用 ANSI 转义序列
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

// 光标定位函数
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

// 清除当前行
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

// 设置颜色和属性
void
set_color(int fg, int bg)
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