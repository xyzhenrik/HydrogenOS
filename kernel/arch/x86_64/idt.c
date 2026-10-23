#include "idt.h"
#include "isr.h"

#include <stddef.h>
#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel,
                         uint8_t flags) {
    idt[num].offset_low = (uint16_t)(base & 0xFFFF);
    idt[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].selector = sel;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].zero = 0;
}

void idt_install(void) {
    idtp.limit = (uint16_t)(sizeof(struct idt_entry) * 256 - 1);
    idtp.base = (uint64_t)(uintptr_t)&idt;

    for (size_t i = 0; i < 256; ++i) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
    }

    extern void *isr_stub_table[];
    extern void isr128(void);
    for (uint8_t i = 0; i < 49; ++i) {
        idt_set_gate(i, (uint64_t)(uintptr_t)isr_stub_table[i], 0x08, 0x8E);
    }
    idt_set_gate(0x80, (uint64_t)(uintptr_t)isr128, 0x08, 0xEE);

    __asm__ volatile("lidt (%0)" ::"r"(&idtp));
}

void isr_install(void) {
    // gates are installed in idt_install()
}
