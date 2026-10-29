#include "vmm_user.h"

#include "errno.h"
#include "kheap.h"
#include "paging.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

/* Bounds kernel-heap use per process; adjacent regions merge, so real
 * workloads stay far below this. */
#define VMM_USER_MAX_REGIONS 4096u
#define VMM_USER_PROT_MASK \
    (VMM_USER_PROT_READ | VMM_USER_PROT_WRITE | VMM_USER_PROT_EXEC)

struct VmmUserRegion {
    uintptr_t start;
    uintptr_t end; /* exclusive */
    uint32_t prot;
    VmmUserRegion *next;
};

void vmm_user_init(VmmUserSpace *space) {
    if (!space)
        return;
    space->regions = 0;
    space->region_count = 0;
}

static size_t page_round_up(size_t length) {
    return (length + (VMM_USER_PAGE_SIZE - 1)) & ~(size_t)(VMM_USER_PAGE_SIZE - 1);
}

static uint64_t prot_to_page_flags(uint32_t prot) {
    if (prot == 0)
        return PAGE_PROT_NONE;
    uint64_t flags = PAGE_USER;
    if (prot & VMM_USER_PROT_WRITE)
        flags |= PAGE_RW;
    if (!(prot & VMM_USER_PROT_EXEC))
        flags |= PAGE_NO_EXEC;
    return flags;
}

static int prot_check(uint32_t prot, uint32_t flags) {
    if ((flags & ~VMM_USER_FLAG_ALLOW_WX) || (prot & ~VMM_USER_PROT_MASK))
        return -EINVAL;
    if (prot != 0 && !(prot & VMM_USER_PROT_READ))
        return -EINVAL;
    if ((prot & VMM_USER_PROT_WRITE) && (prot & VMM_USER_PROT_EXEC) &&
        !(flags & VMM_USER_FLAG_ALLOW_WX))
        return -EACCES;
    return 0;
}

static int range_is_free(const VmmUserSpace *space, uintptr_t start,
                         uintptr_t end) {
    for (const VmmUserRegion *r = space->regions; r && r->start < end; r = r->next) {
        if (r->end > start)
            return 0;
    }
    return 1;
}

static int find_free_range(const VmmUserSpace *space, size_t length,
                           uintptr_t *out_start) {
    uintptr_t candidate = VMM_USER_MMAP_BASE;
    for (const VmmUserRegion *r = space->regions; r; r = r->next) {
        if (r->start - candidate >= length) {
            *out_start = candidate;
            return 0;
        }
        candidate = r->end;
    }
    if (VMM_USER_MMAP_LIMIT - candidate >= length) {
        *out_start = candidate;
        return 0;
    }
    return -ENOMEM;
}

static void find_neighbors(const VmmUserSpace *space, uintptr_t start,
                           VmmUserRegion **out_prev, VmmUserRegion **out_next) {
    VmmUserRegion *prev = 0;
    VmmUserRegion *next = space->regions;
    while (next && next->start < start) {
        prev = next;
        next = next->next;
    }
    *out_prev = prev;
    *out_next = next;
}

/* Frames are cleared through the kernel's identity map of low physical
 * memory, which is present (supervisor-only) in every address space. */
static void zero_frame(uintptr_t phys) {
    uint64_t *words = (uint64_t *)phys;
    for (size_t i = 0; i < VMM_USER_PAGE_SIZE / sizeof(uint64_t); ++i)
        words[i] = 0;
}

static int populate(uint64_t *pml4, uintptr_t start, uintptr_t end,
                    uint64_t page_flags) {
    for (uintptr_t addr = start; addr < end; addr += VMM_USER_PAGE_SIZE) {
        uintptr_t phys = pmm_alloc_page();
        int mapped = 0;
        if (phys) {
            zero_frame(phys);
            mapped = paging_user_map_page(pml4, addr, phys, page_flags) == 0;
            if (!mapped)
                pmm_free_page(phys);
        }
        if (!mapped) {
            /* Covers the failing page too, so tables created for it are pruned. */
            paging_user_unmap_range(pml4, start, addr + VMM_USER_PAGE_SIZE - start);
            return -ENOMEM;
        }
    }
    return 0;
}

