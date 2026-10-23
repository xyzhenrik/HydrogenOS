#pragma once

#include <stdint.h>

typedef struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} regs_t;

void isr_install(void);
regs_t *isr_handler(regs_t *r);
