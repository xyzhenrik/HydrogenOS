#pragma once

#include <stddef.h>
#include <stdint.h>

void pmm_init(uintptr_t multiboot_info_addr);
uintptr_t pmm_alloc_page(void);
uintptr_t pmm_alloc_contiguous(size_t pages);
void pmm_free_page(uintptr_t addr);
uintptr_t pmm_first_free_frame(void);
