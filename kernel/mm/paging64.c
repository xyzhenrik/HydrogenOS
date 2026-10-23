#include "paging.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_PS 0x80

extern uint64_t pml4[];
extern uint64_t pdpt[];

static uint64_t *kernel_pml4_tbl = 0;
static uint64_t *current_pml4_tbl = 0;

static inline void load_cr3(uint64_t phys_addr) {
    __asm__ volatile("mov %0, %%cr3" ::"r"(phys_addr) : "memory");
}

static inline void invlpg(uintptr_t addr) {
    __asm__ volatile("invlpg (%0)" ::"r"(addr) : "memory");
}

static void paging_free_user_pt(uint64_t *pt_tbl) {
    if (!pt_tbl)
        return;
    for (int i = 0; i < 512; ++i) {
        if (!(pt_tbl[i] & PAGE_PRESENT) || !(pt_tbl[i] & PAGE_USER))
            continue;
        uintptr_t phys = (uintptr_t)(pt_tbl[i] & ~0xFFFULL);
        pmm_free_page(phys);
        pt_tbl[i] = 0;
    }
    pmm_free_page((uintptr_t)pt_tbl);
}

static void paging_free_user_pd(uint64_t *pd_tbl) {
    if (!pd_tbl)
        return;
    for (int i = 0; i < 512; ++i) {
        if (!(pd_tbl[i] & PAGE_PRESENT) || !(pd_tbl[i] & PAGE_USER))
            continue;
        if (pd_tbl[i] & PAGE_PS) {
            pd_tbl[i] = 0;
            continue;
        }
        uint64_t *pt_tbl = (uint64_t *)(uintptr_t)(pd_tbl[i] & ~0xFFFULL);
        paging_free_user_pt(pt_tbl);
        pd_tbl[i] = 0;
    }
    pmm_free_page((uintptr_t)pd_tbl);
}

static void paging_free_user_pdpt(uint64_t *pdpt_tbl) {
    if (!pdpt_tbl)
        return;
    for (int i = 0; i < 512; ++i) {
        if (!(pdpt_tbl[i] & PAGE_PRESENT) || !(pdpt_tbl[i] & PAGE_USER))
            continue;
        if (pdpt_tbl[i] & PAGE_PS) {
            pdpt_tbl[i] = 0;
            continue;
        }
        uint64_t *pd_tbl = (uint64_t *)(uintptr_t)(pdpt_tbl[i] & ~0xFFFULL);
        paging_free_user_pd(pd_tbl);
        pdpt_tbl[i] = 0;
    }
    pmm_free_page((uintptr_t)pdpt_tbl);
}

static uint64_t *alloc_table(void) {
    uintptr_t phys = pmm_alloc_page();
    if (!phys)
        return 0;
    uint64_t *tbl = (uint64_t *)(uintptr_t)phys;
    for (int i = 0; i < 512; ++i) {
        tbl[i] = 0;
    }
    return tbl;
}

void paging_init(void) {
    // In boot.S we created identity mappings; we still build a table
    // structure so we can map later.
    kernel_pml4_tbl = pml4;
    current_pml4_tbl = pml4;
    load_cr3((uint64_t)(uintptr_t)current_pml4_tbl);
}

uint64_t *paging_kernel_pml4(void) { return kernel_pml4_tbl; }

uint64_t *paging_current_pml4(void) { return current_pml4_tbl; }

