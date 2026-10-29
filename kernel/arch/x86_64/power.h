#pragma once

/*
 * System power transitions. Both flush the block cache first and refuse the
 * transition with -EIO if dirty data could not be written back.
 */

/* Enters ACPI S5. Returns a negative errno only if the machine stays on:
 * -ENOSYS without a usable S5 description, -EIO otherwise. */
int power_shutdown(void);

/* Resets the machine via the ACPI reset register, then the 8042 reset line,
 * then a triple fault. Returns only on flush failure (-EIO). */
int power_reboot(void);
