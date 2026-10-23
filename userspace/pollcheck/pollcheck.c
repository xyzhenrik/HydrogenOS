#include "mini_libc.h"

int main(void) {
    int fds[2] = { -1, -1 };
    char byte = 0;
    long ready;

    if (pipe(fds) != 0) {
        write(1, "pollcheck: pipe failed\n", 23);
        return 1;
    }

    ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
    if (ready != 0) {
        close(fds[0]);
        close(fds[1]);
        write(1, "pollcheck: read not idle\n", 25);
        return 2;
    }

    ready = poll_fd(fds[1], POLL_WRITE | POLL_HUP);
    if (ready != POLL_WRITE) {
        close(fds[0]);
        close(fds[1]);
        write(1, "pollcheck: write not ready\n", 27);
        return 3;
    }

    if (write(fds[1], "x", 1) != 1) {
        close(fds[0]);
        close(fds[1]);
        write(1, "pollcheck: write failed\n", 24);
        return 4;
    }

    ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
    if (ready != POLL_READ) {
        close(fds[0]);
        close(fds[1]);
        write(1, "pollcheck: read not signaled\n", 29);
        return 5;
    }

    close(fds[1]);
    ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
    if ((ready & (POLL_READ | POLL_HUP)) != (POLL_READ | POLL_HUP)) {
        close(fds[0]);
        write(1, "pollcheck: hup on reader missing\n", 33);
        return 6;
    }

    if (read(fds[0], &byte, 1) != 1 || byte != 'x') {
        close(fds[0]);
        write(1, "pollcheck: read payload failed\n", 31);
        return 7;
    }

    ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
    if ((ready & (POLL_READ | POLL_HUP)) != (POLL_READ | POLL_HUP)) {
        close(fds[0]);
        write(1, "pollcheck: eof readiness missing\n", 33);
        return 8;
    }
    close(fds[0]);

    if (pipe(fds) != 0) {
        write(1, "pollcheck: pipe 2 failed\n", 25);
        return 9;
    }
    close(fds[0]);
    ready = poll_fd(fds[1], POLL_WRITE | POLL_HUP);
    if (ready != POLL_HUP) {
        close(fds[1]);
        write(1, "pollcheck: writer hup missing\n", 30);
        return 10;
    }
    close(fds[1]);

    write(1, "pollcheck: poll ok\n", 19);
    return 0;
}
