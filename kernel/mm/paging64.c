#include "paging.h"
#include "msr.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_PS 0x80
#define PAGE_NX_BIT (1ULL << 63)
#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define MSR_EFER 0xC0000080
#define EFER_NXE (1ULL << 11)
#define CPUID_EXT_MAX 0x80000000u
#define CPUID_EXT_FEATURES 0x80000001u
#define CPUID_EXT_EDX_NX (1u << 20)

#define PAGING_SPAN_2M 0x200000ULL
#define PAGING_SPAN_1G 0x40000000ULL
#define PAGING_SPAN_512G 0x8000000000ULL
#define PAGING_USER_TOP 0x0000800000000000ULL

extern uint64_t pml4[];
extern uint64_t pdpt[];

static uint64_t *kernel_pml4_tbl = 0;
static uint64_t *current_pml4_tbl = 0;
static int nx_enabled = 0;

static inline void load_cr3(uint64_t phys_addr) {
    __asm__ volatile("mov %0, %%cr3" ::"r"(phys_addr) : "memory");
}

static inline void invlpg(uintptr_t addr) {
    __asm__ volatile("invlpg (%0)" ::"r"(addr) : "memory");
}

/* PROT_NONE pages keep their frame but lose PAGE_USER, so ownership is
 * recognised by the software bit instead. */
static int user_leaf_owned(uint64_t entry) {
    return (entry & PAGE_PRESENT) && (entry & (PAGE_USER | PAGE_PROT_NONE));
}

