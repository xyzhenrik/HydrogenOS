#pragma once

/*
 * Minimal ACPI support: table discovery (RSDP/RSDT/XSDT/FADT), the \_S5
 * sleep type from the DSDT/SSDTs and the FADT reset register. There is no
 * AML interpreter; only the static encodings firmware uses for \_S5 are
 * understood.
 *
 * acpi_init() must run once at boot while the kernel page tables are active
 * and before the first user address space is created, so that the mappings
 * it installs in the kernel half are shared by every process.
 */

/* Parses the firmware tables and logs a summary. Missing or malformed tables
 * are not fatal: the affected power transitions simply report unavailable. */
void acpi_init(void);

int acpi_s5_available(void);

/*
 * Enters S5 (soft off). Must be called with interrupts disabled. Returns only
 * if S5 is unavailable or the platform did not power off.
 */
void acpi_enter_s5(void);

/*
 * Writes the FADT reset register. Must be called with interrupts disabled.
 * Returns only if no reset register is available or the reset did not take
 * effect.
 */
void acpi_reset(void);
