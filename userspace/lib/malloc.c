#include "mini_libc.h"
#include "syscall.h"

#include <stddef.h>
#include <stdint.h>

/*
 * General-purpose allocator on top of sys_mmap.
 *
 * Small requests are carved out of 256 KiB arenas using boundary-tagged
 * blocks: every block header stores its own size and the size of the
 * physically preceding block, so free() coalesces with both neighbours in
 * O(1). Free blocks sit in power-of-two size bins. Invariant: no two free
 * blocks are ever adjacent. Arenas that become completely free are returned
 * to the kernel, except for one kept idle to avoid map/unmap churn.
 * Requests of MALLOC_LARGE_MIN bytes and more get a private mapping that
 * free() returns to the kernel immediately. Each is followed by a PROT_NONE
 * guard page so a linear overrun faults instead of corrupting a neighbour.
 */

#define MALLOC_ALIGN      16u
#define MALLOC_PAGE_SIZE  4096u
#define MALLOC_ARENA_SIZE (256u * 1024u)
#define MALLOC_LARGE_MIN  (128u * 1024u)
#define MALLOC_BIN_COUNT  14
#define MALLOC_MIN_SHIFT  5

#define BLOCK_USED   ((size_t)1)
#define BLOCK_MAPPED ((size_t)2)
#define BLOCK_FLAGS  ((size_t)(MALLOC_ALIGN - 1u))

typedef struct BlockHeader {
    size_t prev_size;  /* 0 for the first block of an arena */
    size_t size_flags; /* total block size including this header | flags */
} BlockHeader;

/* Lives in the payload of free blocks only. */
typedef struct FreeLinks {
    BlockHeader *next;
    BlockHeader *prev;
} FreeLinks;

typedef struct Arena {
    size_t size;
    size_t reserved; /* keeps the first block 16-byte aligned */
} Arena;

#define HEADER_SIZE     (sizeof(BlockHeader))
#define MIN_BLOCK_SIZE  (HEADER_SIZE + sizeof(FreeLinks))
#define ARENA_FIRST_OFF (sizeof(Arena))

_Static_assert(sizeof(BlockHeader) % MALLOC_ALIGN == 0, "header alignment");
_Static_assert(sizeof(Arena) % MALLOC_ALIGN == 0, "arena header alignment");
_Static_assert(MIN_BLOCK_SIZE == (1u << MALLOC_MIN_SHIFT), "bin base");

static BlockHeader *bins[MALLOC_BIN_COUNT];
static unsigned idle_arenas;

static __attribute__((noreturn)) void heap_abort(const char *msg) {
    (void)write(2, "malloc: ", 8);
    (void)write(2, msg, strlen(msg));
    (void)write(2, "\n", 1);
    sys_exit(134);
    __builtin_unreachable();
}

static size_t block_size(const BlockHeader *b) { return b->size_flags & ~BLOCK_FLAGS; }
static int block_used(const BlockHeader *b) { return (b->size_flags & BLOCK_USED) != 0; }
static FreeLinks *block_links(BlockHeader *b) { return (FreeLinks *)(b + 1); }
static void *block_payload(BlockHeader *b) { return (void *)(b + 1); }

static BlockHeader *block_next(BlockHeader *b) {
    return (BlockHeader *)((uint8_t *)b + block_size(b));
}

static BlockHeader *block_prev(BlockHeader *b) {
    return (BlockHeader *)((uint8_t *)b - b->prev_size);
}

static void block_set(BlockHeader *b, size_t size, size_t flags) {
    b->size_flags = size | flags;
    block_next(b)->prev_size = size;
}

static int bin_index(size_t size) {
    int idx = -MALLOC_MIN_SHIFT;
    while (size > 1) {
        size >>= 1;
        ++idx;
    }
    return idx < MALLOC_BIN_COUNT ? idx : MALLOC_BIN_COUNT - 1;
}

static void bin_insert(BlockHeader *b) {
    int idx = bin_index(block_size(b));
    FreeLinks *links = block_links(b);
    links->prev = 0;
    links->next = bins[idx];
    if (bins[idx])
        block_links(bins[idx])->prev = b;
    bins[idx] = b;
}

static void bin_remove(BlockHeader *b) {
    FreeLinks *links = block_links(b);
    if (links->prev)
        block_links(links->prev)->next = links->next;
    else
        bins[bin_index(block_size(b))] = links->next;
    if (links->next)
        block_links(links->next)->prev = links->prev;
}

static void *map_pages(size_t length) {
    long rc = sys_mmap(0, length, MMAP_PROT_READ | MMAP_PROT_WRITE, 0);
    return rc < 0 ? 0 : (void *)rc;
}

/* Only for requests below MALLOC_LARGE_MIN, so the rounding cannot overflow. */
static size_t request_block_size(size_t request) {
    size_t size = (request + HEADER_SIZE + MALLOC_ALIGN - 1u) & ~(size_t)(MALLOC_ALIGN - 1u);
    return size < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE : size;
}

static int block_spans_arena(BlockHeader *b) {
    return b->prev_size == 0 && block_size(block_next(b)) == 0;
}

static BlockHeader *arena_create(void) {
    Arena *arena = (Arena *)map_pages(MALLOC_ARENA_SIZE);
    if (!arena)
        return 0;
    arena->size = MALLOC_ARENA_SIZE;

    BlockHeader *first = (BlockHeader *)((uint8_t *)arena + ARENA_FIRST_OFF);
    size_t first_size = MALLOC_ARENA_SIZE - ARENA_FIRST_OFF - HEADER_SIZE;
    first->prev_size = 0;
    /* The trailing sentinel is permanently "used" and stops coalescing. */
    BlockHeader *sentinel = (BlockHeader *)((uint8_t *)first + first_size);
    sentinel->size_flags = BLOCK_USED;
    block_set(first, first_size, 0);
    return first;
}

