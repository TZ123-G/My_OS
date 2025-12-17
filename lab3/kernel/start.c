#include "defs.h"
#include "riscv.h"

extern char _bss_start[], _bss_end[];

void main();

// stack0 的值（地址）由链接器自动确定，其定义为数组的根本原因只是为了
// 在 .bss 段中分配一份足够大的空间作为栈空间而已
__attribute__((aligned(16))) char stack0[4096];

void test_physical_memory(void)
{
    pmem_init();
    // 测试基本分配和释放
    void *page1 = alloc_page();
    void *page2 = alloc_page();
    assert(page1 != page2);
    assert(((uint64)page1 & 0xFFF) == 0); // 页对齐检查
    // 测试数据写入
    *(int *)page1 = 0x12345678;
    assert(*(int *)page1 == 0x12345678);
    // 测试释放和重新分配
    free_page(page1);
    void *page3 = alloc_page();
    // page3可能等于page1（取决于分配策略）
    free_page(page2);
    free_page(page3);
}

void test_pagetable(void)
{
    pmem_init();
    pagetable_t pt = create_pagetable();
    // 测试基本映射
    uint64 va = 0x1000000;
    uint64 pa = (uint64)alloc_page();
    assert(map_page(pt, va, pa, PTE_R | PTE_W) == 0);
    // 测试地址转换
    pte_t *pte = walk_lookup(pt, va);
    assert(pte != 0 && (*pte & PTE_V));
    assert(PTE2PA(*pte) == pa);
    // 测试权限位
    assert(*pte & PTE_R);
    assert(*pte & PTE_W);
    assert(!(*pte & PTE_X));
    printf("test success!");
}

void test_virtual_memory(void)
{
    printf("Before enabling paging...\n");

    // 初始化物理内存管理器
    pmem_init();

    // 启用分页
    kvminit();
    kvminithart();

    printf("After enabling paging...\n");

    // 测试内核代码仍然可执行
    // 通过调用一个函数来验证代码执行正常
    printf("Testing kernel code execution... ");
    consoleinit();
    printf("OK\n");

    // 测试内核数据仍然可访问
    // 通过访问全局变量来验证数据访问正常
    printf("Testing kernel data access... ");
    extern char end[];
    printf("end address: %p ", end);
    printf("OK\n");

    // 测试设备访问仍然正常
    // 通过调用UART函数来验证设备访问正常
    printf("Testing device access... ");
    uartinit();
    uart_putc('T');
    uart_putc('e');
    uart_putc('s');
    uart_putc('t');
    uart_putc('\n');
    printf("OK\n");

    // 测试内存分配和页表操作
    printf("Testing memory allocation and page mapping... ");
    void *page = alloc_page();
    assert(page != 0);
    printf("Allocated page at %p ", page);

    // 测试页表映射
    // 注意：0x2000000 区域在内核初始化时被用于 CLINT 设备映射，
    // 直接使用该地址会导致 remap 失败。这里选择一个未被占用的虚拟地址，
    // 并在遇到已映射页时向后寻找一个空闲页。
    uint64 va = 0x3000000;    // 起始虚拟地址候选
    uint64 pa = (uint64)page; // 物理地址
    extern pagetable_t kernel_pagetable;

    // 查找未被映射的虚拟页，最多尝试 1024 页（安全上限）
    int tries = 0;
    while (walkaddr(kernel_pagetable, va) != 0 && tries < 1024)
    {
        va += PGSIZE;
        tries++;
    }
    if (tries >= 1024)
    {
        panic("test_virtual_memory: cannot find free virtual page to map");
    }
    printf("test_virtual_memory: mapping page at va=%p -> pa=%p\n", va, pa);
    assert(map_page(kernel_pagetable, va, pa, PTE_R | PTE_W | PTE_X) == 0);

    // 测试地址转换
    uint64 converted_pa = walkaddr(kernel_pagetable, va);
    assert(converted_pa == pa);

    printf("Page mapping and address translation OK\n");

    printf("Virtual memory test completed successfully!\n");
}

void start()
{
    // 清零 .bss 段
    for (char *p = _bss_start; p < _bss_end; p++)
    {
        *p = 0;
    }

    main();
}