#pragma once

/*
 * SYS_CRASH operations. The values are part of the syscall ABI;
 * userspace/lib/crash.h mirrors this file.
 */
#define HYDROGEN_CRASH_OP_TEST  0u /* root: record a simulated oops, keep running */
#define HYDROGEN_CRASH_OP_READ  1u /* root: copy the pending dump to the buffer */
#define HYDROGEN_CRASH_OP_CLEAR 2u /* root: discard the pending dump */

/* Upper bound of a dump in bytes; a buffer of this size always suffices. */
#define HYDROGEN_CRASH_DUMP_MAX 16384u
