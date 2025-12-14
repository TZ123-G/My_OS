想象一下，内核是操作系统的“黑匣子”。当系统崩溃、性能下降或出现异常行为时，我们
需要一种方法来回溯发生了什么。
Ø 传统的printf调试有什么问题？
p阻塞I/O：直接向控制台打印非常缓慢，会严重影响内核性能，甚至在中断上下文中是不
可行的。
p信息泛滥：调试信息和关键错误混在一起，难以筛选。
p无法控制：一旦编译进内核，就无法在运行时动态开启或关闭。
p非结构化：纯文本流，难以进行自动化分析。
Ø 本项目目标：构建一个高性能、结构化、可配置的内核日志框架，解决以上所有问题。
我们的日志系统将遵循以下四个核心设计原则：
Ø 结构化与分级 (Structured & Leveled)
p每条日志都有明确的严重性级别（如 DEBUG, INFO, WARN, ERROR）。
p日志格式统一，包含时间戳、级别、来源模块等元数据。
Ø 高性能缓冲 (High-Performance Buffering)
p日志产生时，仅需快速写入内存缓冲区，与慢速的I/O设备解耦。
p即使在中断处理等对性能极其敏感的上下文中也能安全、快速地记录日志。
我们的日志系统将遵循以下四个核心设计原则：
Ø 结构化与分级 (Structured & Leveled)
p每条日志都有明确的严重性级别（如 DEBUG, INFO, WARN, ERROR）。
p日志格式统一，包含时间戳、级别、来源模块等元数据。
Ø 高性能缓冲 (High-Performance Buffering)
p日志产生时，仅需快速写入内存缓冲区，与慢速的I/O设备解耦。
p即使在中断处理等对性能极其敏感的上下文中也能安全、快速地记录日志。
灵活格式化 (Flexible Formatting)
p支持类似 printf 的可变参数格式化功能，方便开发者记录丰富的信息。
p例如：klog(LEVEL_INFO, "File %s opened, fd=%d", name, fd);
Ø 安全与可控 (Safe & Controllable)
p提供一个从用户空间安全读取内核日志的接口（系统调用）。
p支持在系统运行时动态调整日志记录级别，过滤掉不关心的信息。
灵活格式化 (Flexible Formatting)
p支持类似 printf 的可变参数格式化功能，方便开发者记录丰富的信息。
p例如：klog(LEVEL_INFO, "File %s opened, fd=%d", name, fd);
Ø 安全与可控 (Safe & Controllable)
p提供一个从用户空间安全读取内核日志的接口（系统调用）。
p支持在系统运行时动态调整日志记录级别，过滤掉不关心的信息。
日志级别让我们能控制信息的“信噪比”。
Ø 1. 定义级别
Ø 在内核头文件中定义一组级别常量。
// In kernel/log.h or similar
#define LOG_LEVEL_DEBUG   0  // 详细的调试信息
#define LOG_LEVEL_INFO    1  // 常规运行信息
#define LOG_LEVEL_WARN    2  // 潜在问题警告
#define LOG_LEVEL_ERROR   3  // 发生错误，但不影响系统继续运行
#define LOG_LEVEL_FATAL   4  // 严重错误，可能导致系统崩溃
2. 全局日志级别
Ø 在内核中维护一个全局变量，用于控制当前生效的日志级别。
// In kernel/log.c
int current_log_level = LOG_LEVEL_INFO;
3. 在klog()中实现过滤
Ø klog() 函数在写入缓冲区之前，首先检查消息级别。
Ø 好处：这种“尽早过滤”的方式避免了不必要的格式化和内存拷贝开销，提升了性能。
void klog(int level, const char *fmt, ...) {
  // 如果消息的级别低于当前系统设置的级别，则直接返回，不记录。
  if (level < current_log_level) {
    return;
  }
  
  // ... 后续的格式化和写入缓冲区的逻辑 ...
}
这是本项目性能和并发安全的关键。
Ø 为什么是环形缓冲区？
p高效：内存区域固定，无需动态分配，只需移动读/写指针。
p天然的FIFO：先进先出，符合日志时序。
p自动覆盖：当缓冲区写满时，新的日志可以覆盖最旧的日志，保证系统在极端情况下依
然能记录最新的事件。
// In kernel/log.c
#define LOG_BUF_SIZE 4096
struct klog_buffer {
  struct spinlock lock;     // 保护缓冲区的自旋锁
  char buf[LOG_BUF_SIZE];   // 存储日志数据的数组
  int read_pos;             // 读取位置
  int write_pos;            // 写入位置
};
struct klog_buffer log_buf;
写入逻辑 (klog_write)
p获取锁：acquire(&log_buf.lock);
p写入数据：将格式化好的日志字符串逐字节拷贝到 log_buf.buf 的 write_pos 位置。
p更新写指针：log_buf.write_pos = (log_buf.write_pos + 1) % LOG_BUF_SIZE;
p处理覆盖：如果 write_pos 追上了 read_pos，需要将 read_pos 也向前移动，丢弃旧数
据。
p释放锁：release(&log_buf.lock);
Ø 关键挑战：保证整个写入过程是原子的。如果一条日志消息被分成了两次写入，并且中间被
其他CPU的写入打断，日志就会错乱。锁是必须的！
内核不能使用标准C库的printf，我们需要实现一个内核版的。
Ø klog() API 设计
p我们希望用户能像这样调用：
p klog(LOG_LEVEL_ERROR, "Failed to allocate page for pid %d", p->pid);
Ø 这需要使用C语言的可变参数机制。
实现步骤
Ø 创建kvprintf: 实现一个 kvprintf(const 
char *fmt, va_list ap) 函数，它接受一个格
式化字符串和 va_list 类型的参数列表。这
个函数是所有格式化输出的核心。
Ø 解析格式字符串: 在 kvprintf 内部，遍历 
fmt 字符串。当遇到 % 时，根据后面的字
符（如 d, s, x, p）从 va_list 中取出对应类
型的参数，并将其转换为字符串。
Ø klog 包装:
void klog(int level, const char *fmt, ...) {
  if (level < current_log_level) return;
  va_list ap;
  char formatted_log[MAX_LOG_LEN]; // 临
时缓冲区
  
  va_start(ap, fmt);
  // vsnprintf 是一个很好的参考，它将格式
化结果输出到字符串
  int len = vsnprintf(formatted_log, 
MAX_LOG_LEN, fmt, ap);
  va_end(ap);
  // 调用内部函数，将 formatted_log 写入环
形缓冲区
  klog_write(formatted_log, len);
}
如何让用户空间的程序拿到内核日志？答案是系统调用。
Ø 系统调用原型 int sys_klog(char *user_buf, int n);
p user_buf: 用户提供的缓冲区地址。
p n: 用户缓冲区的大小。
p返回值: 实际读取到的日志字节数。
int sys_klog(char *user_buf, int n) {
  int bytes_to_read;
  acquire(&log_buf.lock); // 1. 加锁
  // 2. 计算有多少数据可读
  //    这是环形缓冲区中 write_pos 和 read_pos 之间的距离
  bytes_to_read = calculate_readable_bytes(&log_buf);
  if (bytes_to_read == 0) {// 如果没有日志，可以考虑让进程睡眠，等待新日志
写入后再唤醒
    // sleep(&log_buf.read_pos, &log_buf.lock);
    // 这里为了简化，我们先实现非阻塞版本
    release(&log_buf.lock);
    return 0;
  }
  
  if (n < bytes_to_read) bytes_to_read = n;
  // 3. 从环形缓冲区读取数据
  //    注意处理跨越数组末尾的回卷情况
  read_from_ring_buffer(temp_kbuf, bytes_to_read);
  
  // 4. 更新内核的 read_pos
  log_buf.read_pos = (log_buf.read_pos + 
bytes_to_read) % LOG_BUF_SIZE;
  release(&log_buf.lock); // 5. 释放锁
  // 6. 使用 copyout 安全地将数据从内核空间
拷贝到用户空间
  if (copyout(user_pagetable, user_buf, 
temp_kbuf, bytes_to_read) < 0) {
    return -1;
  }
  return bytes_to_read;
}
Task 1: 基础结构定义
p在 kernel/defs.h 中添加 klog 函数原型。
p在 kernel/log.c 中定义 struct klog_buffer 和日志级别。
p初始化日志缓冲区和锁。
Ø Task 2: 实现环形缓冲区逻辑
p实现核心的 klog_write() 函数，处理加锁、写入、更新写指针和数据覆盖。
Ø Task 3: 实现格式化功能
p实现一个简化的内核版 kvprintf 或 vsnprintf。
p创建 klog() 宏或函数，调用格式化功能，并最终调用 klog_write()。
Task 4: 添加系统调用
p实现 sys_klog()，完成从内核缓冲区到用户空间的日志数据拷贝。
p注册新的系统调用。
Ø Task 5: 编写用户态工具
p创建一个用户程序 logread.c，循环调用 sys_klog() 并将读取到的内容打印到标准输出。
Ø Task 6: 集成与测试
p在内核的关键路径（如 sys_open, fork, exec 等）中添加不同级别的 klog() 调用。
p运行内核，并执行 logread 程序，验证日志是否能被正确捕获和显示。
p（可选）实现一个修改 current_log_level 的系统调用，动态调整日志级别并验证过滤效果。