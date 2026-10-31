#include "mini_libc.h"
#include "syscall.h"

/*
 * Oneshot for supervisetest that never finishes. It leaves its pid behind so
 * the test can tell that init killed it, and it clears the runs file of an
 * earlier boot before the supervised daemon starts writing to it.
 */

#define RUNS_PATH "/tmp/supervise.runs"
#define HANG_PATH "/tmp/supervise.hang"

int main(void) {
    char line[24];
    int length = snprintf(line, sizeof(line), "%ld\n", sys_getpid());
    long fd;

    sys_unlink(RUNS_PATH);
    fd = open(HANG_PATH, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    if (fd < 0 || write((int)fd, line, (size_t)length) != length)
        return 2;
    close((int)fd);
    for (;;)
        sleep_ms(1000);
}
