#include "mini_libc.h"

enum {
    ESPIPE_ERRNO = 29,
};

static int bytes_eq(const char *a, const char *b, int len) {
    for (int i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    char control[12];
    char got[4];
    int pipe_fds[2] = { -1, -1 };
    long fd;
    long alias_fd;
    long control_fd;
    long n;

    control_fd = open("/time", 0);
    if (control_fd < 0) {
        write(1, "seekcheck: control open failed\n", 31);
        return 1;
    }
    n = read((int)control_fd, control, sizeof(control));
    close((int)control_fd);
    if (n != (long)sizeof(control)) {
        write(1, "seekcheck: control read failed\n", 31);
        return 2;
    }

    fd = open("/time", 0);
    if (fd < 0) {
        write(1, "seekcheck: open failed\n", 23);
        return 3;
    }
    alias_fd = dup((int)fd);
    if (alias_fd < 0) {
        close((int)fd);
        write(1, "seekcheck: dup failed\n", 22);
        return 4;
    }

    if (lseek((int)fd, 4, SEEK_SET) != 4) {
        close((int)alias_fd);
        close((int)fd);
        write(1, "seekcheck: seek set failed\n", 27);
        return 5;
    }
    if (read((int)alias_fd, got, sizeof(got)) != (long)sizeof(got) ||
        !bytes_eq(got, control + 4, sizeof(got))) {
        close((int)alias_fd);
        close((int)fd);
        write(1, "seekcheck: shared set failed\n", 29);
        return 6;
    }

    if (lseek((int)alias_fd, 4, SEEK_CUR) != 12) {
        close((int)alias_fd);
        close((int)fd);
        write(1, "seekcheck: seek cur failed\n", 27);
        return 7;
    }
    if (lseek((int)fd, -4, SEEK_CUR) != 8) {
        close((int)alias_fd);
        close((int)fd);
        write(1, "seekcheck: seek back failed\n", 28);
        return 8;
    }
    if (read((int)alias_fd, got, sizeof(got)) != (long)sizeof(got) ||
        !bytes_eq(got, control + 8, sizeof(got))) {
        close((int)alias_fd);
        close((int)fd);
        write(1, "seekcheck: shared cur failed\n", 29);
        return 9;
    }

    if (lseek((int)fd, 0, SEEK_SET) != 0) {
        close((int)alias_fd);
        close((int)fd);
        write(1, "seekcheck: rewind failed\n", 25);
        return 10;
    }
    if (read((int)alias_fd, got, sizeof(got)) != (long)sizeof(got) ||
        !bytes_eq(got, control, sizeof(got))) {
        close((int)alias_fd);
        close((int)fd);
        write(1, "seekcheck: shared rewind failed\n", 32);
        return 11;
    }

    close((int)alias_fd);
    close((int)fd);

    if (pipe(pipe_fds) != 0) {
        write(1, "seekcheck: pipe failed\n", 23);
        return 12;
    }
    if (lseek(pipe_fds[0], 0, SEEK_SET) != -ESPIPE_ERRNO) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        write(1, "seekcheck: pipe seek wrong\n", 27);
        return 13;
    }
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    write(1, "seekcheck: seek ok\n", 19);
    return 0;
}
