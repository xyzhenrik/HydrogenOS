#include "mini_libc.h"
#include "syscall.h"

#define ERROR_IO 5

int main(void) {
    long rc = sys_reboot();
    if (rc == -ERROR_IO)
        fprintf(2, "reboot: flushing the disk cache failed, not rebooting\n");
    else
        fprintf(2, "reboot: failed (error %ld)\n", -rc);
    return 1;
}
