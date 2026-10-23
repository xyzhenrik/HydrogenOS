#include "task.h"
#include "console.h"
#include "kheap.h"
#include "isr.h"
#include <stddef.h>

static task_t *current = NULL;
static uint32_t next_id = 1;
static int multitasking_enabled = 0;

static task_t *alloc_task(void) {
  task_t *t = (task_t *)kalloc(sizeof(task_t));
  if (!t)
    return NULL;
  t->frame = NULL;
  t->stack_base = 0;
  t->next = NULL;
  t->id = next_id++;
  return t;
}

void tasking_init(void) {
  current = alloc_task();
  if (!current) {
    console_write("tasking_init: no mem\n");
    return;
  }
  // current task is the one running; esp/ebp/eip filled on first switch
  current->next = current; // circular list
}

void task_create(void (*fn)(void)) {
  task_t *t = alloc_task();
  if (!t)
    return;
  // allocate stack
  uint8_t *stack = (uint8_t *)kalloc(8192);
  if (!stack)
    return;
  uint32_t stack_top = (uint32_t)(stack + 8192);
  t->stack_base = (uint32_t)stack;

  regs_t *frame = (regs_t *)(stack_top - sizeof(regs_t));
  frame->gs = 0x10;
  frame->fs = 0x10;
  frame->es = 0x10;
  frame->ds = 0x10;
  frame->edi = 0;
  frame->esi = 0;
  frame->ebp = 0;
  frame->esp = 0;
  frame->ebx = 0;
  frame->edx = 0;
  frame->ecx = 0;
  frame->eax = 0;
  frame->int_no = 0;
  frame->err_code = 0;
  frame->eip = (uint32_t)fn;
  frame->cs = 0x08;
  frame->eflags = 0x202;
  frame->useresp = 0;
  frame->ss = 0x10;
  t->frame = frame;

  // insert into round-robin list after current
  t->next = current->next;
  current->next = t;
  multitasking_enabled = 1;
}

regs_t *task_schedule(regs_t *r) {
  if (!multitasking_enabled)
    return r;
  if (!current || !current->next)
    return r;
  if (current->next == current)
    return r; // single task

  current->frame = r;
  task_t *next = current->next;
  if (!next || !next->frame)
    return r;
  current = next;
  return current->frame;
}

void task_switch(void) { task_yield(); }

void task_yield(void) { __asm__ volatile("int $0x30"); }

int task_is_enabled(void) { return multitasking_enabled; }

void task_set_enabled(int on) { multitasking_enabled = on ? 1 : 0; }
