#include "types.h"
#include "spinlock.h"
#include "printf.h"
#include "string.h"
#include "defs.h" // for uart_putc prototype
#include "log.h"
#include <stdarg.h>

struct klog_buffer log_buf;
int current_log_level = LOG_LEVEL_INFO;

static int klog_readable_bytes(void)
{
    if (log_buf.write_pos >= log_buf.read_pos)
        return log_buf.write_pos - log_buf.read_pos;
    else
        return (LOG_BUF_SIZE - log_buf.read_pos) + log_buf.write_pos;
}

void klog_init(void)
{
    initlock(&log_buf.lock, "klog");
    log_buf.read_pos = 0;
    log_buf.write_pos = 0;
}

static void klog_write_bytes(const char *s, int len)
{
    for (int i = 0; i < len; i++)
    {
        log_buf.buf[log_buf.write_pos] = s[i];
        log_buf.write_pos = (log_buf.write_pos + 1) % LOG_BUF_SIZE;
        // 覆盖最旧数据：若追上 read_pos，则前移 read_pos
        if (log_buf.write_pos == log_buf.read_pos)
        {
            log_buf.read_pos = (log_buf.read_pos + 1) % LOG_BUF_SIZE;
        }
    }
}

// 仅支持少量格式占位：%d, %x, %p, %s, %%
static int kvsnprintf(char *out, int outsz, const char *fmt, va_list ap)
{
    int n = 0;
    for (; *fmt && n < outsz; fmt++)
    {
        if (*fmt != '%')
        {
            out[n++] = *fmt;
            continue;
        }
        fmt++;
        if (!*fmt)
            break;
        char buf[32];
        int m = 0;
        switch (*fmt)
        {
        case '%':
            out[n++] = '%';
            break;
        case 'd':
        {
            int x = va_arg(ap, int);
            // 简易十进制转换
            unsigned int ux = (x < 0) ? -x : x;
            char tmp[32];
            int t = 0;
            if (x < 0)
                buf[m++] = '-';
            if (ux == 0)
                tmp[t++] = '0';
            while (ux)
            {
                tmp[t++] = '0' + (ux % 10);
                ux /= 10;
            }
            while (t--)
                buf[m++] = tmp[t];
            for (int i = 0; i < m && n < outsz; i++)
                out[n++] = buf[i];
            break;
        }
        case 'x':
        {
            unsigned int x = va_arg(ap, unsigned int);
            const char *hex = "0123456789abcdef";
            char tmp[32];
            int t = 0;
            if (x == 0)
                tmp[t++] = '0';
            while (x)
            {
                tmp[t++] = hex[x & 0xF];
                x >>= 4;
            }
            while (t--)
                out[n++] = tmp[t];
            break;
        }
        case 'p':
        {
            uint64 x = (uint64)va_arg(ap, void *);
            const char *hex = "0123456789abcdef";
            out[n++] = '0';
            if (n < outsz)
                out[n++] = 'x';
            for (int i = (sizeof(uint64) * 2) - 1; i >= 0 && n < outsz; i--)
            {
                out[n++] = hex[(x >> (i * 4)) & 0xF];
            }
            break;
        }
        case 's':
        {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (*s && n < outsz)
                out[n++] = *s++;
            break;
        }
        default:
            // 未知格式符，原样输出
            out[n++] = '%';
            if (n < outsz)
                out[n++] = *fmt;
            break;
        }
    }
    if (n < outsz)
        out[n] = '\0';
    else
        out[outsz - 1] = '\0';
    return n;
}

void klog(int level, const char *fmt, ...)
{
    if (level < current_log_level)
        return;
    char line[MAX_LOG_LEN];
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = kvsnprintf(line, MAX_LOG_LEN, fmt, ap);
    va_end(ap);

    // 添加简单的级别前缀与换行
    const char *prefix = "";
    switch (level)
    {
    case LOG_LEVEL_DEBUG:
        prefix = "[DBG] ";
        break;
    case LOG_LEVEL_INFO:
        prefix = "[INF] ";
        break;
    case LOG_LEVEL_WARN:
        prefix = "[WRN] ";
        break;
    case LOG_LEVEL_ERROR:
        prefix = "[ERR] ";
        break;
    case LOG_LEVEL_FATAL:
        prefix = "[FTL] ";
        break;
    default:
        prefix = "[LOG] ";
        break;
    }

    acquire(&log_buf.lock);
    klog_write_bytes(prefix, strlen(prefix));
    klog_write_bytes(line, n);
    klog_write_bytes("\n", 1);
    release(&log_buf.lock);
}

// 用于调试：将当前可读内容全部打印到控制台
int klog_dump_to_console(void)
{
    int printed = 0;
    acquire(&log_buf.lock);
    int avail = klog_readable_bytes();
    while (avail > 0)
    {
        char c = log_buf.buf[log_buf.read_pos];
        log_buf.read_pos = (log_buf.read_pos + 1) % LOG_BUF_SIZE;
        avail--;
        // 直接使用 printf 的底层输出，避免递归日志
        uart_putc(c);
        printed++; 
    }
    release(&log_buf.lock);
    return printed;
}

int klog_read(char *dst, int n)
{
    int copied = 0;
    acquire(&log_buf.lock);
    int avail = klog_readable_bytes();
    if (avail == 0)
    {
        release(&log_buf.lock);
        return 0;
    }
    if (n > avail)
        n = avail;
    for (int i = 0; i < n; i++)
    {
        dst[i] = log_buf.buf[log_buf.read_pos];
        log_buf.read_pos = (log_buf.read_pos + 1) % LOG_BUF_SIZE;
        copied++;
    }
    release(&log_buf.lock);
    return copied;
}
// kernel/log.c
#include "types.h"
#include "defs.h"
#include "fs.h"

// Minimal stub logging to satisfy dependencies from task.md.
static int log_inited = 0;

void log_init(void)
{
    if (log_inited)
        return;
    log_inited = 1;
}

void begin_op(void) {}
void end_op(void) {}
void log_write(struct buf *b) {}