int paging_is_mapped(uintptr_t virt) {
    uint64_t *pml4_root = current_pml4_tbl;
    if (!pml4_root)
        return 0;

    uint16_t pml4_idx = (uint16_t)((virt >> 39) & 0x1FF);
    uint16_t pdpt_idx = (uint16_t)((virt >> 30) & 0x1FF);
    uint16_t pd_idx = (uint16_t)((virt >> 21) & 0x1FF);
    uint16_t pt_idx = (uint16_t)((virt >> 12) & 0x1FF);

    if (!(pml4_root[pml4_idx] & PAGE_PRESENT))
        return 0;
    uint64_t *pdpt_tbl =
        (uint64_t *)(uintptr_t)(pml4_root[pml4_idx] & ~0xFFFULL);
    if (!(pdpt_tbl[pdpt_idx] & PAGE_PRESENT))
        return 0;
    uint64_t *pd_tbl =
        (uint64_t *)(uintptr_t)(pdpt_tbl[pdpt_idx] & ~0xFFFULL);
    if (!(pd_tbl[pd_idx] & PAGE_PRESENT))
        return 0;
    if (pd_tbl[pd_idx] & PAGE_PS)
        return 1;
    uint64_t *pt_tbl = (uint64_t *)(uintptr_t)(pd_tbl[pd_idx] & ~0xFFFULL);
    return (pt_tbl[pt_idx] & PAGE_PRESENT) != 0;
}

void paging_switch(uint64_t *pml4_root) {
    if (!pml4_root)
        return;
    current_pml4_tbl = pml4_root;
    load_cr3((uint64_t)(uintptr_t)pml4_root);
}

uint64_t *paging_create_user_pml4(void) {
    if (!kernel_pml4_tbl)
        return 0;
    uint64_t *new_pml4 = alloc_table();
    if (!new_pml4)
        return 0;
    uint64_t *new_pdpt = alloc_table();
    if (!new_pdpt)
        return 0;

    for (int i = 256; i < 512; ++i) {
        new_pml4[i] = kernel_pml4_tbl[i];
    }
    new_pml4[0] = (uint64_t)(uintptr_t)new_pdpt | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    if (kernel_pml4_tbl[0] & PAGE_PRESENT) {
        uint64_t *kernel_pdpt =
            (uint64_t *)(uintptr_t)(kernel_pml4_tbl[0] & ~0xFFFULL);
        for (int i = 0; i < 512; ++i) {
            if (kernel_pdpt[i] & PAGE_PRESENT) {
                new_pdpt[i] = kernel_pdpt[i] & ~0x4ULL; // supervisor-only
            }
        }
    }
    return new_pml4;
}

void paging_destroy_user_pml4(uint64_t *pml4_root) {
    if (!pml4_root || pml4_root == kernel_pml4_tbl)
        return;

    for (int i = 0; i < 256; ++i) {
        if (!(pml4_root[i] & PAGE_PRESENT) || !(pml4_root[i] & PAGE_USER))
            continue;
        uint64_t *pdpt_tbl = (uint64_t *)(uintptr_t)(pml4_root[i] & ~0xFFFULL);
        paging_free_user_pdpt(pdpt_tbl);
        pml4_root[i] = 0;
    }

    pmm_free_page((uintptr_t)pml4_root);
}

