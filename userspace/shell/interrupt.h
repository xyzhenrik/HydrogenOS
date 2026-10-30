#pragma once

#include "shell_limits.h"

/* Ctrl+C support for a shell whose terminal sends SIGINT to the shell itself
 * (the desktop terminal does) instead of to a foreground process group. The
 * handler records the interrupt and forwards it to the foreground processes
 * the shell is waiting for. */
void interrupt_install(void);

/* True from the moment SIGINT arrives until interrupt_clear(). Long-running
 * builtins and the command-list loop poll it to stop early. */
int interrupt_pending(void);
void interrupt_clear(void);

/* Registers the processes of the foreground pipeline. A SIGINT that arrived
 * before they were spawned is forwarded right away. */
void interrupt_set_targets(const int *pids, int count);

/* Forgets a foreground process once it has been reaped, so its pid is never
 * signalled after the kernel may have reused it. */
void interrupt_target_reaped(int pid);
