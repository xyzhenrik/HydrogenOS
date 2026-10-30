#include "meminfo.h"
#include "mini_libc.h"

/*
 * free - physical memory, kernel heap and out-of-memory kills, in KiB.
 */
enum {
    KIB = 1024,
};

int main(void) {
    hydrogen_meminfo_t info;
    if (meminfo_read(&info) != 0) {
        dprintf(2, "free: memory information unavailable\n");
        return 1;
    }
    unsigned long page_kib = (unsigned long)(info.page_size / KIB);
    unsigned long total = (unsigned long)info.total_pages * page_kib;
    unsigned long unused = (unsigned long)info.free_pages * page_kib;
    unsigned long heap_total = (unsigned long)(info.kheap_total_bytes / KIB);
    unsigned long heap_used = (unsigned long)(info.kheap_used_bytes / KIB);
    printf("%-13s %10s %10s %10s\n", "", "total", "used", "free");
    printf("%-13s %10lu %10lu %10lu\n", "Mem:", total, total - unused, unused);
    printf("%-13s %10lu %10lu %10lu\n", "Kernel heap:", heap_total, heap_used,
           heap_total - heap_used);
    printf("OOM kills: %lu\n", (unsigned long)info.oom_kills);
    return 0;
}
