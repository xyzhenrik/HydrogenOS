#include "apic.h"

#include "io.h"
#include "msr.h"
#include "paging.h"

#include <stddef.h>
#include <stdint.h>

#define IA32_APIC_BASE_MSR 0x1BU

#define LAPIC_ID_REG 0x0020U
#define LAPIC_TPR_REG 0x0080U
#define LAPIC_EOI_REG 0x00B0U
#define LAPIC_SVR_REG 0x00F0U
#define LAPIC_LVT_TIMER_REG 0x0320U
#define LAPIC_LVT_LINT0_REG 0x0350U
#define LAPIC_LVT_LINT1_REG 0x0360U
#define LAPIC_LVT_ERROR_REG 0x0370U

#define IOAPIC_REGSEL_OFFSET 0x00U
#define IOAPIC_IOWIN_OFFSET 0x10U
#define IOAPIC_ID_REG 0x00U
#define IOAPIC_REDTBL_BASE 0x10U

static volatile uint32_t *lapic_base = (volatile uint32_t *)LAPIC_DEFAULT_BASE;
static volatile uint32_t *ioapic_base = (volatile uint32_t *)IOAPIC_DEFAULT_BASE;

static void lapic_map(void) {
    uint64_t base = rdmsr(IA32_APIC_BASE_MSR) & 0xFFFFF000ULL;
    if (base == 0ULL) {
        base = LAPIC_DEFAULT_BASE;
    }
    paging_map_region((uintptr_t)base, (uintptr_t)base, 4096U, PAGE_RW | PAGE_PRESENT);
    lapic_base = (volatile uint32_t *)(uintptr_t)base;
    wrmsr(IA32_APIC_BASE_MSR, base | (1ULL << 11));
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
    lapic_base[reg / 4U] = value;
}

static inline uint32_t lapic_read(uint32_t reg) {
    return lapic_base[reg / 4U];
}

void lapic_init(void) {
    lapic_map();

    lapic_write(LAPIC_TPR_REG, 0U);
    /* Enable the LAPIC with spurious vector 0xFF. */
    lapic_write(LAPIC_SVR_REG, 0x100U | 0xFFU);

    /* Virtual-wire mode: LINT0 must deliver ExtINT so legacy 8259 PIC
       interrupts (PIT/keyboard) still reach the CPU. Masking LINT0 here
       silently disables the PIT. LINT1 stays as NMI. */
    lapic_write(LAPIC_LVT_TIMER_REG, 0x00010000U);
    lapic_write(LAPIC_LVT_LINT0_REG, 0x00000700U);
    lapic_write(LAPIC_LVT_LINT1_REG, 0x00000400U);
    lapic_write(LAPIC_LVT_ERROR_REG, 0x00010000U);
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI_REG, 0U);
}

uint32_t lapic_id(void) {
    return lapic_read(LAPIC_ID_REG) >> 24;
}

static void ioapic_map(uintptr_t base) {
    if (base == 0U) {
        base = IOAPIC_DEFAULT_BASE;
    }
    paging_map_region(base, base, 4096U, PAGE_RW | PAGE_PRESENT);
    ioapic_base = (volatile uint32_t *)(uintptr_t)base;
}

void ioapic_init(void) {
    ioapic_map(IOAPIC_DEFAULT_BASE);
}

void ioapic_init_at(uintptr_t base) {
    ioapic_map(base);
}

static void ioapic_write_reg(uint8_t reg, uint32_t value) {
    ioapic_base[IOAPIC_REGSEL_OFFSET / 4U] = reg;
    ioapic_base[IOAPIC_IOWIN_OFFSET / 4U] = value;
}

static uint32_t ioapic_read_reg(uint8_t reg) {
    ioapic_base[IOAPIC_REGSEL_OFFSET / 4U] = reg;
    return ioapic_base[IOAPIC_IOWIN_OFFSET / 4U];
}

uint32_t ioapic_id(void) {
    return (ioapic_read_reg(IOAPIC_ID_REG) >> 24) & 0x0FU;
}

void ioapic_set_entry(uint8_t irq, uint64_t entry) {
    uint8_t reg = (uint8_t)(IOAPIC_REDTBL_BASE + (irq * 2U));
    ioapic_write_reg(reg, (uint32_t)(entry & 0xFFFFFFFFULL));
    ioapic_write_reg((uint8_t)(reg + 1U), (uint32_t)(entry >> 32));
}

void ioapic_enable_irq(uint8_t irq, uint8_t vector, uint8_t destination_apic_id) {
    uint64_t entry = vector;
    entry |= ((uint64_t)destination_apic_id << 56);
    ioapic_set_entry(irq, entry);
}

void ioapic_mask_irq(uint8_t irq) {
    uint8_t reg = (uint8_t)(IOAPIC_REDTBL_BASE + (irq * 2U));
    uint32_t low = ioapic_read_reg(reg);
    ioapic_write_reg(reg, low | (1U << 16));
}

void ioapic_unmask_irq(uint8_t irq) {
    uint8_t reg = (uint8_t)(IOAPIC_REDTBL_BASE + (irq * 2U));
    uint32_t low = ioapic_read_reg(reg);
    ioapic_write_reg(reg, low & ~(1U << 16));
}