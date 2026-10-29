#pragma once

#include <stddef.h>
#include <stdint.h>

void paging_init(void);
void paging_map_region(uintptr_t phys, uintptr_t virt, size_t size_bytes,
                       uint32_t flags);
uintptr_t paging_alloc_pages(size_t pages, uint32_t flags);
void paging_switch(uint64_t *pml4);
uint64_t *paging_kernel_pml4(void);
uint64_t *paging_create_user_pml4(void);
void paging_destroy_user_pml4(uint64_t *pml4);
size_t paging_count_user_pages(const uint64_t *pml4);
uint64_t *paging_current_pml4(void);
int paging_is_mapped(uintptr_t virt);
/*
 * True when every page of [virt, virt + len) is present and user-accessible
 * in 'pml4' (and writable when 'writable'). Lets the kernel touch a user
 * buffer without taking a fault. len must be non-zero.
 */
int paging_user_range_ok(const uint64_t *pml4, uintptr_t virt, size_t len,
                         int writable);
void paging_map_region_pml4(uint64_t *pml4, uintptr_t phys, uintptr_t virt,
                            size_t size_bytes, uint32_t flags);
/*
 * Map one 4 KiB user page into a user PML4; fails (-1) if the page is already
 * mapped, a table cannot be allocated, or the walk would cross a
 * supervisor-owned (shared kernel) table.
 */
int paging_user_map_page(uint64_t *pml4, uintptr_t virt, uintptr_t phys,
                         uint64_t flags);
/* Unmap user pages in a range, free their frames and prune empty PTs/PDs. */
void paging_user_unmap_range(uint64_t *pml4, uintptr_t virt, size_t size_bytes);

#define PAGE_RW 0x2
#define PAGE_PRESENT 0x1
#define PAGE_USER 0x4
#define PAGE_NO_EXEC (1ULL << 63)
