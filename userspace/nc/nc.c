#include "mini_libc.h"
#include "syscall.h"

/*
 * nc - connect two byte streams over TCP.
 *
 *   nc HOST PORT     connect to HOST (dotted quad) and relay
 *   nc -l PORT       wait for one connection on PORT and relay
 *
 * Relaying copies stdin to the socket and the socket to stdout. End of input
 * half-closes the connection; the program ends when the peer closes its side.
 * stdin must be pollable (a pipe or file); a console is never reported ready,
 * so with a console on stdin only the receive direction works.
 */

#define NC_BUFFER_SIZE 1024
#define NC_PORT_MAX    65535u

static int fail(const char *what, long value) {
    dprintf(2, "nc: %s failed (%ld)\n", what, value);
    return 1;
}

static int usage(void) {
    dprintf(2, "usage: nc HOST PORT | nc -l PORT\n");
    return 2;
}

/* Strict decimal; returns -1 for anything but 1..65535. */
static long parse_port(const char *s) {
    unsigned long value = 0;

    if (*s == '\0')
        return -1;
    for (; *s != '\0'; ++s) {
        if (*s < '0' || *s > '9')
            return -1;
        value = value * 10 + (unsigned long)(*s - '0');
        if (value > NC_PORT_MAX)
            return -1;
    }
    return value == 0 ? -1 : (long)value;
}

/* Strict dotted quad; returns 0 and the address in host order, or -1. */
static int parse_ipv4(const char *s, unsigned int *out) {
    unsigned int ip = 0;

    for (int part = 0; part < 4; ++part) {
        unsigned int value = 0;
        int digits = 0;

        while (*s >= '0' && *s <= '9' && digits < 3) {
            value = value * 10 + (unsigned int)(*s - '0');
            ++s;
            ++digits;
        }
        if (digits == 0 || value > 255)
            return -1;
        ip = (ip << 8) | value;
        if (part < 3) {
            if (*s != '.')
                return -1;
            ++s;
        }
    }
    if (*s != '\0')
        return -1;
    *out = ip;
    return 0;
}

static long write_all(int fd, const char *data, unsigned long len) {
    unsigned long done = 0;

    while (done < len) {
        long rc = write(fd, data + done, len - done);

        if (rc <= 0)
            return rc == 0 ? -1 : rc;
        done += (unsigned long)rc;
    }
    return 0;
}

static int relay(int sock) {
    char buf[NC_BUFFER_SIZE];
    int stdin_open = 1;

    for (;;) {
        poll_fd_t fds[2];
        long ready;

        fds[0].fd = sock;
        fds[0].events = POLL_READ | POLL_HUP;
        fds[0].revents = 0;
        fds[1].fd = stdin_open ? 0 : -1;
        fds[1].events = POLL_READ | POLL_HUP;
        fds[1].revents = 0;
        ready = poll_many_wait(fds, 2, POLL_WAIT_INFINITE);
        if (ready < 0)
            return fail("poll", ready);

        if (fds[0].revents & (POLL_READ | POLL_HUP)) {
            long n = read(sock, buf, sizeof(buf));

            if (n < 0)
                return fail("receive", n);
            if (n == 0)
                return 0;
            if (write_all(1, buf, (unsigned long)n) != 0)
                return fail("write stdout", n);
        }
        if (fds[1].fd == 0 && (fds[1].revents & (POLL_READ | POLL_HUP))) {
            long n = read(0, buf, sizeof(buf));

            if (n < 0)
                return fail("read stdin", n);
            if (n == 0) {
                stdin_open = 0;
                if (sys_shutdown_sock(sock, NET_SHUT_WR) != 0)
                    return fail("shutdown", n);
            } else if (write_all(sock, buf, (unsigned long)n) != 0) {
                return fail("send", n);
            }
        }
    }
}

int main(int argc, char **argv) {
    unsigned int address = 0;
    long port;
    long sock;
    long rc;
    int status;

    if (argc == 3 && strcmp(argv[1], "-l") == 0) {
        port = parse_port(argv[2]);
        if (port < 0)
            return usage();
        sock = sys_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
        if (sock < 0)
            return fail("socket", sock);
        rc = sys_bind((int)sock, 0, (unsigned int)port);
        if (rc == 0)
            rc = sys_listen((int)sock, 1);
        if (rc != 0)
            return fail("listen", rc);
        rc = sys_accept((int)sock, 0);
        if (rc < 0)
            return fail("accept", rc);
        close((int)sock);
        sock = rc;
    } else if (argc == 3) {
        port = parse_port(argv[2]);
        if (port < 0 || parse_ipv4(argv[1], &address) != 0)
            return usage();
        sock = sys_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
        if (sock < 0)
            return fail("socket", sock);
        rc = sys_connect((int)sock, address, (unsigned int)port);
        if (rc != 0)
            return fail("connect", rc);
    } else {
        return usage();
    }

    status = relay((int)sock);
    close((int)sock);
    return status;
}
