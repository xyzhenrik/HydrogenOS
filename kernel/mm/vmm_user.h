#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Per-process anonymous memory regions (sys_mmap/sys_munmap).
 *
 * All regions live inside a dedicated window that starts at PML4 slot 1, so
 * it never shares page tables with the ELF image, the user stack (both below
 * 4 GiB) or the supervisor-only kernel tables copied into PML4 slot 0.
 */

#define VMM_USER_MMAP_BASE  0x0000008000000000ULL
#define VMM_USER_MMAP_LIMIT 0x0000700000000000ULL
#define VMM_USER_PAGE_SIZE  4096ULL

enum {
    VMM_USER_PROT_READ = 1u << 0,
    VMM_USER_PROT_WRITE = 1u << 1,
    VMM_USER_PROT_EXEC = 1u << 2,
};

/* Permits WRITE|EXEC together; without it such requests fail with -EACCES. */
#define VMM_USER_FLAG_ALLOW_WX (1u << 1)

typedef struct VmmUserRegion VmmUserRegion;

typedef struct VmmUserSpace {
    VmmUserRegion *regions; /* sorted by start address, non-overlapping */
    uint32_t region_count;
} VmmUserSpace;

void vmm_user_init(VmmUserSpace *space);

/*
 * Map `length` bytes (rounded up to pages) of zero-filled memory. A non-zero,
 * page-aligned `hint` is used when that range is free; otherwise the lowest
 * free range is chosen. `prot` may be 0 (PROT_NONE): the pages are committed
 * but every user access faults; otherwise READ is required. `flags` is 0 or
 * VMM_USER_FLAG_ALLOW_WX; WRITE|EXEC without it is refused.
 * Returns 0 and stores the address in *out_addr, or -EINVAL / -EACCES /
 * -ENOMEM.
 */
int vmm_user_map(VmmUserSpace *space, uint64_t *pml4, uintptr_t hint,
                 size_t length, uint32_t prot, uint32_t flags,
                 uintptr_t *out_addr);

/*
 * Unmap [addr, addr + length) (length rounded up to pages). Parts of the
 * range that are not mapped are ignored; regions are trimmed or split.
 * Returns 0, -EINVAL for a malformed range or -ENOMEM if a split needs a
 * region descriptor that cannot be allocated.
 */
int vmm_user_unmap(VmmUserSpace *space, uint64_t *pml4, uintptr_t addr,
                   size_t length);

/*
 * Change the protection of [addr, addr + length) (length rounded up to pages),
 * splitting and re-merging regions as needed. `prot` and `flags` follow
 * vmm_user_map. The whole range must lie inside mapped regions; nothing is
 * changed on failure.
 * Returns 0, -EINVAL for a malformed range or protection, -EACCES for an
 * unflagged WRITE|EXEC request, or -ENOMEM if the range is not fully mapped or
 * a split needs a region descriptor that cannot be allocated.
 */
int vmm_user_protect(VmmUserSpace *space, uint64_t *pml4, uintptr_t addr,
                     size_t length, uint32_t prot, uint32_t flags);

/* Unmap and free every region; must run before the PML4 is destroyed. */
void vmm_user_release(VmmUserSpace *space, uint64_t *pml4);
