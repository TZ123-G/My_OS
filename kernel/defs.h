#ifndef DEFS_H
#define DEFS_H

#include "types.h"
struct spinlock;
struct proc;
struct context;
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

void consoleinit(void);
void consputc(int c);
void clear_screen(void);
void goto_xy(int x, int y);
void clear_line(void);
void set_color(int fg, int bg);

// uart.c
void uartinit(void);
void uart_putc(char c);
void uart_puts(char *s);

// kalloc.c
void pmem_init(void);
void *alloc_page(void);
void *alloc_pages(int n);
void free_page(void *pa);

// vm.c
// 页表类型和接口由 vm.h 提供
#include "vm.h"

// string.c
// 内存操作函数
void *memset(void *dest, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
// 字符串操作函数
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
// 内存查找函数
void *memchr(const void *s, int c, size_t n);

// trap.c
struct trapframe;
void trap_init(void);
void timer_interrupt_handler(void);
void enable_interrupts(void);
void disable_interrupts(void);
void usertrapret(void);

// proc.c
struct proc *myproc(void);
void procinit(void);
void scheduler(void);
void yield(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
int kill(int pid);
int wait(uint64 addr);
void proc_freepagetable(pagetable_t pagetable, uint64 sz);
void yield(void);
void wakeup(void *chan);
/* minimal cross-file prototypes used by proc.c */
void forkret(void);
void usertrapret(void);
void swtch(struct context *, struct context *);
void sched(void);
void freeproc(struct proc *p);

/* Process API wrappers provided for tests */
struct proc *alloc_process(void);
void free_process(struct proc *p);
int create_process(void (*entry)(void));
void exit_process(int status);
int wait_process(int *status);

#endif