void paging_map_region_pml4(uint64_t *pml4_root, uintptr_t phys, uintptr_t virt,
                            size_t size_bytes, uint32_t flags) {
    if (!pml4_root)
        return;
    uintptr_t end = phys + size_bytes;
    for (uintptr_t addr = phys; addr < end; addr += 0x1000, virt += 0x1000) {
        uint16_t pml4_idx = (uint16_t)((virt >> 39) & 0x1FF);
        uint16_t pdpt_idx = (uint16_t)((virt >> 30) & 0x1FF);
        uint16_t pd_idx = (uint16_t)((virt >> 21) & 0x1FF);
        uint16_t pt_idx = (uint16_t)((virt >> 12) & 0x1FF);

        uint64_t *pdpt_tbl;
        uint64_t upper_flags = PAGE_PRESENT | PAGE_RW;
        if (flags & PAGE_USER)
            upper_flags |= 0x4;
        if (pml4_root[pml4_idx] & PAGE_PRESENT) {
            pdpt_tbl = (uint64_t *)(uintptr_t)(pml4_root[pml4_idx] & ~0xFFFULL);
            if (flags & PAGE_USER)
                pml4_root[pml4_idx] |= 0x4;
        } else {
            pdpt_tbl = alloc_table();
            if (!pdpt_tbl)
                return;
            pml4_root[pml4_idx] = (uint64_t)(uintptr_t)pdpt_tbl | upper_flags;
        }

        uint64_t *pd_tbl;
        if (pdpt_tbl[pdpt_idx] & PAGE_PRESENT) {
            pd_tbl = (uint64_t *)(uintptr_t)(pdpt_tbl[pdpt_idx] & ~0xFFFULL);
            if (flags & PAGE_USER)
                pdpt_tbl[pdpt_idx] |= 0x4;
        } else {
            pd_tbl = alloc_table();
            if (!pd_tbl)
                return;
            pdpt_tbl[pdpt_idx] = (uint64_t)(uintptr_t)pd_tbl | upper_flags;
        }

        uint64_t *pt_tbl;
        if (pd_tbl[pd_idx] & PAGE_PRESENT) {
            if (pd_tbl[pd_idx] & PAGE_PS) {
                // split large page not supported yet
                return;
            }
            pt_tbl = (uint64_t *)(uintptr_t)(pd_tbl[pd_idx] & ~0xFFFULL);
            if (flags & PAGE_USER)
                pd_tbl[pd_idx] |= 0x4;
        } else {
            pt_tbl = alloc_table();
            if (!pt_tbl)
                return;
            pd_tbl[pd_idx] = (uint64_t)(uintptr_t)pt_tbl | upper_flags;
        }

        uint64_t entry_flags = PAGE_PRESENT | PAGE_RW;
        if (flags & PAGE_USER)
            entry_flags |= 0x4;
        if (flags & PAGE_RW)
            entry_flags |= PAGE_RW;
        pt_tbl[pt_idx] = (uint64_t)(addr & ~0xFFFULL) |
                         entry_flags;
        invlpg(virt);
    }
}

void paging_map_region(uintptr_t phys, uintptr_t virt, size_t size_bytes,
                       uint32_t flags) {
    paging_map_region_pml4(current_pml4_tbl, phys, virt, size_bytes, flags);
}

void paging_unmap_region(uintptr_t virt, size_t size_bytes) {
    uint64_t *pml4_root = current_pml4_tbl;
    if (!pml4_root) return;
    uintptr_t end = virt + size_bytes;
    for (uintptr_t addr = virt; addr < end; addr += 0x1000) {
        uint16_t pml4_idx = (uint16_t)((addr >> 39) & 0x1FF);
        uint16_t pdpt_idx = (uint16_t)((addr >> 30) & 0x1FF);
        uint16_t pd_idx = (uint16_t)((addr >> 21) & 0x1FF);
        uint16_t pt_idx = (uint16_t)((addr >> 12) & 0x1FF);

        if (!(pml4_root[pml4_idx] & PAGE_PRESENT)) continue;
        uint64_t *pdpt_tbl = (uint64_t *)(uintptr_t)(pml4_root[pml4_idx] & ~0xFFFULL);
        if (!(pdpt_tbl[pdpt_idx] & PAGE_PRESENT)) continue;
        uint64_t *pd_tbl = (uint64_t *)(uintptr_t)(pdpt_tbl[pdpt_idx] & ~0xFFFULL);
        if (!(pd_tbl[pd_idx] & PAGE_PRESENT)) continue;
        if (pd_tbl[pd_idx] & PAGE_PS) {
            pd_tbl[pd_idx] = 0;
            invlpg(addr);
            continue;
        }
        uint64_t *pt_tbl = (uint64_t *)(uintptr_t)(pd_tbl[pd_idx] & ~0xFFFULL);
        if (pt_tbl[pt_idx] & PAGE_PRESENT) {
            uintptr_t phys = (uintptr_t)(pt_tbl[pt_idx] & ~0xFFFULL);
            pmm_free_page(phys);
            pt_tbl[pt_idx] = 0;
            invlpg(addr);
        }
    }
}

uintptr_t paging_alloc_pages(size_t pages, uint32_t flags) {
    uintptr_t first = 0;
    for (size_t i = 0; i < pages; ++i) {
        uintptr_t phys = pmm_alloc_page();
        if (!phys) {
            return 0;
        }
        if (i == 0)
            first = phys;
        paging_map_region(phys, phys, 4096, flags);
    }
    return first;
}
