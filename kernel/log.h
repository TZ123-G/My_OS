#pragma once

#include "types.h"

// 日志级别
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_FATAL 4

#define LOG_BUF_SIZE 4096
#define MAX_LOG_LEN 256

struct klog_buffer
{
    struct spinlock lock;
    char buf[LOG_BUF_SIZE];
    int read_pos;
    int write_pos;
};

extern struct klog_buffer log_buf;
extern int current_log_level;

void klog_init(void);
void klog(int level, const char *fmt, ...);
int klog_dump_to_console(void);  // 仅用于早期验证：把当前缓冲区可读内容打印到控制台
int klog_read(char *dst, int n); // 读取n字节到内核缓冲，返回实际读取字节数
