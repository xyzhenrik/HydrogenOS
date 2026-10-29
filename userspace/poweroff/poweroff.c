#include "mini_libc.h"
#include "syscall.h"

#define ERROR_IO 5
#define ERROR_NOT_SUPPORTED 38

int main(void) {
    long rc = sys_shutdown();
    if (rc == -ERROR_NOT_SUPPORTED)
        fprintf(2, "poweroff: ACPI S5 is not available on this machine\n");
    else if (rc == -ERROR_IO)
        fprintf(2, "poweroff: flushing the disk cache or entering S5 failed\n");
    else
        fprintf(2, "poweroff: failed (error %ld)\n", -rc);
    return 1;
}
