#pragma once

#include <stdint.h>

#include "isr.h"

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_OPEN 3
#define SYS_CLOSE 4
#define SYS_LSEEK 5
#define SYS_SPAWN 8
#define SYS_WAIT 9
#define SYS_GETPID 10
#define SYS_SLEEP 11
#define SYS_YIELD 12
#define SYS_TIME_MS 13
#define SYS_TTY_SET_COLOR 14
#define SYS_PIPE 15
#define SYS_HANDLE_SET_INHERIT 16
#define SYS_SPAWN_WITH_STDIO 17
#define SYS_DUP 18
#define SYS_DUP2 19
#define SYS_DUP3 20
#define SYS_POLL 21
#define SYS_POLL_WAIT 22
#define SYS_POLL_MANY 23
#define SYS_SET_PRIORITY 24
#define SYS_STAT    25
#define SYS_FSTAT   26
#define SYS_READDIR 27
#define SYS_GETCWD  28
#define SYS_CHDIR   29
#define SYS_MKDIR   30
#define SYS_UNLINK  31
#define SYS_RENAME  32
#define SYS_WRITEF  33
#define SYS_FB_INFO 36
#define SYS_FB_BLIT 37
#define SYS_MOUSE_STATE 38
#define SYS_KBD_READ 39
#define SYS_CONSOLE_CURSOR 40
#define SYS_FB_BLIT_STRIDED 41
#define SYS_FB_CLEAR 42
#define SYS_SYSINFO 43
#define SYS_SET_LAYOUT 44
#define SYS_MMAP 45
#define SYS_MUNMAP 46
#define SYS_RTC_TIME 54
#define SYS_RMDIR 47
#define SYS_SHUTDOWN 48
#define SYS_REBOOT 49
#define SYS_NET_INFO 50
#define SYS_NET_PING 51
#define SYS_SPAWN_ARGS 52
#define SYS_TRY_WAIT 53
#define SYS_MPROTECT 55
#define SYS_KILL 56
#define SYS_SIGACTION 57
#define SYS_SIGRETURN 58
#define SYS_SOCKET 59
#define SYS_BIND 60
#define SYS_SENDTO 61
#define SYS_RECVFROM 62
#define SYS_LINK 63
#define SYS_SYMLINK 64
#define SYS_READLINK 65
#define SYS_SIGPROCMASK 68
#define SYS_EXECVE 73
#define SYS_GETUID 74
#define SYS_SETUID 75
#define SYS_GETGID 76
#define SYS_SETGID 77
#define SYS_CHMOD 78
#define SYS_CHOWN 79
#define SYS_TRUNCATE 80
#define SYS_PERF_READ 81
#define SYS_PUTCHAR 255

uint64_t syscall_handle(regs_t *r);
void syscall_set_user_return(uintptr_t rip, uintptr_t rsp);

struct process;

/* True for the first user process, whose death hands the CPU back to the
 * kernel shell. */
int syscall_is_bootstrap(const struct process *proc);

/*
 * Called once `exited` is dead. If it is the bootstrap process and nothing
 * else can run, rewrites r to resume in the kernel shell and returns 1; the
 * caller then owns destroying `exited`. Returns 0 otherwise.
 */
int syscall_bootstrap_exited(const struct process *exited, regs_t *r,
                             uint64_t status);
