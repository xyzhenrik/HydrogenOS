#include "mini_libc.h"

enum {
    EMFILE_ERRNO = 24,
    HANDLE_SLOTS = 8,
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
    int dup_fds[HANDLE_SLOTS];
    int pipe_fds[2] = { -1, -1 };
    long fd;

    for (int i = 0; i < HANDLE_SLOTS; ++i) {
        dup_fds[i] = -1;
    }

    for (int i = 0; i < HANDLE_SLOTS; ++i) {
        fd = dup(1);
        if (fd < 0) {
            close_handles(dup_fds, HANDLE_SLOTS);
            write(1, "handlelimitcheck: initial dup failed\n", 37);
            return 1;
        }
        dup_fds[i] = (int)fd;
    }

    if (dup(1) != -EMFILE_ERRNO) {
        close_handles(dup_fds, HANDLE_SLOTS);
        write(1, "handlelimitcheck: dup did not hit emfile\n", 41);
        return 2;
    }

    if (open("/time", 0) != -EMFILE_ERRNO) {
        close_handles(dup_fds, HANDLE_SLOTS);
        write(1, "handlelimitcheck: open did not hit emfile\n", 42);
        return 3;
    }

    if (pipe(pipe_fds) != -EMFILE_ERRNO) {
        if (pipe_fds[0] >= 0) {
            close(pipe_fds[0]);
        }
        if (pipe_fds[1] >= 0) {
            close(pipe_fds[1]);
        }
        close_handles(dup_fds, HANDLE_SLOTS);
        write(1, "handlelimitcheck: pipe did not hit emfile\n", 42);
        return 4;
    }

    close(dup_fds[HANDLE_SLOTS - 1]);
    dup_fds[HANDLE_SLOTS - 1] = -1;
    fd = open("/time", 0);
    if (fd < 0) {
        close_handles(dup_fds, HANDLE_SLOTS);
        write(1, "handlelimitcheck: open did not recover\n", 39);
        return 5;
    }
    close((int)fd);

    close(dup_fds[HANDLE_SLOTS - 2]);
    dup_fds[HANDLE_SLOTS - 2] = -1;
    if (pipe(pipe_fds) != 0) {
        close_handles(dup_fds, HANDLE_SLOTS);
        write(1, "handlelimitcheck: pipe did not recover\n", 39);
        return 6;
    }
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    close_handles(dup_fds, HANDLE_SLOTS);
    write(1, "handlelimitcheck: emfile ok\n", 28);
    return 0;
}
