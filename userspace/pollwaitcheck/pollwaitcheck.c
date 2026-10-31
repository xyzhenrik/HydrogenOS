#include "mini_libc.h"

int main(void) {
    int fds[2] = { -1, -1 };
    int status = 0;
    long pid;
    long ready;
    unsigned long start;
    unsigned long elapsed;
    char byte = 0;

    if (pipe(fds) != 0) {
        write(1, "pollwaitcheck: pipe failed\n", 27);
        return 1;
    }

    start = time_ms();
    ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 40);
    elapsed = time_ms() - start;
    if (ready != 0) {
        close(fds[0]);
        close(fds[1]);
        write(1, "pollwaitcheck: timeout returned events\n", 39);
        return 2;
    }
    if (elapsed < 20) {
        close(fds[0]);
        close(fds[1]);
        write(1, "pollwaitcheck: timeout too short\n", 33);
        return 3;
    }
    close(fds[0]);
    close(fds[1]);

    if (pipe(fds) != 0) {
        write(1, "pollwaitcheck: pipe 2 failed\n", 29);
        return 4;
    }

    pid = spawn_with_stdio("/lib/tests/pollwakewriter", SPAWN_STDIO_NULL, fds[1],
                           SPAWN_STDIO_NULL);
    if (pid <= 0) {
        close(fds[0]);
        close(fds[1]);
        write(1, "pollwaitcheck: spawn failed\n", 28);
        return 5;
    }

    close(fds[1]);
    ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 500);
    if ((ready & POLL_READ) == 0) {
        close(fds[0]);
        wait_pid((int)pid, &status);
        write(1, "pollwaitcheck: read wake missing\n", 33);
        return 6;
    }

    if (read(fds[0], &byte, 1) != 1 || byte != 'w') {
        close(fds[0]);
        wait_pid((int)pid, &status);
        write(1, "pollwaitcheck: payload failed\n", 30);
        return 7;
    }

    if (wait_pid((int)pid, &status) != pid || status != 0) {
        close(fds[0]);
        write(1, "pollwaitcheck: child wait failed\n", 33);
        return 8;
    }

    ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 100);
    if ((ready & (POLL_READ | POLL_HUP)) != (POLL_READ | POLL_HUP)) {
        close(fds[0]);
        write(1, "pollwaitcheck: eof wake missing\n", 32);
        return 9;
    }

    close(fds[0]);
    write(1, "pollwaitcheck: wait ok\n", 23);
    return 0;
}
