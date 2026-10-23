#include "mini_libc.h"

static int str_eq(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

int main(void) {
    static const char console_msg[] = "dupfallbackcheck: implicit stdout live\n";
    static const char pipe_msg[] = "dupfallbackcheck: pipe redirect ok\n";
    int fds[2] = { -1, -1 };
    char buf[64];
    long saved_stdout;
    long got;

    saved_stdout = dup(1);
    if (saved_stdout < 0) {
        return 1;
    }
    if (write((int)saved_stdout, console_msg, sizeof(console_msg) - 1) !=
        (long)(sizeof(console_msg) - 1)) {
        close((int)saved_stdout);
        return 2;
    }
    if (pipe(fds) != 0) {
        close((int)saved_stdout);
        return 3;
    }
    if (dup2(fds[1], 1) != 1) {
        close((int)saved_stdout);
        close(fds[0]);
        close(fds[1]);
        return 4;
    }
    close(fds[1]);
    if (write(1, pipe_msg, sizeof(pipe_msg) - 1) != (long)(sizeof(pipe_msg) - 1)) {
        close((int)saved_stdout);
        close(fds[0]);
        return 5;
    }
    if (dup2((int)saved_stdout, 1) != 1) {
        close((int)saved_stdout);
        close(fds[0]);
        return 6;
    }
    close((int)saved_stdout);
    got = read(fds[0], buf, sizeof(buf) - 1);
    close(fds[0]);
    if (got != (long)(sizeof(pipe_msg) - 1)) {
        return 7;
    }
    buf[got] = 0;
    return str_eq(buf, pipe_msg) ? 0 : 8;
}
