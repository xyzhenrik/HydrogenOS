#include "usercopy.h"

#include "errno.h"
#include "paging.h"
#include "string.h"

#define USER_PAGE_SIZE 0x1000ULL
#define PAGE_LARGE 0x80ULL
#define PAGE_TABLE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

static int entry_allows(uint64_t entry, UserAccess access) {
    uint64_t required = PAGE_PRESENT | PAGE_USER;
    if (access == USER_ACCESS_WRITE)
        required |= PAGE_RW;
    return (entry & required) == required;
}

static const uint64_t *next_table(uint64_t entry) {
    return (const uint64_t *)(uintptr_t)(entry & PAGE_TABLE_ADDRESS_MASK);
}

/* Mirrors the hardware check for a ring 3 access: the user bit, and for
   writes the writable bit, must be set at every level of the walk. */
static int page_accessible(const uint64_t *root, uintptr_t addr,
                           UserAccess access) {
    uint64_t entry = root[(addr >> 39) & 0x1FF];
    if (!entry_allows(entry, access))
        return 0;
    entry = next_table(entry)[(addr >> 30) & 0x1FF];
    if (!entry_allows(entry, access))
        return 0;
    if (entry & PAGE_LARGE)
        return 1;
    entry = next_table(entry)[(addr >> 21) & 0x1FF];
    if (!entry_allows(entry, access))
        return 0;
    if (entry & PAGE_LARGE)
        return 1;
    return entry_allows(next_table(entry)[(addr >> 12) & 0x1FF], access);
}

int user_range_validate(const void *uptr, size_t len, UserAccess access) {
    if (len == 0)
        return 0;
    uintptr_t start = (uintptr_t)uptr;
    if (start >= USER_ADDRESS_LIMIT || len > USER_ADDRESS_LIMIT - start)
        return -EFAULT;
    const uint64_t *root = paging_current_pml4();
    if (!root)
        return -EFAULT;

    uintptr_t page = start & ~(uintptr_t)(USER_PAGE_SIZE - 1);
    uintptr_t last = (start + len - 1) & ~(uintptr_t)(USER_PAGE_SIZE - 1);
    for (;; page += USER_PAGE_SIZE) {
        if (!page_accessible(root, page, access))
            return -EFAULT;
        if (page == last)
            return 0;
    }
}

int copy_from_user(void *kdst, const void *usrc, size_t len) {
    int rc = user_range_validate(usrc, len, USER_ACCESS_READ);
    if (rc != 0)
        return rc;
    memcpy(kdst, usrc, len);
    return 0;
}

int copy_to_user(void *udst, const void *ksrc, size_t len) {
    int rc = user_range_validate(udst, len, USER_ACCESS_WRITE);
    if (rc != 0)
        return rc;
    memcpy(udst, ksrc, len);
    return 0;
}

long strncpy_from_user(char *kdst, const char *usrc, size_t dst_size) {
    if (dst_size == 0)
        return -ERANGE;
    uintptr_t start = (uintptr_t)usrc;
    for (size_t i = 0; i < dst_size; ++i) {
        uintptr_t addr = start + i;
        /* The string length is unknown, so validate page by page as the
           copy advances; this also catches a wrap into the kernel half. */
        if ((i == 0 || (addr & (USER_PAGE_SIZE - 1)) == 0) &&
            user_range_validate((const void *)addr, 1, USER_ACCESS_READ) != 0)
            return -EFAULT;
        kdst[i] = usrc[i];
        if (kdst[i] == 0)
            return (long)i;
    }
    kdst[dst_size - 1] = 0;
    return -ERANGE;
}
