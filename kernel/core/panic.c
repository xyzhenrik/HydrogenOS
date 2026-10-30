#include "panic.h"

#include "console.h"
#include "core/crash.h"
#include "io.h"
#include <stdint.h>

void panic(const char *msg, const char *file, int line) {
    __asm__ volatile("cli");
    console_write("KERNEL PANIC: ");
    if (msg) {
        console_write(msg);
    }
    console_write("\nFILE: ");
    console_write(file);
    console_write("\nLINE: ");
    console_write_hex((uint32_t)line);
    console_write("\n");

    uint64_t regs[16];
    __asm__ volatile (
        "mov %%rax, %0\n\t"
        "mov %%rbx, %1\n\t"
        "mov %%rcx, %2\n\t"
        "mov %%rdx, %3\n\t"
        "mov %%rsi, %4\n\t"
        "mov %%rdi, %5\n\t"
        "mov %%rbp, %6\n\t"
        "mov %%rsp, %7\n\t"
        "mov %%r8, %8\n\t"
        "mov %%r9, %9\n\t"
        "mov %%r10, %10\n\t"
        "mov %%r11, %11\n\t"
        "mov %%r12, %12\n\t"
        "mov %%r13, %13\n\t"
        "mov %%r14, %14\n\t"
        "mov %%r15, %15"
        : "=m"(regs[0]), "=m"(regs[1]), "=m"(regs[2]), "=m"(regs[3]), 
          "=m"(regs[4]), "=m"(regs[5]), "=m"(regs[6]), "=m"(regs[7]), 
          "=m"(regs[8]), "=m"(regs[9]), "=m"(regs[10]), "=m"(regs[11]), 
          "=m"(regs[12]), "=m"(regs[13]), "=m"(regs[14]), "=m"(regs[15]));
    
    uint64_t rip;
    __asm__ volatile ("lea (%%rip), %0" : "=r"(rip));

    console_write("RAX: "); console_write_hex64(regs[0]);  console_write(" RBX: "); console_write_hex64(regs[1]);  console_write("\n");
    console_write("RCX: "); console_write_hex64(regs[2]);  console_write(" RDX: "); console_write_hex64(regs[3]);  console_write("\n");
    console_write("RSI: "); console_write_hex64(regs[4]);  console_write(" RDI: "); console_write_hex64(regs[5]);  console_write("\n");
    console_write("RBP: "); console_write_hex64(regs[6]);  console_write(" RSP: "); console_write_hex64(regs[7]);  console_write("\n");
    console_write("R8:  "); console_write_hex64(regs[8]);  console_write(" R9:  "); console_write_hex64(regs[9]);  console_write("\n");
    console_write("R10: "); console_write_hex64(regs[10]); console_write(" R11: "); console_write_hex64(regs[11]); console_write("\n");
    console_write("R12: "); console_write_hex64(regs[12]); console_write(" R13: "); console_write_hex64(regs[13]); console_write("\n");
    console_write("R14: "); console_write_hex64(regs[14]); console_write(" R15: "); console_write_hex64(regs[15]); console_write("\n");
    console_write("RIP: "); console_write_hex64(rip); console_write("\n");

    crash_record(CRASH_KIND_PANIC, msg, 0);

    console_write("Halte an.\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
