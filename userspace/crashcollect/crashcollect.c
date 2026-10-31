#include "crash.h"
#include "mini_libc.h"

/*
 * Boot service: turns a crash dump that the kernel found on the disk (left by
 * the previous boot) into /var/crash/crash-N.txt. Having nothing to collect is
 * the normal case and not an error.
 */
int main(void) {
    char path[64];
    long rc = crash_collect(path, sizeof(path));
    if (rc < 0) {
        dprintf(2, "crashcollect: failed (error %ld)\n", -rc);
        return 1;
    }
    if (rc > 0)
        printf("crashcollect: saved %s\n", path);
    return 0;
}
