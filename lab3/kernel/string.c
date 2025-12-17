#include "types.h"
#include <stddef.h>
/**
 * 用指定值填充内存区域
 */
void *memset(void *dest, int c, size_t n)
{
    if (dest == 0 || n == 0)
    {
        return dest;
    }

    unsigned char *p = (unsigned char *)dest;
    unsigned char value = (unsigned char)c;

    // 简单的逐字节填充
    for (size_t i = 0; i < n; i++)
    {
        p[i] = value;
    }

    return dest;
}

/**
 * 内存复制（不处理重叠）
 */
void *memcpy(void *dest, const void *src, size_t n)
{
    if (dest == 0 || src == 0 || n == 0)
    {
        return dest;
    }

    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    // 逐字节复制
    for (size_t i = 0; i < n; i++)
    {
        d[i] = s[i];
    }

    return dest;
}

/**
 * 内存移动（处理重叠区域）
 */
void *memmove(void *dest, const void *src, size_t n)
{
    if (dest == 0 || src == 0 || n == 0)
    {
        return dest;
    }

    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    // 如果目标地址在源地址之前，或者不重叠，正向复制
    if (d < s || d >= s + n)
    {
        for (size_t i = 0; i < n; i++)
        {
            d[i] = s[i];
        }
    }
    else
    {
        // 如果目标地址在源地址之后且有重叠，反向复制
        for (size_t i = n; i > 0; i--)
        {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

/**
 * 内存比较
 */
int memcmp(const void *s1, const void *s2, size_t n)
{
    if (s1 == 0 && s2 == 0)
        return 0;
    if (s1 == 0)
        return -1;
    if (s2 == 0)
        return 1;

    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i])
        {
            return (int)p1[i] - (int)p2[i];
        }
    }

    return 0;
}

/**
 * 计算字符串长度
 */
size_t strlen(const char *s)
{
    if (s == 0)
    {
        return 0;
    }

    size_t len = 0;
    while (s[len] != '\0')
    {
        len++;
    }

    return len;
}

/**
 * 字符串复制
 */
char *strcpy(char *dest, const char *src)
{
    if (dest == 0 || src == 0)
    {
        return dest;
    }

    char *d = dest;
    while ((*d++ = *src++) != '\0')
    {
        // 空循环体，所有工作在条件中完成
    }

    return dest;
}

/**
 * 有限长度字符串复制
 */
char *strncpy(char *dest, const char *src, size_t n)
{
    if (dest == 0 || src == 0 || n == 0)
    {
        return dest;
    }

    char *d = dest;
    size_t i;

    for (i = 0; i < n && src[i] != '\0'; i++)
    {
        d[i] = src[i];
    }

    // 如果n大于src长度，用空字符填充剩余空间
    for (; i < n; i++)
    {
        d[i] = '\0';
    }

    return dest;
}

/**
 * 字符串比较
 */
int strcmp(const char *s1, const char *s2)
{
    if (s1 == 0 && s2 == 0)
        return 0;
    if (s1 == 0)
        return -1;
    if (s2 == 0)
        return 1;

    while (*s1 && *s2 && *s1 == *s2)
    {
        s1++;
        s2++;
    }

    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/**
 * 有限长度字符串比较
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0)
        return 0;
    if (s1 == 0 && s2 == 0)
        return 0;
    if (s1 == 0)
        return -1;
    if (s2 == 0)
        return 1;

    while (n-- > 0 && *s1 && *s2 && *s1 == *s2)
    {
        s1++;
        s2++;
    }

    if (n == (size_t)-1)
    { // 如果n为0，返回0
        return 0;
    }

    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/**
 * 字符串连接
 */
char *strcat(char *dest, const char *src)
{
    if (dest == 0 || src == 0)
    {
        return dest;
    }

    // 找到dest的结尾
    char *d = dest;
    while (*d != '\0')
    {
        d++;
    }

    // 追加src
    while ((*d++ = *src++) != '\0')
    {
        // 空循环体
    }

    return dest;
}

/**
 * 有限长度字符串连接
 */
char *strncat(char *dest, const char *src, size_t n)
{
    if (dest == 0 || src == 0 || n == 0)
    {
        return dest;
    }

    // 找到dest的结尾
    char *d = dest;
    while (*d != '\0')
    {
        d++;
    }

    // 追加最多n个字符
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
    {
        d[i] = src[i];
    }
    d[i] = '\0'; // 确保以空字符结尾

    return dest;
}

/**
 * 查找字符在字符串中第一次出现的位置
 */
char *strchr(const char *s, int c)
{
    if (s == 0)
    {
        return 0;
    }

    char ch = (char)c;
    while (*s != '\0')
    {
        if (*s == ch)
        {
            return (char *)s;
        }
        s++;
    }

    // 检查是否查找空字符
    if (ch == '\0')
    {
        return (char *)s;
    }

    return 0;
}

/**
 * 查找字符在字符串中最后一次出现的位置
 */
char *strrchr(const char *s, int c)
{
    if (s == 0)
    {
        return 0;
    }

    char ch = (char)c;
    const char *last = 0;

    while (*s != '\0')
    {
        if (*s == ch)
        {
            last = s;
        }
        s++;
    }

    // 检查是否查找空字符
    if (ch == '\0')
    {
        return (char *)s;
    }

    return (char *)last;
}

/**
 * 在内存区域中查找字符
 */
void *memchr(const void *s, int c, size_t n)
{
    if (s == 0 || n == 0)
    {
        return 0;
    }

    const unsigned char *p = (const unsigned char *)s;
    unsigned char ch = (unsigned char)c;

    for (size_t i = 0; i < n; i++)
    {
        if (p[i] == ch)
        {
            return (void *)(p + i);
        }
    }

    return 0;
}