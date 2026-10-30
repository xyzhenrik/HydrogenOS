#pragma once

#include <stddef.h>

#include "syscall.h"

/*
 * SYS_CRASH: access to the kernel crash dump (docs/CRASH_OOM.md). Mirrors
 * kernel/include/crash_abi.h. Every operation is reserved for root and fails
 * with -EPERM otherwise.
 */
#define HYDROGEN_CRASH_OP_TEST  0u /* record a simulated oops; the system keeps running */
#define HYDROGEN_CRASH_OP_READ  1u /* copy the pending dump to the buffer */
#define HYDROGEN_CRASH_OP_CLEAR 2u /* discard the pending dump */
#define HYDROGEN_CRASH_DUMP_MAX 16384u

#define CRASH_DIRECTORY "/var/crash"

/* READ returns the number of bytes copied, or -ENOENT if no dump is pending. */
static inline long sys_crash(unsigned long op, void *buffer, unsigned long size) {
    return syscall3(SYS_CRASH, (long)op, (long)buffer, (long)size);
}

/*
 * Stores the pending kernel dump as the next free /var/crash/crash-N.txt and
 * then discards it in the kernel. Returns 1 and the file's path in path when a
 * dump was collected, 0 if none was pending, or a negative errno; the kernel
 * copy stays untouched on failure.
 */
long crash_collect(char *path, size_t path_cap);
