#include "syscall_arch.h"

#include <stdint.h>

#include "msr.h"

#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084

extern void syscall_entry(void);

void syscall_arch_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 0x1; // SCE
    wrmsr(MSR_EFER, efer);

    // STAR[47:32] = kernel CS (0x08), STAR[63:48] = user CS - 0x10 (0x13)
    uint64_t star = ((uint64_t)0x13 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)&syscall_entry);
    wrmsr(MSR_FMASK, 0x200); // clear IF on syscall entry
}
