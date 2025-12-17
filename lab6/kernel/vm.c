#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "param.h"
#include "printf.h"
#include "defs.h"

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
// 全局内核页表
pagetable_t kernel_pagetable;

// 虚拟地址空间限制（Sv39规范）
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

// 从虚拟地址提取各级VPN
#define VPN_SHIFT(level) (12 + 9 * (level))
#define VPN(va, level) (((va) >> VPN_SHIFT(level)) & 0x1FF)

// 调试宏
#define VM_DEBUG 1
#if VM_DEBUG
#define vm_debug(fmt, ...) printf("[VM] " fmt, ##__VA_ARGS__)
#else
#define vm_debug(fmt, ...)
#endif

static struct pagetable_stats pt_stats;

/**
 * 页表遍历函数 - 查找或创建页表项
 * @param pagetable: 根页表指针
 * @param va: 虚拟地址
 * @param alloc: 是否分配新页表
 * @return: 页表项指针，失败返回0
 */
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
    if (va >= MAXVA)
    {
        vm_debug("walk: virtual address %p too large\n", va);
        return 0;
    }

    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pagetable[VPN(va, level)];

        if (*pte & PTE_V)
        {
            // 页表项有效，进入下一级
            pagetable = (pagetable_t)PTE2PA(*pte);
        }
        else
        {
            // 页表项无效
            if (!alloc)
            {
                return 0;
            }

            // 分配新页表
            pagetable = (pagetable_t)alloc_page();
            if (pagetable == 0)
            {
                vm_debug("walk: kalloc failed for level %d page table\n", level);
                return 0;
            }

            // 清零新页表
            memset(pagetable, 0, PGSIZE);
            pt_stats.total_pt_pages++;

            // 设置父页表项
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }

    return &pagetable[VPN(va, 0)];
}

/**
 * 仅查找页表项，不分配新页表
 */
pte_t *
walk_lookup(pagetable_t pagetable, uint64 va)
{
    return walk(pagetable, va, 0);
}

/**
 * 创建并分配新页表项
 */
pte_t *
walk_create(pagetable_t pagetable, uint64 va)
{
    return walk(pagetable, va, 1);
}

/**
 * 创建空的页表
 */
pagetable_t
create_pagetable(void)
{
    pagetable_t pagetable = (pagetable_t)alloc_page();
    if (pagetable == 0)
    {
        vm_debug("create_pagetable: kalloc failed\n");
        return 0;
    }

    memset(pagetable, 0, PGSIZE);
    pt_stats.total_pt_pages++;

    vm_debug("Created new pagetable at %p\n", pagetable);
    return pagetable;
}

/**
 * 建立页表映射
 * @param pagetable: 页表
 * @param va: 虚拟地址起始
 * @param size: 映射大小
 * @param pa: 物理地址起始
 * @param perm: 权限位
 * @return: 0成功，-1失败
 */
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
    uint64 a, last;
    pte_t *pte;

    if (size == 0)
    {
        vm_debug("mappages: zero size\n");
        return -1;
    }

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);

    vm_debug("mappages: va=%p to pa=%p, size=%d, perm=0x%x\n",
             va, pa, size, perm);

    for (;;)
    {
        // 查找或创建页表项
        if ((pte = walk_create(pagetable, a)) == 0)
        {
            vm_debug("mappages: walk_create failed for va=%p\n", a);
            return -1;
        }

        // 检查是否已映射
        if (*pte & PTE_V)
        {
            vm_debug("mappages: remap detected at va=%p\n", a);
            return -1;
        }

        // 建立映射
        *pte = PA2PTE(pa) | perm | PTE_V;

        pt_stats.total_mappings++;

        if (a == last)
        {
            break;
        }
        a += PGSIZE;
        pa += PGSIZE;
    }

    return 0;
}

/**
 * 建立单页映射
 */
int map_page(pagetable_t pagetable, uint64 va, uint64 pa, int perm)
{
    return mappages(pagetable, va, PGSIZE, pa, perm);
}

/**
 * 取消映射
 */
void unmap_page(pagetable_t pagetable, uint64 va)
{
    pte_t *pte = walk_lookup(pagetable, va);
    if (pte == 0)
    {
        return;
    }

    if (*pte & PTE_V)
    {
        *pte = 0; // 清除页表项
        pt_stats.total_mappings--;

        // 注意：这里不释放物理页面，由调用者负责
        vm_debug("unmap_page: unmapped va=%p\n", va);
    }
}

/**
 * 释放页表及其所有映射
 * 递归释放所有级别的页表
 */
