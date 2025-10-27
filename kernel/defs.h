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

// printf.c
void printf(char *fmt, ...);
void panic(char *s);
void printf_color(int fg, int bg, char *fmt, ...);

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
// 页表类型定义
typedef uint64 pte_t;
typedef uint64 *pagetable_t;
    // 页表统计信息
struct pagetable_stats
{
    uint64 total_pt_pages;
    uint64 total_mappings;
    uint64 kernel_pt_pages;
};
    // 页表操作接口
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
pte_t *walk_lookup(pagetable_t pagetable, uint64 va);
pte_t *walk_create(pagetable_t pagetable, uint64 va);
pagetable_t create_pagetable(void);
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);
int map_page(pagetable_t pagetable, uint64 va, uint64 pa, int perm);
void unmap_page(pagetable_t pagetable, uint64 va);
void destroy_pagetable(pagetable_t pagetable);
uint64 walkaddr(pagetable_t pagetable, uint64 va);
int copy_pagetable_mapping(pagetable_t old, pagetable_t new, uint64 va, uint64 size);
    // 内核虚拟内存管理
void kvminit(void);
void kvminithart(void);

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