static BlockHeader *bin_find(size_t size) {
    for (int idx = bin_index(size); idx < MALLOC_BIN_COUNT; ++idx) {
        for (BlockHeader *b = bins[idx]; b; b = block_links(b)->next) {
            if (block_size(b) >= size)
                return b;
        }
    }
    return 0;
}

/* Marks a block free, merges it with free neighbours and bins it. */
static void block_release(BlockHeader *b) {
    size_t size = block_size(b);
    BlockHeader *next = block_next(b);
    if (!block_used(next)) {
        bin_remove(next);
        size += block_size(next);
    }
    if (b->prev_size != 0) {
        BlockHeader *prev = block_prev(b);
        if (!block_used(prev)) {
            bin_remove(prev);
            size += block_size(prev);
            b = prev;
        }
    }
    block_set(b, size, 0);

    if (block_spans_arena(b)) {
        if (idle_arenas > 0) {
            Arena *arena = (Arena *)((uint8_t *)b - ARENA_FIRST_OFF);
            (void)sys_munmap(arena, arena->size);
            return;
        }
        idle_arenas++;
    }
    bin_insert(b);
}

/* Shrinks a used block to `size`, releasing the tail if it can stand alone. */
static void block_trim(BlockHeader *b, size_t size) {
    size_t total = block_size(b);
    if (total - size < MIN_BLOCK_SIZE)
        return;
    block_set(b, size, BLOCK_USED);
    BlockHeader *tail = block_next(b);
    block_set(tail, total - size, BLOCK_USED);
    block_release(tail);
}

static void *large_alloc(size_t request) {
    if (request > (size_t)-1 - HEADER_SIZE - 2u * MALLOC_PAGE_SIZE)
        return 0;
    size_t length = (request + HEADER_SIZE + MALLOC_PAGE_SIZE - 1u) & ~(size_t)(MALLOC_PAGE_SIZE - 1u);
    BlockHeader *b = (BlockHeader *)map_pages(length + MALLOC_PAGE_SIZE);
    if (!b)
        return 0;
    /* The guard splits the mapping, which needs one more kernel region
     * descriptor; when that is exhausted the request fails like any other
     * out-of-memory case rather than handing out an unguarded mapping. */
    if (sys_mprotect((uint8_t *)b + length, MALLOC_PAGE_SIZE, MMAP_PROT_NONE, 0) != 0) {
        (void)sys_munmap(b, length + MALLOC_PAGE_SIZE);
        return 0;
    }
    b->prev_size = 0;
    b->size_flags = length | BLOCK_USED | BLOCK_MAPPED;
    return block_payload(b);
}

static BlockHeader *header_of(void *ptr) {
    if (((uintptr_t)ptr & (MALLOC_ALIGN - 1u)) != 0)
        heap_abort("misaligned pointer");
    BlockHeader *b = (BlockHeader *)ptr - 1;
    if (!block_used(b) || block_size(b) < MIN_BLOCK_SIZE)
        heap_abort("invalid pointer or double free");
    if ((b->size_flags & BLOCK_MAPPED) && (block_size(b) & (MALLOC_PAGE_SIZE - 1u)))
        heap_abort("corrupted mapping header");
    return b;
}

void *malloc(size_t size) {
    if (size == 0)
        return 0;
    if (size >= MALLOC_LARGE_MIN)
        return large_alloc(size);

    size_t need = request_block_size(size);
    BlockHeader *b = bin_find(need);
    if (b) {
        bin_remove(b);
        if (block_spans_arena(b))
            idle_arenas--;
    } else {
        b = arena_create();
        if (!b)
            return 0;
    }
    block_set(b, block_size(b), BLOCK_USED);
    block_trim(b, need);
    return block_payload(b);
}

void free(void *ptr) {
    if (!ptr)
        return;
    BlockHeader *b = header_of(ptr);
    if (b->size_flags & BLOCK_MAPPED) {
        (void)sys_munmap(b, block_size(b) + MALLOC_PAGE_SIZE);
        return;
    }
    block_release(b);
}

void *calloc(size_t count, size_t size) {
    if (count != 0 && size > (size_t)-1 / count)
        return 0;
    size_t total = count * size;
    void *ptr = malloc(total);
    if (ptr && !(header_of(ptr)->size_flags & BLOCK_MAPPED))
        memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }
    BlockHeader *b = header_of(ptr);
    size_t usable = block_size(b) - HEADER_SIZE;

    if (!(b->size_flags & BLOCK_MAPPED) && size < MALLOC_LARGE_MIN) {
        size_t need = request_block_size(size);
        if (need <= block_size(b)) {
            block_trim(b, need);
            return ptr;
        }
        BlockHeader *next = block_next(b);
        if (!block_used(next) && block_size(b) + block_size(next) >= need) {
            bin_remove(next);
            block_set(b, block_size(b) + block_size(next), BLOCK_USED);
            block_trim(b, need);
            return ptr;
        }
    } else if ((b->size_flags & BLOCK_MAPPED) && size <= usable) {
        return ptr;
    }

    void *fresh = malloc(size);
    if (!fresh)
        return 0;
    memcpy(fresh, ptr, usable < size ? usable : size);
    free(ptr);
    return fresh;
}
