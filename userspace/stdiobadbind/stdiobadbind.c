#include "mini_libc.h"

static int expect_spawn_failure_with_stdin_writer(void) {
    int fds[2] = { -1, -1 };
    long pid;
    if (pipe(fds) != 0) {
        return -1;
    }
    pid = spawn_with_stdio("/bin/sleep", fds[1], -1, -1);
    close(fds[0]);
    close(fds[1]);
    return pid < 0 ? 0 : -1;
}

static int expect_spawn_failure_with_stdout_reader(void) {
    int fds[2] = { -1, -1 };
    long pid;
    if (pipe(fds) != 0) {
        return -1;
    }
    pid = spawn_with_stdio("/bin/sleep", -1, fds[0], -1);
    close(fds[0]);
    close(fds[1]);
    return pid < 0 ? 0 : -1;
}

static int expect_spawn_failure_with_stderr_reader(void) {
    int fds[2] = { -1, -1 };
    long pid;
    if (pipe(fds) != 0) {
        return -1;
    }
    pid = spawn_with_stdio("/bin/sleep", -1, -1, fds[0]);
    close(fds[0]);
    close(fds[1]);
    return pid < 0 ? 0 : -1;
}

int main(void) {
    if (expect_spawn_failure_with_stdin_writer() != 0) {
        write(1, "stdiobadbind: stdin writer accepted\n", 36);
        return 1;
    }
    if (expect_spawn_failure_with_stdout_reader() != 0) {
        write(1, "stdiobadbind: stdout reader accepted\n", 37);
        return 1;
    }
    if (expect_spawn_failure_with_stderr_reader() != 0) {
        write(1, "stdiobadbind: stderr reader accepted\n", 37);
        return 1;
    }
    write(1, "stdiobadbind: invalid stdio rejected\n", 37);
    return 0;
}
