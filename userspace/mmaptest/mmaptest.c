#include "mini_libc.h"
#include "syscall.h"

#include <stdint.h>

#define PAGE_SIZE       4096ul
#define MMAP_AREA_BASE  0x0000008000000000ul
#define MMAP_AREA_LIMIT 0x0000700000000000ul
#define RC_ENOMEM       (-12l)
#define RC_EINVAL       (-22l)
#define PROT_RW         (MMAP_PROT_READ | MMAP_PROT_WRITE)

#define MANY_PAGES      256
#define REUSE_CYCLES    24
#define REUSE_BYTES     (4ul * 1024ul * 1024ul)
#define SMALL_COUNT     1500

static uintptr_t many_pages[MANY_PAGES];
static unsigned char *small_ptrs[SMALL_COUNT];
static unsigned small_sizes[SMALL_COUNT];
static uint32_t rng_state = 0x2545F491u;

static int fail(const char *what) {
    write(1, "MMAP FAIL: ", 11);
    write(1, what, strlen(what));
    write(1, "\n", 1);
    return 1;
}

static uint32_t rng_next(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static long map_rw(uintptr_t hint, unsigned long len) {
    return sys_mmap((void *)hint, len, PROT_RW, 0);
}

static int in_area(long addr, unsigned long len) {
    uintptr_t a = (uintptr_t)addr;
    return addr > 0 && (a & (PAGE_SIZE - 1)) == 0 && a >= MMAP_AREA_BASE &&
           a + len <= MMAP_AREA_LIMIT;
}

static int all_bytes(const unsigned char *p, unsigned long len, unsigned char v) {
    for (unsigned long i = 0; i < len; ++i) {
        if (p[i] != v)
            return 0;
    }
    return 1;
}

static int check_errors(void) {
    if (sys_mmap(0, 0, PROT_RW, 0) != RC_EINVAL)
        return fail("len 0 accepted");
    if (sys_mmap(0, PAGE_SIZE, 0, 0) != RC_EINVAL)
        return fail("prot none accepted");
    if (sys_mmap(0, PAGE_SIZE, MMAP_PROT_WRITE, 0) != RC_EINVAL)
        return fail("write-only accepted");
    if (sys_mmap(0, PAGE_SIZE, PROT_RW | 0x8u, 0) != RC_EINVAL)
        return fail("unknown prot accepted");
    if (sys_mmap(0, PAGE_SIZE, PROT_RW, 1u) != RC_EINVAL)
        return fail("unknown flags accepted");
    if (map_rw(MMAP_AREA_BASE + 1, PAGE_SIZE) != RC_EINVAL)
        return fail("unaligned hint accepted");
    if (map_rw(0, 1ul << 62) != RC_ENOMEM)
        return fail("huge len accepted");
    if (map_rw(0, 1ul << 30) != RC_ENOMEM)
        return fail("len beyond ram accepted");

    if (sys_munmap((void *)MMAP_AREA_BASE, 0) != RC_EINVAL)
        return fail("munmap len 0 accepted");
    if (sys_munmap((void *)(MMAP_AREA_BASE + 8), PAGE_SIZE) != RC_EINVAL)
        return fail("munmap unaligned accepted");
    if (sys_munmap((void *)0x80000000ul, PAGE_SIZE) != RC_EINVAL)
        return fail("munmap of elf image accepted");
    if (sys_munmap((void *)MMAP_AREA_LIMIT, PAGE_SIZE) != RC_EINVAL)
        return fail("munmap past area accepted");
    if (sys_munmap((void *)MMAP_AREA_BASE, ~0ul) != RC_EINVAL)
        return fail("munmap overflow accepted");
    if (sys_munmap((void *)(MMAP_AREA_LIMIT - 16 * PAGE_SIZE), 16 * PAGE_SIZE) != 0)
        return fail("munmap of hole failed");
    return 0;
}

static int check_basic(void) {
    long rc = map_rw(0, 3 * PAGE_SIZE - 100);
    if (!in_area(rc, 3 * PAGE_SIZE))
        return fail("map basic");
    unsigned char *p = (unsigned char *)rc;
    if (!all_bytes(p, 3 * PAGE_SIZE, 0))
        return fail("map not zero-filled");
    for (unsigned long i = 0; i < 3 * PAGE_SIZE; ++i)
        p[i] = (unsigned char)(i * 13u + 7u);
    for (unsigned long i = 0; i < 3 * PAGE_SIZE; ++i) {
        if (p[i] != (unsigned char)(i * 13u + 7u))
            return fail("readback");
    }
    if (sys_munmap(p, 3 * PAGE_SIZE) != 0)
        return fail("unmap basic");

    rc = map_rw((uintptr_t)p, 3 * PAGE_SIZE);
    if (rc != (long)(uintptr_t)p)
        return fail("hint not honoured");
    if (!all_bytes(p, 3 * PAGE_SIZE, 0))
        return fail("reused frames not zeroed");

    long other = map_rw((uintptr_t)p, PAGE_SIZE);
    if (!in_area(other, PAGE_SIZE) || other == rc)
        return fail("busy hint not relocated");
    long ro = sys_mmap(0, PAGE_SIZE, MMAP_PROT_READ, 0);
    if (!in_area(ro, PAGE_SIZE) || !all_bytes((unsigned char *)ro, PAGE_SIZE, 0))
        return fail("read-only map");

    long code = sys_mmap(0, PAGE_SIZE, PROT_RW | MMAP_PROT_EXEC, 0);
    if (!in_area(code, PAGE_SIZE))
        return fail("exec map");
    static const unsigned char ret_42[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
    memcpy((void *)code, ret_42, sizeof(ret_42));
    int (*fn)(void) = (int (*)(void))(uintptr_t)code;
    if (fn() != 42)
        return fail("exec mapping not executable");

    if (sys_munmap((void *)code, PAGE_SIZE) != 0 ||
        sys_munmap((void *)other, PAGE_SIZE) != 0 ||
        sys_munmap((void *)ro, PAGE_SIZE) != 0 ||
        sys_munmap(p, 3 * PAGE_SIZE) != 0)
        return fail("unmap basic cleanup");
    return 0;
}

static int page_intact(const unsigned char *page, unsigned char v) {
    return all_bytes(page, PAGE_SIZE, v);
}

static int check_partial(void) {
    long rc = map_rw(0, 4 * PAGE_SIZE);
    if (!in_area(rc, 4 * PAGE_SIZE))
        return fail("map partial");
    unsigned char *base = (unsigned char *)rc;
    for (unsigned long i = 0; i < 4; ++i)
        memset(base + i * PAGE_SIZE, (int)(0x10 + i), PAGE_SIZE);

    if (sys_munmap(base + PAGE_SIZE, 2 * PAGE_SIZE) != 0)
        return fail("unmap middle");
    if (!page_intact(base, 0x10) || !page_intact(base + 3 * PAGE_SIZE, 0x13))
        return fail("split damaged neighbours");

    rc = map_rw((uintptr_t)(base + PAGE_SIZE), 2 * PAGE_SIZE);
    if (rc != (long)(uintptr_t)(base + PAGE_SIZE))
        return fail("refill hole");
    if (!all_bytes(base + PAGE_SIZE, 2 * PAGE_SIZE, 0))
        return fail("refilled hole not zero");
    memset(base + PAGE_SIZE, 0x21, 2 * PAGE_SIZE);

    if (sys_munmap(base + 2 * PAGE_SIZE, PAGE_SIZE) != 0)
        return fail("unmap inner page");
    if (sys_munmap(base, PAGE_SIZE) != 0)
        return fail("unmap head page");
    if (!page_intact(base + PAGE_SIZE, 0x21) || !page_intact(base + 3 * PAGE_SIZE, 0x13))
        return fail("trim damaged remainder");
    if (sys_munmap(base, 4 * PAGE_SIZE) != 0)
        return fail("unmap sparse range");

    rc = map_rw((uintptr_t)base, 4 * PAGE_SIZE);
    if (rc != (long)(uintptr_t)base || !all_bytes(base, 4 * PAGE_SIZE, 0))
        return fail("range not fully released");
    if (sys_munmap(base, 4 * PAGE_SIZE) != 0)
        return fail("unmap partial cleanup");
    return 0;
}

static int check_many(void) {
    for (int i = 0; i < MANY_PAGES; ++i) {
        long rc = map_rw(0, PAGE_SIZE);
        if (!in_area(rc, PAGE_SIZE))
            return fail("many map");
        many_pages[i] = (uintptr_t)rc;
        uint32_t *words = (uint32_t *)many_pages[i];
        words[0] = 0xA5000000u | (uint32_t)i;
        words[PAGE_SIZE / 4 - 1] = ~(uint32_t)i;
    }
    for (int i = 0; i < MANY_PAGES; ++i) {
        const uint32_t *words = (const uint32_t *)many_pages[i];
        if (words[0] != (0xA5000000u | (uint32_t)i) ||
            words[PAGE_SIZE / 4 - 1] != ~(uint32_t)i)
            return fail("many readback");
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = pass; i < MANY_PAGES; i += 2) {
            if (sys_munmap((void *)many_pages[i], PAGE_SIZE) != 0)
                return fail("many unmap");
        }
    }
    return 0;
}

static int check_reuse(void) {
    for (int cycle = 0; cycle < REUSE_CYCLES; ++cycle) {
        long rc = map_rw(0, REUSE_BYTES);
        if (!in_area(rc, REUSE_BYTES))
            return fail("reuse map");
        unsigned char *p = (unsigned char *)rc;
        for (unsigned long off = 0; off < REUSE_BYTES; off += PAGE_SIZE) {
            if (p[off] != 0 || p[off + PAGE_SIZE - 1] != 0)
                return fail("reuse not zeroed");
            p[off] = (unsigned char)cycle;
            p[off + PAGE_SIZE - 1] = (unsigned char)~cycle;
        }
        for (unsigned long off = 0; off < REUSE_BYTES; off += PAGE_SIZE) {
            if (p[off] != (unsigned char)cycle ||
                p[off + PAGE_SIZE - 1] != (unsigned char)~cycle)
                return fail("reuse readback");
        }
        if (sys_munmap(p, REUSE_BYTES) != 0)
            return fail("reuse unmap");
    }
    return 0;
}

static unsigned char small_tag(int i, unsigned generation) {
    return (unsigned char)(i * 7 + (int)generation * 31 + 1);
}

static int small_verify(int i, unsigned generation, unsigned len) {
    return all_bytes(small_ptrs[i], len, small_tag(i, generation));
}

static int check_malloc_small(void) {
    if (malloc(0) != 0)
        return fail("malloc(0)");
    free(0);
    if (calloc((size_t)1 << 40, (size_t)1 << 40) != 0)
        return fail("calloc overflow");

    for (int i = 0; i < SMALL_COUNT; ++i) {
        small_sizes[i] = 1u + rng_next() % 700u;
        small_ptrs[i] = (unsigned char *)malloc(small_sizes[i]);
        if (!small_ptrs[i] || ((uintptr_t)small_ptrs[i] & 15u) != 0)
            return fail("malloc small");
        memset(small_ptrs[i], small_tag(i, 0), small_sizes[i]);
    }
    for (int i = 0; i < SMALL_COUNT; ++i) {
        if (!small_verify(i, 0, small_sizes[i]))
            return fail("malloc small overlap");
    }

    for (int i = 0; i < SMALL_COUNT; ++i) {
        if (i % 3 == 0) {
            free(small_ptrs[i]);
            small_ptrs[i] = 0;
            continue;
        }
        unsigned new_size = 1u + rng_next() % 1200u;
        unsigned char *q = (unsigned char *)realloc(small_ptrs[i], new_size);
        if (!q)
            return fail("realloc small");
        small_ptrs[i] = q;
        unsigned keep = new_size < small_sizes[i] ? new_size : small_sizes[i];
        if (!small_verify(i, 0, keep))
            return fail("realloc lost data");
        small_sizes[i] = new_size;
        memset(q, small_tag(i, 1), new_size);
    }

    unsigned char *zeroed = (unsigned char *)calloc(64, 33);
    if (!zeroed || !all_bytes(zeroed, 64 * 33, 0))
        return fail("calloc not zeroed");
    free(zeroed);

    for (int i = 0; i < SMALL_COUNT; i += 3) {
        small_sizes[i] = 1u + rng_next() % 300u;
        small_ptrs[i] = (unsigned char *)malloc(small_sizes[i]);
        if (!small_ptrs[i])
            return fail("malloc refill");
        memset(small_ptrs[i], small_tag(i, 1), small_sizes[i]);
    }
    for (int i = 0; i < SMALL_COUNT; ++i) {
        if (!small_verify(i, 1, small_sizes[i]))
            return fail("heap corrupted after churn");
    }
    for (int i = SMALL_COUNT - 1; i >= 0; --i) {
        free(small_ptrs[i]);
        small_ptrs[i] = 0;
    }
    return 0;
}

static int check_malloc_large(void) {
    unsigned char *p = 0;
    size_t len = 0;
    for (size_t want = 8; want <= 512u * 1024u; want *= 2) {
        unsigned char *q = (unsigned char *)realloc(p, want);
        if (!q)
            return fail("realloc grow");
        if (len && !all_bytes(q, len, (unsigned char)len))
            return fail("realloc grow lost data");
        memset(q, (int)(unsigned char)want, want);
        p = q;
        len = want;
    }
    p = (unsigned char *)realloc(p, 1000);
    if (!p || !all_bytes(p, 1000, (unsigned char)len))
        return fail("realloc shrink");
    free(p);

    for (int cycle = 0; cycle < REUSE_CYCLES; ++cycle) {
        size_t size = 3u * 1024u * 1024u + (size_t)cycle * 100u;
        unsigned char *big = (unsigned char *)malloc(size);
        if (!big)
            return fail("malloc large");
        for (size_t off = 0; off < size; off += PAGE_SIZE)
            big[off] = (unsigned char)cycle;
        big[size - 1] = (unsigned char)~cycle;
        for (size_t off = 0; off < size; off += PAGE_SIZE) {
            if (big[off] != (unsigned char)cycle)
                return fail("malloc large readback");
        }
        free(big);
    }

    unsigned char *zeroed = (unsigned char *)calloc(1, 256u * 1024u);
    if (!zeroed || !all_bytes(zeroed, 256u * 1024u, 0))
        return fail("calloc large");
    free(zeroed);
    return 0;
}

int main(void) {
    if (check_errors() || check_basic() || check_partial() || check_many() ||
        check_reuse() || check_malloc_small() || check_malloc_large())
        return 1;
    write(1, "MMAP PASS\n", 10);
    return 0;
}
