#include "mini_libc.h"

int main(void) {
    int idle_fds[2] = { -1, -1 };
    int left[2] = { -1, -1 };
    int right[2] = { -1, -1 };
    poll_fd_t pollfds[3];
    int status = 0;
    long pid = 0;
    long ready = 0;
    char byte = 0;

    if (pipe(idle_fds) != 0) {
        write(1, "pollmanycheck: idle pipe failed\n", 32);
        return 1;
    }
    pollfds[0].fd = -1;
    pollfds[0].events = POLL_READ;
    pollfds[0].revents = 0;
    pollfds[1].fd = 99;
    pollfds[1].events = POLL_READ | POLL_HUP;
    pollfds[1].revents = 0;
    pollfds[2].fd = idle_fds[1];
    pollfds[2].events = POLL_WRITE | POLL_HUP;
    pollfds[2].revents = 0;
    ready = poll_many_wait(pollfds, 3, 0);
    if (ready != 2 || pollfds[0].revents != 0 ||
        pollfds[1].revents != POLL_NVAL ||
        pollfds[2].revents != POLL_WRITE) {
        close(idle_fds[0]);
        close(idle_fds[1]);
        write(1, "pollmanycheck: snapshot invalid failed\n", 39);
        return 2;
    }
    close(idle_fds[0]);
    close(idle_fds[1]);

    if (pipe(left) != 0 || pipe(right) != 0) {
        if (left[0] >= 0) close(left[0]);
        if (left[1] >= 0) close(left[1]);
        if (right[0] >= 0) close(right[0]);
        if (right[1] >= 0) close(right[1]);
        write(1, "pollmanycheck: wait pipes failed\n", 33);
        return 3;
    }

    pollfds[0].fd = -1;
    pollfds[0].events = POLL_READ;
    pollfds[0].revents = 0;
    pollfds[1].fd = left[0];
    pollfds[1].events = POLL_READ | POLL_HUP;
    pollfds[1].revents = 0;
    pollfds[2].fd = right[0];
    pollfds[2].events = POLL_READ | POLL_HUP;
    pollfds[2].revents = 0;

    ready = poll_many_wait(pollfds, 3, 40);
    if (ready != 0 || pollfds[0].revents != 0 ||
        pollfds[1].revents != 0 || pollfds[2].revents != 0) {
        close(left[0]);
        close(left[1]);
        close(right[0]);
        close(right[1]);
        write(1, "pollmanycheck: timeout failed\n", 30);
        return 4;
    }

    pid = spawn_with_stdio("/pollwakewriter", SPAWN_STDIO_NULL, right[1],
                           SPAWN_STDIO_NULL);
    if (pid <= 0) {
        close(left[0]);
        close(left[1]);
        close(right[0]);
        close(right[1]);
        write(1, "pollmanycheck: spawn failed\n", 28);
        return 5;
    }

    close(right[1]);
    ready = poll_many_wait(pollfds, 3, 500);
    if (ready != 1 || pollfds[0].revents != 0 || pollfds[1].revents != 0 ||
        (pollfds[2].revents & POLL_READ) == 0) {
        close(left[0]);
        close(left[1]);
        close(right[0]);
        wait_pid((int)pid, &status);
        write(1, "pollmanycheck: wake failed\n", 27);
        return 6;
    }

    if (read(right[0], &byte, 1) != 1 || byte != 'w') {
        close(left[0]);
        close(left[1]);
        close(right[0]);
        wait_pid((int)pid, &status);
        write(1, "pollmanycheck: payload failed\n", 30);
        return 7;
    }

    if (wait_pid((int)pid, &status) != pid || status != 0) {
        close(left[0]);
        close(left[1]);
        close(right[0]);
        write(1, "pollmanycheck: child wait failed\n", 33);
        return 8;
    }

    close(left[1]);
    pollfds[0].revents = 0;
    pollfds[1].revents = 0;
    pollfds[2].revents = 0;
    ready = poll_many_wait(pollfds, 3, 100);
    if (ready != 2 || pollfds[0].revents != 0 ||
        (pollfds[1].revents & (POLL_READ | POLL_HUP)) != (POLL_READ | POLL_HUP) ||
        (pollfds[2].revents & (POLL_READ | POLL_HUP)) != (POLL_READ | POLL_HUP)) {
        close(left[0]);
        close(right[0]);
        write(1, "pollmanycheck: eof fanin failed\n", 32);
        return 9;
    }

    close(left[0]);
    close(right[0]);
    write(1, "pollmanycheck: many ok\n", 23);
    return 0;
}
