#include "crash.h"

#include "errno.h"
#include "mini_libc.h"

enum {
    CRASH_MAX_FILES = 9999,
    /* The block cache writes dirty sectors back after about a second; the
       kernel copy of the dump may only go once the file is on disk. */
    CRASH_WRITE_BACK_WAIT_MS = 1500,
};

static char dump[HYDROGEN_CRASH_DUMP_MAX];

long crash_collect(char *path, size_t path_cap) {
    long length = sys_crash(HYDROGEN_CRASH_OP_READ, dump, sizeof(dump));
    if (length == -ENOENT)
        return 0;
    if (length < 0)
        return length;

    sys_mkdir(CRASH_DIRECTORY);
    int found = 0;
    for (int number = 1; number <= CRASH_MAX_FILES && !found; ++number) {
        stat_t info;
        snprintf(path, path_cap, CRASH_DIRECTORY "/crash-%d.txt", number);
        found = stat(path, &info) != 0;
    }
    if (!found)
        return -ENOSPC;

    long written = sys_writef(path, dump, (unsigned long)length);
    if (written < 0)
        return written;
    sleep_ms(CRASH_WRITE_BACK_WAIT_MS);
    long cleared = sys_crash(HYDROGEN_CRASH_OP_CLEAR, 0, 0);
    return cleared < 0 ? cleared : 1;
}
