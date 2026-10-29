#include "mini_libc.h"

enum {
    EMFILE_ERRNO = 24,
    MAX_DUPS = 128,
};

static void close_handles(const int *fds, int count) {
    if (!fds) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
}

int main(void) {
    int dup_fds[MAX_DUPS];
    int pipe_fds[2] = { -1, -1 };
    int count = 0;
    long fd;

    for (int i = 0; i < MAX_DUPS; ++i) {
        dup_fds[i] = -1;
    }

    /* Fill the handle table until it reports EMFILE. The exact number of
       free slots depends on how many handles stdio already occupies, so we
       probe instead of assuming a fixed slot count. */
    while (count < MAX_DUPS) {
        fd = dup(1);
        if (fd == -EMFILE_ERRNO) {
            break;
        }
        if (fd < 0) {
            close_handles(dup_fds, count);
            write(1, "handlelimitcheck: initial dup failed\n", 37);
            return 1;
        }
        dup_fds[count++] = (int)fd;
    }

    if (count == 0 || count == MAX_DUPS) {
        close_handles(dup_fds, count);
        write(1, "handlelimitcheck: dup did not hit emfile\n", 41);
        return 2;
    }

    if (open("/time", 0) != -EMFILE_ERRNO) {
        close_handles(dup_fds, count);
        write(1, "handlelimitcheck: open did not hit emfile\n", 42);
        return 3;
    }

    if (pipe(pipe_fds) != -EMFILE_ERRNO) {
        if (pipe_fds[0] >= 0) close(pipe_fds[0]);
        if (pipe_fds[1] >= 0) close(pipe_fds[1]);
        close_handles(dup_fds, count);
        write(1, "handlelimitcheck: pipe did not hit emfile\n", 42);
        return 4;
    }

    /* Free one slot and verify it becomes usable again. */
    close(dup_fds[count - 1]);
    dup_fds[count - 1] = -1;
    fd = open("/time", 0);
    if (fd < 0) {
        close_handles(dup_fds, count);
        write(1, "handlelimitcheck: open did not recover\n", 39);
        return 5;
    }
    close((int)fd);

    fd = dup(1);
    if (fd < 0) {
        close_handles(dup_fds, count);
        write(1, "handlelimitcheck: dup did not recover\n", 38);
        return 6;
    }
    close((int)fd);

    close_handles(dup_fds, count);
    write(1, "handlelimitcheck: emfile ok\n", 28);
    return 0;
}
