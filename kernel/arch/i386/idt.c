#include "idt.h"
#include "isr.h"

#include <stddef.h>
#include <stdint.h>

struct idt_entry {
  uint16_t base_lo;
  uint16_t sel;
  uint8_t always0;
  uint8_t flags;
  uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed));

extern void isr_default(void);

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel,
                         uint8_t flags) {
  idt[num].base_lo = base & 0xFFFF;
  idt[num].base_hi = (base >> 16) & 0xFFFF;
  idt[num].sel = sel;
  idt[num].always0 = 0;
  idt[num].flags = flags;
}

void idt_install(void) {
  idtp.limit = sizeof(struct idt_entry) * 256 - 1;
  idtp.base = (uint32_t)&idt;

  for (size_t i = 0; i < 256; ++i) {
    idt_set_gate((uint8_t)i, 0, 0, 0);
  }

  extern void *isr_stub_table[];
  extern void isr128(void);
  uint16_t cs;
  __asm__ volatile("mov %%cs, %0" : "=r"(cs));
  for (uint8_t i = 0; i < 49; ++i) {
    idt_set_gate(i, (uint32_t)isr_stub_table[i], cs, 0x8E);
  }
  idt_set_gate(0x80, (uint32_t)isr128, cs, 0xEE);

  __asm__ volatile("lidt (%0)" ::"r"(&idtp));
}

void isr_install(void) {
  // keine zusätzliche Arbeit nötig; gates in idt_install gesetzt
}
