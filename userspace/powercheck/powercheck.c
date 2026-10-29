#include "mini_libc.h"
#include "syscall.h"

#define ERROR_INVALID 22

/* Exercises the power syscalls without changing the machine state: the
 * flags word is reserved and must be rejected before any transition. */
int main(void) {
    if (syscall1(SYS_SHUTDOWN, 1) != -ERROR_INVALID) {
        dprintf(2, "powercheck: shutdown accepted reserved flags\n");
        return 1;
    }
    if (syscall1(SYS_REBOOT, 1) != -ERROR_INVALID) {
        dprintf(2, "powercheck: reboot accepted reserved flags\n");
        return 1;
    }
    printf("POWERCHECK PASS\n");
    return 0;
}