int vmm_user_map(VmmUserSpace *space, uint64_t *pml4, uintptr_t hint,
                 size_t length, uint32_t prot, uint32_t flags,
                 uintptr_t *out_addr) {
    if (!space || !pml4 || !out_addr)
        return -EINVAL;
    if (length == 0)
        return -EINVAL;
    int rc = prot_check(prot, flags);
    if (rc != 0)
        return rc;
    if (hint & (VMM_USER_PAGE_SIZE - 1))
        return -EINVAL;
    if (length > VMM_USER_MMAP_LIMIT - VMM_USER_MMAP_BASE)
        return -ENOMEM;
    length = page_round_up(length);
    if (length / VMM_USER_PAGE_SIZE > pmm_free_page_count())
        return -ENOMEM;

    uintptr_t start;
    if (hint >= VMM_USER_MMAP_BASE && hint < VMM_USER_MMAP_LIMIT &&
        length <= VMM_USER_MMAP_LIMIT - hint &&
        range_is_free(space, hint, hint + length)) {
        start = hint;
    } else if (find_free_range(space, length, &start) != 0) {
        return -ENOMEM;
    }
    uintptr_t end = start + length;

    VmmUserRegion *prev;
    VmmUserRegion *next;
    find_neighbors(space, start, &prev, &next);
    int merge_prev = prev && prev->end == start && prev->prot == prot;
    int merge_next = next && next->start == end && next->prot == prot;

    VmmUserRegion *node = 0;
    if (!merge_prev && !merge_next) {
        if (space->region_count >= VMM_USER_MAX_REGIONS)
            return -ENOMEM;
        node = (VmmUserRegion *)kalloc(sizeof(*node));
        if (!node)
            return -ENOMEM;
    }

    if (populate(pml4, start, end, prot_to_page_flags(prot)) != 0) {
        if (node)
            kfree(node);
        return -ENOMEM;
    }

    if (merge_prev && merge_next) {
        prev->end = next->end;
        prev->next = next->next;
        kfree(next);
        space->region_count--;
    } else if (merge_prev) {
        prev->end = end;
    } else if (merge_next) {
        next->start = start;
    } else {
        node->start = start;
        node->end = end;
        node->prot = prot;
        node->next = next;
        if (prev)
            prev->next = node;
        else
            space->regions = node;
        space->region_count++;
    }
    *out_addr = start;
    return 0;
}

int vmm_user_unmap(VmmUserSpace *space, uint64_t *pml4, uintptr_t addr,
                   size_t length) {
    if (!space || !pml4)
        return -EINVAL;
    if (length == 0 || (addr & (VMM_USER_PAGE_SIZE - 1)))
        return -EINVAL;
    if (addr < VMM_USER_MMAP_BASE || addr >= VMM_USER_MMAP_LIMIT ||
        length > VMM_USER_MMAP_LIMIT - addr)
        return -EINVAL;
    /* Cannot pass the limit: both addr and the limit are page aligned. */
    uintptr_t end = addr + page_round_up(length);

    VmmUserRegion *spare = 0;
    for (const VmmUserRegion *r = space->regions; r && r->start < end; r = r->next) {
        if (r->start < addr && r->end > end) {
            if (space->region_count >= VMM_USER_MAX_REGIONS)
                return -ENOMEM;
            spare = (VmmUserRegion *)kalloc(sizeof(*spare));
            if (!spare)
                return -ENOMEM;
            break;
        }
    }

    VmmUserRegion **link = &space->regions;
    while (*link && (*link)->start < end) {
        VmmUserRegion *r = *link;
        if (r->end <= addr) {
            link = &r->next;
            continue;
        }
        uintptr_t cut_start = r->start > addr ? r->start : addr;
        uintptr_t cut_end = r->end < end ? r->end : end;
        paging_user_unmap_range(pml4, cut_start, cut_end - cut_start);

        if (r->start >= addr && r->end <= end) {
            *link = r->next;
            kfree(r);
            space->region_count--;
            continue;
        }
        if (r->start < addr && r->end > end) {
            spare->start = end;
            spare->end = r->end;
            spare->prot = r->prot;
            spare->next = r->next;
            r->end = addr;
            r->next = spare;
            space->region_count++;
            break;
        }
        if (r->start < addr)
            r->end = addr;
        else
            r->start = end;
        link = &r->next;
    }
    return 0;
}

