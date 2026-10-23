#include "console.h"
#include "time.h"
#include "task.h"

static volatile uint32_t demo_count_a = 0;
static volatile uint32_t demo_count_b = 0;

static void task_a(void) {
    for (;;) {
        demo_count_a++;
        for (volatile int i = 0; i < 20000; ++i) { __asm__ volatile("nop"); }
        task_yield();
    }
}

static void task_b(void) {
    for (;;) {
        demo_count_b++;
        for (volatile int i = 0; i < 20000; ++i) { __asm__ volatile("nop"); }
        task_yield();
    }
}

void demo_tasks_start(void) {
    task_create(task_a);
    task_create(task_b);
}

void demo_tasks_snapshot(uint32_t *a, uint32_t *b) {
    if (a) *a = demo_count_a;
    if (b) *b = demo_count_b;
}
