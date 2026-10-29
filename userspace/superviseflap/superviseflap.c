#include "mini_libc.h"
#include "syscall.h"

/*
 * Daemon for supervisetest. Every instance appends its pid to the runs file.
 * The first instance then fails so that init has to restart the daemon; each
 * later instance stays alive.
 */

#define RUNS_PATH "/tmp/supervise.runs"

static int count_earlier_runs(void) {
    char buf[256];
    int lines = 0;
    long fd = open(RUNS_PATH, OPEN_READ);
    long length;
    if (fd < 0)
        return 0;
    length = read((int)fd, buf, sizeof(buf));
    close((int)fd);
    for (long i = 0; i < length; ++i) {
        if (buf[i] == '\n')
            ++lines;
    }
    return lines;
}

int main(void) {
    char line[24];
    int earlier = count_earlier_runs();
    int length = snprintf(line, sizeof(line), "%ld\n", sys_getpid());
    long fd = open(RUNS_PATH, OPEN_WRITE | OPEN_CREATE | OPEN_APPEND);

    if (fd < 0 || write((int)fd, line, (size_t)length) != length)
        return 2;
    close((int)fd);
    if (earlier == 0)
        return 1;
    for (;;)
        sleep_ms(1000);
}