/* Splits the region containing `point` into two at that address; `spare` is
 * the descriptor for the upper half. Returns 1 if it was consumed. */
static int split_at(VmmUserSpace *space, uintptr_t point, VmmUserRegion *spare) {
    for (VmmUserRegion *r = space->regions; r && r->start < point; r = r->next) {
        if (r->end <= point)
            continue;
        spare->start = point;
        spare->end = r->end;
        spare->prot = r->prot;
        spare->next = r->next;
        r->end = point;
        r->next = spare;
        space->region_count++;
        return 1;
    }
    return 0;
}

/* Re-joins neighbouring regions with equal protection around [start, end]. */
static void merge_around(VmmUserSpace *space, uintptr_t start, uintptr_t end) {
    VmmUserRegion *prev;
    VmmUserRegion *next;
    find_neighbors(space, start, &prev, &next);
    VmmUserRegion *r = prev ? prev : space->regions;
    while (r && r->next && r->start <= end) {
        VmmUserRegion *following = r->next;
        if (r->end == following->start && r->prot == following->prot) {
            r->end = following->end;
            r->next = following->next;
            kfree(following);
            space->region_count--;
        } else {
            r = following;
        }
    }
}

int vmm_user_protect(VmmUserSpace *space, uint64_t *pml4, uintptr_t addr,
                     size_t length, uint32_t prot, uint32_t flags) {
    if (!space || !pml4)
        return -EINVAL;
    if (length == 0 || (addr & (VMM_USER_PAGE_SIZE - 1)))
        return -EINVAL;
    if (addr < VMM_USER_MMAP_BASE || addr >= VMM_USER_MMAP_LIMIT ||
        length > VMM_USER_MMAP_LIMIT - addr)
        return -EINVAL;
    int rc = prot_check(prot, flags);
    if (rc != 0)
        return rc;
    /* Cannot pass the limit: both addr and the limit are page aligned. */
    uintptr_t end = addr + page_round_up(length);

    /* The range must be covered without gaps; every page that is not a whole
     * region needs one split, so at most two extra descriptors are used. */
    uintptr_t covered = addr;
    uint32_t splits = 0;
    for (const VmmUserRegion *r = space->regions; r && r->start < end; r = r->next) {
        if (r->end <= addr)
            continue;
        if (r->start > covered)
            break;
        if (r->start < addr)
            splits++;
        if (r->end > end)
            splits++;
        covered = r->end;
    }
    if (covered < end)
        return -ENOMEM;
    if (space->region_count + splits > VMM_USER_MAX_REGIONS)
        return -ENOMEM;

    VmmUserRegion *spares[2] = {0, 0};
    for (uint32_t i = 0; i < splits; ++i) {
        spares[i] = (VmmUserRegion *)kalloc(sizeof(*spares[i]));
        if (!spares[i]) {
            while (i > 0)
                kfree(spares[--i]);
            return -ENOMEM;
        }
    }

    paging_user_protect_range(pml4, addr, end - addr, prot_to_page_flags(prot));

    uint32_t used = 0;
    if (split_at(space, addr, spares[used]))
        used++;
    if (split_at(space, end, spares[used]))
        used++;
    for (VmmUserRegion *r = space->regions; r && r->start < end; r = r->next) {
        if (r->start >= addr)
            r->prot = prot;
    }
    merge_around(space, addr, end);
    for (uint32_t i = used; i < splits; ++i)
        kfree(spares[i]);
    return 0;
}

void vmm_user_release(VmmUserSpace *space, uint64_t *pml4) {
    if (!space)
        return;
    VmmUserRegion *r = space->regions;
    while (r) {
        VmmUserRegion *next = r->next;
        if (pml4)
            paging_user_unmap_range(pml4, r->start, r->end - r->start);
        kfree(r);
        r = next;
    }
    space->regions = 0;
    space->region_count = 0;
}
