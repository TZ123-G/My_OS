// printf.h
#ifndef PRINTF_H
#define PRINTF_H

// 标准 printf 函数
void printf(char *fmt, ...);

// 内核恐慌函数，用于输出错误信息并终止程序
void panic(char *s);

// 带颜色支持的 printf 函数
// fg: 前景色 (foreground color)
// bg: 背景色 (background color)
// fmt: 格式化字符串
// ...: 可变参数列表
void printf_color(int fg, int bg, char *fmt, ...);

#endif // PRINTF_H