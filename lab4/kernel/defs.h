#include "types.h"
struct spinlock;
struct proc;
// console.c
// 颜色定义
#define COLOR_BLACK 0

void clear_line(void);
void set_color(int fg, int bg);

// uart.c
void uartinit(void);

// 页表类型定义
typedef uint64 pte_t;
typedef uint64 *pagetable_t;

// 页表操作接口
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
pte_t *walk_lookup(pagetable_t pagetable, uint64 va);
char *strrchr(const char *s, int c);
// 内存查找函数
void *memchr(const void *s, int c, size_t n);

// trap.c
struct trapframe;
void trap_init(void);
void trap_handler(struct trapframe *tf);
void timer_interrupt_handler(void);
void enable_interrupts(void);
void disable_interrupts(void);

// proc.c
struct proc *myproc(void);
void procinit(void);
void scheduler(void);
void yield(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
int kill(int pid);
int wait(uint64 *addr);

// trap.c
void yield(void);
void wakeup(void *chan);