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
#define SYS_MMAP    34
#define SYS_MUNMAP  35
#define SYS_FB_INFO 36
#define SYS_FB_BLIT 37
#define SYS_MOUSE_STATE 38
#define SYS_KBD_READ 39
#define SYS_CONSOLE_CURSOR 40
#define SYS_FB_BLIT_STRIDED 41
#define SYS_FB_CLEAR 42
#define SYS_SYSINFO 43
#define SYS_SET_LAYOUT 44
#define SYS_PUTCHAR 255

uint64_t syscall_handle(regs_t *r);
void syscall_set_user_return(uintptr_t rip, uintptr_t rsp);
