#pragma once

#include <stddef.h>
#include <stdint.h>

#define LAPIC_DEFAULT_BASE 0xFEE00000ULL
#define IOAPIC_DEFAULT_BASE 0xFEC00000ULL

void lapic_init(void);
void lapic_eoi(void);
uint32_t lapic_id(void);

void ioapic_init(void);
void ioapic_init_at(uintptr_t base);
uint32_t ioapic_id(void);
void ioapic_set_entry(uint8_t irq, uint64_t entry);
void ioapic_enable_irq(uint8_t irq, uint8_t vector, uint8_t destination_apic_id);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);