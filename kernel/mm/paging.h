#pragma once

#include <stddef.h>
#include <stdint.h>

void paging_init(void);
void paging_map_region(uintptr_t phys, uintptr_t virt, size_t size_bytes,
                       uint32_t flags);
void paging_unmap_region(uintptr_t virt, size_t size_bytes);
uintptr_t paging_alloc_pages(size_t pages, uint32_t flags);
void paging_switch(uint64_t *pml4);
uint64_t *paging_kernel_pml4(void);
uint64_t *paging_create_user_pml4(void);
void paging_destroy_user_pml4(uint64_t *pml4);
uint64_t *paging_current_pml4(void);
int paging_is_mapped(uintptr_t virt);
void paging_map_region_pml4(uint64_t *pml4, uintptr_t phys, uintptr_t virt,
                            size_t size_bytes, uint32_t flags);

#define PAGE_RW 0x2
#define PAGE_PRESENT 0x1
#define PAGE_USER 0x4
