#include "gdt.h"

#include <stddef.h>
#include <stdint.h>

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct tss64 tss;
static uint64_t gdt[7];
static struct gdt_ptr gdtr;

extern uint8_t stack_top64;

static uint64_t gdt_make_descriptor(uint32_t base, uint32_t limit, uint8_t access,
                                    uint8_t flags) {
    uint64_t desc = 0;
    desc |= (uint64_t)(limit & 0xFFFF);
    desc |= (uint64_t)(base & 0xFFFFFF) << 16;
    desc |= (uint64_t)access << 40;
    desc |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    desc |= (uint64_t)(flags & 0x0F) << 52;
    desc |= (uint64_t)((base >> 24) & 0xFF) << 56;
    return desc;
}

static void gdt_set_tss(uint64_t base, uint32_t limit) {
    uint64_t low = 0;
    uint64_t high = 0;
    low |= (uint64_t)(limit & 0xFFFF);
    low |= (uint64_t)(base & 0xFFFFFF) << 16;
    low |= (uint64_t)0x89 << 40; // present, type 9 (64-bit available TSS)
    low |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    low |= (uint64_t)((base >> 24) & 0xFF) << 56;
    high = (base >> 32) & 0xFFFFFFFF;
    gdt[5] = low;
    gdt[6] = high;
}

void gdt_init(void) {
    for (size_t i = 0; i < sizeof(gdt) / sizeof(gdt[0]); ++i) {
        gdt[i] = 0;
    }

    tss.reserved0 = 0;
    tss.rsp0 = (uint64_t)(uintptr_t)&stack_top64;
    tss.rsp1 = 0;
    tss.rsp2 = 0;
    tss.reserved1 = 0;
    tss.ist1 = 0;
    tss.ist2 = 0;
    tss.ist3 = 0;
    tss.ist4 = 0;
    tss.ist5 = 0;
    tss.ist6 = 0;
    tss.ist7 = 0;
    tss.reserved2 = 0;
    tss.reserved3 = 0;
    tss.iopb = (uint16_t)sizeof(struct tss64);

    gdt[1] = gdt_make_descriptor(0, 0xFFFFF, 0x9A, 0x0A); // kernel code
    gdt[2] = gdt_make_descriptor(0, 0xFFFFF, 0x92, 0x0C); // kernel data
    gdt[3] = gdt_make_descriptor(0, 0xFFFFF, 0xF2, 0x0C); // user data
    gdt[4] = gdt_make_descriptor(0, 0xFFFFF, 0xFA, 0x0A); // user code
    gdt_set_tss((uint64_t)(uintptr_t)&tss, (uint32_t)sizeof(struct tss64) - 1);

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base = (uint64_t)(uintptr_t)&gdt;

    __asm__ volatile("lgdt %0" : : "m"(gdtr));
    __asm__ volatile("mov $0x10, %%ax\n"
                     "mov %%ax, %%ds\n"
                     "mov %%ax, %%es\n"
                     "mov %%ax, %%ss\n"
                     "mov %%ax, %%fs\n"
                     "mov %%ax, %%gs\n"
                     :
                     :
                     : "rax");
    __asm__ volatile("ltr %w0" : : "r"(0x28));
}
