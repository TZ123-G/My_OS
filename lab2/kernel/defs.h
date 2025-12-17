// console.c
// 颜色定义
#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

// 属性定义
#define ATTR_RESET 0
#define ATTR_BOLD 1
#define ATTR_UNDERLINE 4
#define ATTR_BLINK 5
#define ATTR_REVERSE 7

void consoleinit(void);
void consputc(int c);
void goto_xy(int x, int y);
void clear_line(void);
void set_color(int fg, int bg, int attr);

// printf.c
void printf(char *fmt, ...);
void panic(char *s);
void clear_screen(void);
void printf_color(int fg, int bg, int attr, char *fmt, ...);

// uart.c
void uartputc_sync(int c);
void uartinit(void);
void uart_putc(char c);
void uart_puts(char *s);