static void
free_pagetable_recursive(pagetable_t pagetable, int level)
{
    // 遍历所有512个页表项
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = pagetable[i];
        if (pte & PTE_V)
        {
            if (level > 0)
            {
                // 中间级页表，递归释放
                free_pagetable_recursive((pagetable_t)PTE2PA(pte), level - 1);
            }
            // 注意：不释放叶子节点指向的物理页面
        }
    }

    // 释放当前页表页面
    free_page((void *)pagetable);
    pt_stats.total_pt_pages--;
}

/**
 * 销毁整个页表
 */
void destroy_pagetable(pagetable_t pagetable)
{
    if (pagetable == 0)
    {
        return;
    }

    vm_debug("destroy_pagetable: freeing pagetable %p\n", pagetable);
    free_pagetable_recursive(pagetable, 2); // Sv39有3级，从第2级开始
}

/**
 * 虚拟地址到物理地址转换
 */
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;
    uint64 pa;

    if (va >= MAXVA)
    {
        return 0;
    }

    pte = walk_lookup(pagetable, va);
    if (pte == 0)
    {
        return 0;
    }

    if ((*pte & PTE_V) == 0)
    {
        return 0;
    }

    pa = PTE2PA(*pte);

    // 加上页内偏移
    pa |= (va & (PGSIZE - 1));

    return pa;
}

/**
 * 复制页表映射（用于创建进程时）
 */
int copy_pagetable_mapping(pagetable_t old, pagetable_t new, uint64 va, uint64 size)
{
    uint64 a, last;
    pte_t *pte;

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);

    for (;;)
    {
        pte = walk_lookup(old, a);
        if (pte && (*pte & PTE_V))
        {
            // 获取物理地址和权限
            uint64 pa = PTE2PA(*pte);
            int perm = PTE_FLAGS(*pte) & ~PTE_W; // 清除写权限，用于COW

            // 在新页表中建立映射
            if (map_page(new, a, pa, perm) < 0)
            {
                return -1;
            }
        }

        if (a == last)
        {
            break;
        }
        a += PGSIZE;
    }

    return 0;
}

/**
 * 初始化内核页表
 */
void kvminit(void)
{
    vm_debug("Initializing kernel page table\n");

    kernel_pagetable = create_pagetable();
    if (kernel_pagetable == 0)
    {
        panic("kvminit: create_pagetable failed");
    }

    pt_stats.kernel_pt_pages = 1; // 根页表

    // 获取内核段边界（这些符号在链接脚本中定义）
    extern char etext[]; // 内核代码结束
    extern char end[];   // 内核数据结束

    vm_debug("Kernel segments: text=0x%p-0x%p, data=0x%p-0x%p\n",
             KERNBASE, etext, etext, end);

    // 映射内核代码段 (R+X权限)
    if (mappages(kernel_pagetable, KERNBASE, (uint64)etext - KERNBASE,
                 KERNBASE, PTE_R | PTE_X) < 0)
    {
        panic("kvminit: kernel text mapping failed");
    }

    // 映射内核数据段 (R+W权限)
    uint64 data_start = PGROUNDUP((uint64)etext); // 从下一个页面边界开始
    if (data_start < (uint64)end)
    { // 只有在有数据段需要映射时才映射
        if (mappages(kernel_pagetable, data_start, (uint64)end - data_start,
                     data_start, PTE_R | PTE_W) < 0)
        {
            panic("kvminit: kernel data mapping failed");
        }
    }

    // 映射剩余的物理内存
    uint64 remaining_start = PGROUNDUP((uint64)end);
    if (remaining_start < PHYSTOP)
    {
        if (mappages(kernel_pagetable, remaining_start, PHYSTOP - remaining_start,
                     remaining_start, PTE_R | PTE_W) < 0)
        {
            panic("kvminit: remaining memory mapping failed");
        }
    }

    // 映射设备内存
    // UART
    if (mappages(kernel_pagetable, UART0, PGSIZE, UART0, PTE_R | PTE_W) < 0)
    {
        panic("kvminit: UART mapping failed");
    }

    // CLINT (core-local interruptor) - 包含 mtime/mtimecmp
    // 映射 0x2000000 大小的设备区域（映射 64KB）
    if (mappages(kernel_pagetable, CLINT, 0x10000, CLINT, PTE_R | PTE_W) < 0)
    {
        panic("kvminit: CLINT mapping failed");
    }

    vm_debug("Kernel page table initialized successfully\n");
}

/**
 * 激活内核页表
 */
void kvminithart(void)
{
    vm_debug("Activating kernel page table\n");

    // 写入SATP寄存器
    w_satp(MAKE_SATP(kernel_pagetable));

    // 刷新TLB
    sfence_vma();

    vm_debug("Virtual memory enabled\n");
}