static void paging_free_user_pt(uint64_t *pt_tbl) {
    if (!pt_tbl)
        return;
    for (int i = 0; i < 512; ++i) {
        if (!user_leaf_owned(pt_tbl[i]))
            continue;
        uintptr_t phys = (uintptr_t)(pt_tbl[i] & PAGE_ADDR_MASK);
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

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *edx) {
    uint32_t ebx, ecx;
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(ebx), "=c"(ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

static void paging_enable_nx(void) {
    uint32_t eax, edx;
    cpuid(CPUID_EXT_MAX, &eax, &edx);
    if (eax < CPUID_EXT_FEATURES)
        return;
    cpuid(CPUID_EXT_FEATURES, &eax, &edx);
    if (!(edx & CPUID_EXT_EDX_NX))
        return;
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);
    nx_enabled = 1;
}

void paging_init(void) {
    // In boot.S we created identity mappings; we still build a table
    // structure so we can map later.
    kernel_pml4_tbl = pml4;
    current_pml4_tbl = pml4;
    load_cr3((uint64_t)(uintptr_t)current_pml4_tbl);
    paging_enable_nx();
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

/* Counts the 4 KiB user frames that paging_destroy_user_pml4 would release,
   i.e. the physical memory owned by this address space (tables excluded). */
size_t paging_count_user_pages(const uint64_t *pml4_root) {
    if (!pml4_root || pml4_root == kernel_pml4_tbl)
        return 0;
    size_t count = 0;
    for (int i = 0; i < 256; ++i) {
        if (!(pml4_root[i] & PAGE_PRESENT) || !(pml4_root[i] & PAGE_USER))
            continue;
        const uint64_t *pdpt_tbl =
            (const uint64_t *)(uintptr_t)(pml4_root[i] & ~0xFFFULL);
        for (int j = 0; j < 512; ++j) {
            if (!(pdpt_tbl[j] & PAGE_PRESENT) || !(pdpt_tbl[j] & PAGE_USER) ||
                (pdpt_tbl[j] & PAGE_PS))
                continue;
            const uint64_t *pd_tbl =
                (const uint64_t *)(uintptr_t)(pdpt_tbl[j] & ~0xFFFULL);
            for (int k = 0; k < 512; ++k) {
                if (!(pd_tbl[k] & PAGE_PRESENT) || !(pd_tbl[k] & PAGE_USER) ||
                    (pd_tbl[k] & PAGE_PS))
                    continue;
                const uint64_t *pt_tbl =
                    (const uint64_t *)(uintptr_t)(pd_tbl[k] & ~0xFFFULL);
                for (int l = 0; l < 512; ++l) {
                    if (user_leaf_owned(pt_tbl[l]))
                        ++count;
                }
            }
        }
    }
    return count;
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

/*
 * Intermediate tables are only descended into when they are user-owned:
 * a user PML4 shares supervisor-only kernel tables (copied PDPT entries),
 * and writing through those would leak mappings into every address space.
 */
static int user_table_entry(uint64_t entry) {
    return (entry & PAGE_PRESENT) && (entry & PAGE_USER) && !(entry & PAGE_PS);
}

static uint64_t *user_table_at(uint64_t entry) {
    return (uint64_t *)(uintptr_t)(entry & PAGE_ADDR_MASK);
}

static uint64_t *user_table_get_or_create(uint64_t *entry) {
    if (*entry & PAGE_PRESENT)
        return user_table_entry(*entry) ? user_table_at(*entry) : 0;
    uint64_t *tbl = alloc_table();
    if (!tbl)
        return 0;
    *entry = (uint64_t)(uintptr_t)tbl | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    return tbl;
}

static int table_is_empty(const uint64_t *tbl) {
    for (int i = 0; i < 512; ++i) {
        if (tbl[i])
            return 0;
    }
    return 1;
}

static void flush_if_current(uint64_t *pml4_root, uintptr_t virt) {
    if (pml4_root == current_pml4_tbl)
        invlpg(virt);
}

static uint64_t user_leaf_entry(uintptr_t phys, uint64_t flags) {
    uint64_t entry = (uint64_t)phys | PAGE_PRESENT;
    if (flags & PAGE_PROT_NONE)
        entry |= PAGE_PROT_NONE;
    else
        entry |= PAGE_USER;
    if ((flags & PAGE_RW) && !(flags & PAGE_PROT_NONE))
        entry |= PAGE_RW;
    if (((flags & PAGE_NO_EXEC) || (flags & PAGE_PROT_NONE)) && nx_enabled)
        entry |= PAGE_NX_BIT;
    return entry;
}

int paging_user_map_page(uint64_t *pml4_root, uintptr_t virt, uintptr_t phys,
                         uint64_t flags) {
    if (!pml4_root || pml4_root == kernel_pml4_tbl)
        return -1;
    if ((virt & 0xFFFULL) || (phys & 0xFFFULL) || virt >= PAGING_USER_TOP)
        return -1;

    uint64_t *pdpt_tbl = user_table_get_or_create(&pml4_root[(virt >> 39) & 0x1FF]);
    if (!pdpt_tbl)
        return -1;
    uint64_t *pd_tbl = user_table_get_or_create(&pdpt_tbl[(virt >> 30) & 0x1FF]);
    if (!pd_tbl)
        return -1;
    uint64_t *pt_tbl = user_table_get_or_create(&pd_tbl[(virt >> 21) & 0x1FF]);
    if (!pt_tbl)
        return -1;
    uint64_t *pte = &pt_tbl[(virt >> 12) & 0x1FF];
    if (*pte & PAGE_PRESENT)
        return -1;

    *pte = user_leaf_entry(phys, flags);
    return 0;
}

void paging_user_protect_range(uint64_t *pml4_root, uintptr_t virt,
                               size_t size_bytes, uint64_t flags) {
    if (!pml4_root || pml4_root == kernel_pml4_tbl || size_bytes == 0)
        return;
    uintptr_t addr = virt & ~0xFFFULL;
    uintptr_t end = virt + size_bytes;
    if (end < virt || end > PAGING_USER_TOP)
        end = PAGING_USER_TOP;

    while (addr < end) {
        uint64_t *pml4e = &pml4_root[(addr >> 39) & 0x1FF];
        if (!user_table_entry(*pml4e)) {
            addr = (addr | (PAGING_SPAN_512G - 1)) + 1;
            continue;
        }
        uint64_t *pdpte = &user_table_at(*pml4e)[(addr >> 30) & 0x1FF];
        if (!user_table_entry(*pdpte)) {
            addr = (addr | (PAGING_SPAN_1G - 1)) + 1;
            continue;
        }
        uint64_t *pde = &user_table_at(*pdpte)[(addr >> 21) & 0x1FF];
        uintptr_t chunk_end = (addr | (PAGING_SPAN_2M - 1)) + 1;
        if (user_table_entry(*pde)) {
            uint64_t *pt_tbl = user_table_at(*pde);
            uintptr_t stop = chunk_end < end ? chunk_end : end;
            for (uintptr_t a = addr; a < stop; a += 0x1000) {
                uint64_t *pte = &pt_tbl[(a >> 12) & 0x1FF];
                if (!user_leaf_owned(*pte))
                    continue;
                *pte = user_leaf_entry((uintptr_t)(*pte & PAGE_ADDR_MASK), flags);
                flush_if_current(pml4_root, a);
            }
        }
        addr = chunk_end;
    }
}

void paging_user_unmap_range(uint64_t *pml4_root, uintptr_t virt,
                             size_t size_bytes) {
    if (!pml4_root || pml4_root == kernel_pml4_tbl || size_bytes == 0)
        return;
    uintptr_t addr = virt & ~0xFFFULL;
    uintptr_t end = virt + size_bytes;
    if (end < virt || end > PAGING_USER_TOP)
        end = PAGING_USER_TOP;

    while (addr < end) {
        uint64_t *pml4e = &pml4_root[(addr >> 39) & 0x1FF];
        if (!user_table_entry(*pml4e)) {
            addr = (addr | (PAGING_SPAN_512G - 1)) + 1;
            continue;
        }
        uint64_t *pdpte = &user_table_at(*pml4e)[(addr >> 30) & 0x1FF];
        if (!user_table_entry(*pdpte)) {
            addr = (addr | (PAGING_SPAN_1G - 1)) + 1;
            continue;
        }
        uint64_t *pd_tbl = user_table_at(*pdpte);
        uint64_t *pde = &pd_tbl[(addr >> 21) & 0x1FF];
        uintptr_t chunk_end = (addr | (PAGING_SPAN_2M - 1)) + 1;
        if (user_table_entry(*pde)) {
            uint64_t *pt_tbl = user_table_at(*pde);
            uintptr_t stop = chunk_end < end ? chunk_end : end;
            for (uintptr_t a = addr; a < stop; a += 0x1000) {
                uint64_t *pte = &pt_tbl[(a >> 12) & 0x1FF];
                if (!(*pte & PAGE_PRESENT))
                    continue;
                uintptr_t frame = (uintptr_t)(*pte & PAGE_ADDR_MASK);
                *pte = 0;
                flush_if_current(pml4_root, a);
                pmm_free_page(frame);
            }
            if (table_is_empty(pt_tbl)) {
                *pde = 0;
                flush_if_current(pml4_root, addr);
                pmm_free_page((uintptr_t)pt_tbl);
            }
        }
        addr = chunk_end;
        if ((addr >= end || ((addr >> 21) & 0x1FF) == 0) && table_is_empty(pd_tbl)) {
            *pdpte = 0;
            flush_if_current(pml4_root, addr - PAGING_SPAN_2M);
            pmm_free_page((uintptr_t)pd_tbl);
        }
    }
}
