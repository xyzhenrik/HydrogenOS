#include "meminfo.h"

#include "core/usercopy.h"
#include "errno.h"
#include "kheap.h"
#include "meminfo_abi.h"
#include "oom.h"
#include "pmm.h"

long meminfo_read_user(void *buffer, size_t size) {
    if (size < 2 * sizeof(uint32_t))
        return -EINVAL;
    if (size > sizeof(hydrogen_meminfo_t))
        size = sizeof(hydrogen_meminfo_t);

    kheap_stats_t heap;
    kheap_get_stats(&heap);

    hydrogen_meminfo_t info = {
        .version = HYDROGEN_MEMINFO_VERSION,
        .size = sizeof(info),
        .page_size = 4096,
        .total_pages = pmm_total_page_count(),
        .free_pages = pmm_free_page_count(),
        .kheap_total_bytes = heap.total_bytes,
        .kheap_used_bytes = heap.used_bytes,
        .oom_kills = oom_kill_count(),
    };
    int rc = copy_to_user(buffer, &info, size);
    return rc != 0 ? rc : (long)size;
}
