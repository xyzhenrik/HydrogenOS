#pragma once

#include <stddef.h>
#include <stdint.h>

#include "isr.h"

struct process;

/*
 * Crash dumps.
 *
 * On a kernel panic, a simulated oops or a fatal fault of a user process the
 * kernel renders a text report (registers, backtrace, process table, the last
 * console lines) into a bounded buffer and persists it in a reserved raw
 * region at the end of the boot disk, outside the filesystem. It survives a
 * reboot there; the next boot's init converts it into /var/crash/crash-N.txt
 * (docs/CRASH_OOM.md). Without a usable disk the dump stays in memory only.
 */
typedef enum crash_kind {
    CRASH_KIND_PANIC,
    CRASH_KIND_OOPS,
    CRASH_KIND_USER_FAULT,
} crash_kind_t;

/* Locates the reserved region on the boot disk and picks up a dump a previous
 * boot left behind. Needs block devices and the root filesystem. */
void crash_init(void);

/* Remembers the frame of the CPU exception that is about to end in a panic,
 * so that the dump shows the faulting context instead of the panic call. */
void crash_note_trap_frame(const regs_t *frame);

/* Records a kernel-level crash. frame is the trapped context, or NULL to
 * report the caller's own position. Returns 0 if the dump was stored where it
 * survives a reboot (or no disk exists, so memory is all there is) and -EIO
 * if a disk write failed. */
int crash_record(crash_kind_t kind, const char *reason, const regs_t *frame);

/* Records the death of p by the fault signal named signal_name. Does nothing
 * while an earlier dump is still waiting to be collected, so the first (and
 * usually most serious) event of a boot is the one that stays. */
void crash_record_user_fault(const struct process *p, const char *signal_name);

/* SYS_CRASH. Returns the operation's result or a negative errno. */
long crash_syscall(uint64_t op, void *buffer, size_t size);
