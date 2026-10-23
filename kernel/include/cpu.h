#pragma once
#include <stdint.h>
#include "../core/process.h"

typedef struct cpu {
    uint32_t id;
    process_t *current_proc;
    uintptr_t kernel_stack;
} cpu_t;

void cpu_init(uint32_t id);
cpu_t *cpu_current(void);
