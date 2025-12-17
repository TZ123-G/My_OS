#ifndef VM_H
#define VM_H

#include "types.h"
#include "riscv.h"

// Sv39 virtual-address-limit (1 << (9+9+9+12-1))
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
// 页表项与页表类型
#ifndef __ASSEMBLER__
typedef uint64 pte_t;
typedef uint64 *pagetable_t;

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
int copy_pagetable_mapping(pagetable_t old, pagetable_t newpt, uint64 va, uint64 size);

// 内核页表初始化/激活
void kvminit(void);
void kvminithart(void);
#endif /* __ASSEMBLER__ */

#endif // VM_H
