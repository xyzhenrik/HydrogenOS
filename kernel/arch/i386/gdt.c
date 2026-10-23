#include "gdt.h"

#include <stdint.h>

extern void gdt_flush(uint32_t);

struct gdt_entry {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t granularity;
  uint8_t base_hi;
} __attribute__((packed));

struct gdt_ptr {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr gdtp;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access,
                         uint8_t gran) {
  gdt[num].base_low = base & 0xFFFF;
  gdt[num].base_mid = (base >> 16) & 0xFF;
  gdt[num].base_hi = (base >> 24) & 0xFF;

  gdt[num].limit_low = limit & 0xFFFF;
  gdt[num].granularity = (limit >> 16) & 0x0F;
  gdt[num].granularity |= gran & 0xF0;

  gdt[num].access = access;
}

void gdt_init(void) {
  gdtp.limit = (sizeof(struct gdt_entry) * 3) - 1;
  gdtp.base = (uint32_t)&gdt;

  gdt_set_gate(0, 0, 0, 0, 0);                // Null
  gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Code
  gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data

  gdt_flush((uint32_t)&gdtp);
}
