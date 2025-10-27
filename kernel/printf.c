// kernel/printf.c
#include "types.h"
#include "defs.h"
#include <stdarg.h>

static char digits[] = "0123456789abcdef";

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

// 简化版 printf，支持基本格式
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

// 简化版 panic 函数
void panic(char *s)
{
    printf("panic: ");
    printf(s);
    printf("\n");
    while (1)
        ;
}