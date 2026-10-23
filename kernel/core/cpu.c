#include "cpu.h"
#include <stdint.h>

static cpu_t cpus[1]; /* Single CPU for now */

void cpu_init(uint32_t id) {
    cpus[id].id = id;
    cpus[id].current_proc = 0;
    
    /* Set GS base to point to this cpu_t structure */
    uint64_t base = (uint64_t)(uintptr_t)&cpus[id];
    __asm__ volatile (
        "mov $0xC0000101, %%ecx\n\t" /* GS_BASE MSR */
        "mov %0, %%rax\n\t"
        "mov %%rax, %%rdx\n\t"
        "shr $32, %%rdx\n\t"
        "wrmsr"
        : : "r"(base) : "eax", "edx", "ecx", "memory");
}

cpu_t *cpu_current(void) {
    cpu_t *ret;
    __asm__ volatile (
        "mov %%gs:0, %0"
        : "=r"(ret)
    );
    return ret;
}
