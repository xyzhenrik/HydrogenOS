#pragma once

#include <stdint.h>

struct regs;
typedef struct regs regs_t;

typedef struct task {
    regs_t *frame;
    uint32_t stack_base;
    struct task *next;
    uint32_t id;
} task_t;

void tasking_init(void);
void task_switch(void);
void task_create(void (*fn)(void));
void task_yield(void);
regs_t *task_schedule(regs_t *r);
int task_is_enabled(void);
void task_set_enabled(int on);
