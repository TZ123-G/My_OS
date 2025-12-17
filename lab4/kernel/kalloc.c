#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

extern char end[];
// 空闲页链表节点
struct run
{
    struct run *next;
};

// 内存管理全局状态
struct
{
    struct run *freelist;
    uint64 total_pages;     // 总页面数
    uint64 allocated_pages; // 已分配页面数
    uint64 free_pages;      // 空闲页面数
} kmem;

// 初始化物理内存管理器
void pmem_init(void)
{
    // initlock(&kmem.lock, "kmem");

    // 初始化统计信息
    kmem.total_pages = 0;
    kmem.allocated_pages = 0;
    kmem.free_pages = 0;

    // 从内核结束地址到PHYSTOP的内存加入空闲链表
    char *p = (char *)PGROUNDUP((uint64)end);
    for (; p + PGSIZE <= (char *)PHYSTOP; p += PGSIZE)
    {
        free_page(p);
        kmem.total_pages++;
    }

    kmem.free_pages = kmem.total_pages;
    printf("Physical memory initialized: %d pages available\n", kmem.free_pages);
}

// 分配单页物理内存
void *alloc_page(void)
{
    struct run *r;

    // acquire(&kmem.lock);
    r = kmem.freelist;
    if (r)
    {
        kmem.freelist = r->next;
        kmem.allocated_pages++;
        kmem.free_pages--;
    }
    // release(&kmem.lock);

    if (r)
    {
        // 填充调试模式值，帮助检测未初始化内存
        memset((char *)r, 0xAA, PGSIZE);
    }

    return (void *)r;
}

// 分配连续n页物理内存
void *alloc_pages(int n)
{
    if (n <= 0)
        return 0;

    if (n == 1)
        return alloc_page();

    void *pages[n];
    int consecutive = 0;

    for (int attempt = 0; attempt < 10; attempt++)
    { // 最多尝试10次
        // 分配第一页
        pages[0] = alloc_page();
        if (!pages[0])
            return 0;

        // 尝试分配连续的后续页面
        consecutive = 1;
        for (int i = 1; i < n; i++)
        {
            pages[i] = alloc_page();
            if (!pages[i] ||
                (uint64)pages[i] != (uint64)pages[i - 1] + PGSIZE)
            {
                // 不连续，释放已分配的页面
                for (int j = 0; j < consecutive; j++)
                {
                    free_page(pages[j]);
                }
                consecutive = 0;
                break;
            }
            consecutive++;
        }

        if (consecutive == n)
        {
            return pages[0]; // 成功分配到连续页面
        }
    }

    return 0; // 多次尝试后仍失败
}

// 释放单页物理内存
void free_page(void *pa)
{
    struct run *r;

    // 参数检查
    if (!pa)
        panic("free_page: null pointer");

    if (((uint64)pa % PGSIZE) != 0)
        panic("free_page: not page aligned");

    if ((char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("free_page: out of range");

    // 安全检查：清空页面内容，防止信息泄漏
    memset(pa, 0, PGSIZE);

    r = (struct run *)pa;

    // acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    kmem.allocated_pages--;
    kmem.free_pages++;
    // release(&kmem.lock);
}