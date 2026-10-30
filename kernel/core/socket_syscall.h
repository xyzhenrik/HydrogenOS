#pragma once

#include <stdint.h>

#include "isr.h"
#include "process.h"

/* SYS_CONNECT, SYS_LISTEN, SYS_ACCEPT and SYS_SHUTDOWN_SOCK for the process
 * that issued the call. A call that cannot finish yet blocks the process on
 * its socket (PROCESS_BLOCKED_SOCKET); the scheduler repeats it through
 * socket_syscall_resume() once net_socket_wait_ready() reports progress. */
uint64_t socket_syscall_dispatch(process_t *proc, const regs_t *r);

/* sys_read / sys_write on a stream socket. Returns 0 when r->rdi is not one,
 * leaving the call to the generic handle code; otherwise 1 with the syscall
 * result in *result (0 for a call that blocked). A read returns the buffered
 * bytes or 0 at end of stream; a write queues what fits and may accept fewer
 * bytes than requested. Both wait for at least one byte of progress. */
int socket_syscall_stream_io(process_t *proc, const regs_t *r, int is_write,
                             uint64_t *result);

/* Repeats the syscall p blocked in, using the registers saved at syscall
 * entry. Returns 0 once it completed (frame.rax holds the result) and -1 when
 * p has to keep waiting. */
int socket_syscall_resume(process_t *p);
