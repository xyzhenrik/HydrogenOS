#include "mini_libc.h"
#include "syscall.h"

/*
 * tcptest - exercises the TCP sockets over loopback; no external network and
 * not even a NIC is needed.
 *
 * Checks the error paths, the handshake and accept queue inside one process,
 * half-close and reset semantics, and finally a 64 KiB transfer against an
 * echo server running as a child ("tcptest serve <port>").
 */

#define ERR_EBADF         (-9)
#define ERR_EFAULT        (-14)
#define ERR_EINVAL        (-22)
#define ERR_EPIPE         (-32)
#define ERR_ENOTSOCK      (-88)
#define ERR_EPROTONOSUPPORT (-93)
#define ERR_EOPNOTSUPP    (-95)
#define ERR_EADDRINUSE    (-98)
#define ERR_ECONNRESET    (-104)
#define ERR_EISCONN       (-106)
#define ERR_ENOTCONN      (-107)
#define ERR_ECONNREFUSED  (-111)

#define LOOPBACK        NET_IPV4_ADDR(127, 0, 0, 1)
#define BASE_PORT       42000u
#define ECHO_PORT       (BASE_PORT + 20)
#define TRANSFER_SIZE   (64 * 1024)
#define CHUNK_SIZE      1000
#define CONNECT_TRIES   100
#define CONNECT_WAIT_MS 50
#define IO_TIMEOUT_MS   20000
#define LATE_CONNECT_DELAY_MS 100
#define UNMAPPED_PTR    ((void *)0x300000000000UL)

static int fail(const char *what, long value) {
    dprintf(2, "tcptest: FAIL %s (%ld)\n", what, value);
    return 1;
}

#define EXPECT(what, actual, expected)                    \
    do {                                                  \
        long actual_value = (actual);                     \
        if (actual_value != (long)(expected))             \
            return fail(what, actual_value);              \
    } while (0)

static unsigned char pattern_byte(unsigned long index) {
    return (unsigned char)(index * 31u + (index >> 8) + 7u);
}

static long stream_socket(void) {
    return sys_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
}

/* A listener on 127.0.0.1:port with a small backlog. */
static long make_listener(unsigned int port) {
    long fd = stream_socket();

    if (fd < 0)
        return fd;
    if (sys_bind((int)fd, LOOPBACK, port) != 0 || sys_listen((int)fd, 4) != 0) {
        close((int)fd);
        return -1;
    }
    return fd;
}

static long write_all(int fd, const unsigned char *data, unsigned long len) {
    unsigned long done = 0;

    while (done < len) {
        long rc = write(fd, data + done, len - done);

        if (rc <= 0)
            return rc == 0 ? -1 : rc;
        done += (unsigned long)rc;
    }
    return (long)done;
}

static int check_socket_errors(void) {
    unsigned char buf[8];
    long fd = stream_socket();
    long udp = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
    long file;
    long a;
    long b;

    if (fd < 0 || udp < 0)
        return fail("socket", fd < 0 ? fd : udp);
    EXPECT("tcp with udp protocol",
           sys_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_UDP),
           ERR_EPROTONOSUPPORT);

    EXPECT("read unconnected", read((int)fd, buf, sizeof(buf)), ERR_ENOTCONN);
    EXPECT("write unconnected", write((int)fd, "x", 1), ERR_ENOTCONN);
    EXPECT("shutdown unconnected", sys_shutdown_sock((int)fd, NET_SHUT_WR),
           ERR_ENOTCONN);
    EXPECT("accept without listen", sys_accept((int)fd, 0), ERR_EINVAL);
    EXPECT("shutdown bad how", sys_shutdown_sock((int)fd, 3), ERR_EINVAL);
    EXPECT("connect port 0", sys_connect((int)fd, LOOPBACK, 0), ERR_EINVAL);
    EXPECT("connect port 65536", sys_connect((int)fd, LOOPBACK, 65536), ERR_EINVAL);
    EXPECT("connect broadcast", sys_connect((int)fd, 0xFFFFFFFFu, BASE_PORT),
           ERR_EINVAL);
    EXPECT("connect refused", sys_connect((int)fd, LOOPBACK, BASE_PORT),
           ERR_ECONNREFUSED);
    /* A refused socket can try again. */
    EXPECT("connect refused again", sys_connect((int)fd, LOOPBACK, BASE_PORT),
           ERR_ECONNREFUSED);

    EXPECT("udp connect", sys_connect((int)udp, LOOPBACK, BASE_PORT), ERR_EOPNOTSUPP);
    EXPECT("udp listen", sys_listen((int)udp, 1), ERR_EOPNOTSUPP);
    EXPECT("udp accept", sys_accept((int)udp, 0), ERR_EOPNOTSUPP);
    EXPECT("udp shutdown", sys_shutdown_sock((int)udp, NET_SHUT_WR), ERR_EOPNOTSUPP);
    EXPECT("udp stream write", write((int)udp, "x", 1) < 0, 1);
    EXPECT("tcp sendto", sys_sendto((int)fd, "x", 1, LOOPBACK, BASE_PORT),
           ERR_EOPNOTSUPP);
    EXPECT("tcp recvfrom",
           sys_recvfrom((int)fd, buf, sizeof(buf), NET_MSG_DONTWAIT, 0),
           ERR_EOPNOTSUPP);

    EXPECT("listen bad fd", sys_listen(60, 1), ERR_EBADF);
    EXPECT("connect bad fd", sys_connect(60, LOOPBACK, BASE_PORT), ERR_EBADF);
    file = open("/hello.txt", OPEN_READ);
    if (file >= 0) {
        EXPECT("connect file", sys_connect((int)file, LOOPBACK, BASE_PORT),
               ERR_ENOTSOCK);
        close((int)file);
    }

    /* TCP and UDP ports are separate spaces; two TCP sockets conflict. */
    EXPECT("udp bind", sys_bind((int)udp, LOOPBACK, BASE_PORT + 1), 0);
    a = stream_socket();
    b = stream_socket();
    EXPECT("tcp bind next to udp", sys_bind((int)a, LOOPBACK, BASE_PORT + 1), 0);
    EXPECT("tcp bind in use", sys_bind((int)b, LOOPBACK, BASE_PORT + 1),
           ERR_EADDRINUSE);
    EXPECT("listen", sys_listen((int)a, 2), 0);
    EXPECT("listen again", sys_listen((int)a, 3), 0);
    EXPECT("connect on listener", sys_connect((int)a, LOOPBACK, BASE_PORT), ERR_EINVAL);
    EXPECT("read on listener", read((int)a, buf, sizeof(buf)), ERR_ENOTCONN);
    EXPECT("accept bad pointer", sys_accept((int)a, UNMAPPED_PTR), ERR_EFAULT);

    close((int)a);
    close((int)b);
    close((int)udp);
    close((int)fd);
    return 0;
}

/* Handshake ahead of accept, two-way data and half-close. */
static int check_handshake_and_close(void) {
    unsigned char buf[64];
    net_sockaddr_t peer;
    long listener = make_listener(BASE_PORT + 2);
    long client = stream_socket();
    long server;
    poll_fd_t pfd;

    if (listener < 0 || client < 0)
        return fail("socket setup", listener < 0 ? listener : client);

    /* The handshake completes in the kernel; the connection waits in the queue. */
    EXPECT("connect", sys_connect((int)client, LOOPBACK, BASE_PORT + 2), 0);
    EXPECT("connect twice", sys_connect((int)client, LOOPBACK, BASE_PORT + 2),
           ERR_EISCONN);
    EXPECT("listener readable", poll_fd((int)listener, POLL_READ), POLL_READ);
    EXPECT("client writable", poll_fd((int)client, POLL_WRITE), POLL_WRITE);
    EXPECT("client not readable", poll_fd((int)client, POLL_READ), 0);

    server = sys_accept((int)listener, &peer);
    if (server < 0)
        return fail("accept", server);
    EXPECT("peer address", peer.ipv4, LOOPBACK);
    EXPECT("peer port", peer.port >= 49152, 1);
    EXPECT("listener drained", poll_fd((int)listener, POLL_READ), 0);

    EXPECT("write", write((int)client, "ping", 4), 4);
    EXPECT("server readable", poll_wait_fd((int)server, POLL_READ, IO_TIMEOUT_MS),
           POLL_READ);
    EXPECT("read", read((int)server, buf, sizeof(buf)), 4);
    EXPECT("payload", buf[0] == 'p' && buf[3] == 'g', 1);
    EXPECT("reply", write((int)server, "pong!", 5), 5);
    EXPECT("client read", read((int)client, buf, sizeof(buf)), 5);

    /* Half-close: the server sees the end of the stream and can still answer. */
    EXPECT("client shutdown", sys_shutdown_sock((int)client, NET_SHUT_WR), 0);
    EXPECT("write after shutdown", write((int)client, "x", 1), ERR_EPIPE);
    EXPECT("server eof", read((int)server, buf, sizeof(buf)), 0);
    EXPECT("late reply", write((int)server, "bye", 3), 3);
    EXPECT("client late read", read((int)client, buf, sizeof(buf)), 3);
    EXPECT("server shutdown", sys_shutdown_sock((int)server, NET_SHUT_WR), 0);
    EXPECT("client eof", read((int)client, buf, sizeof(buf)), 0);
    pfd.fd = (int)client;
    pfd.events = POLL_READ | POLL_HUP;
    pfd.revents = 0;
    EXPECT("client poll", poll_many_wait(&pfd, 1, 0), 1);
    EXPECT("client hup", pfd.revents & POLL_HUP, POLL_HUP);

    EXPECT("close client", close((int)client), 0);
    EXPECT("close server", close((int)server), 0);
    EXPECT("close listener", close((int)listener), 0);
    return 0;
}

/* Unread data at close resets the peer; a closed listener refuses. */
static int check_reset(void) {
    unsigned char buf[16];
    long listener = make_listener(BASE_PORT + 3);
    long client = stream_socket();
    long server;

    if (listener < 0 || client < 0)
        return fail("socket setup", listener < 0 ? listener : client);
    EXPECT("connect", sys_connect((int)client, LOOPBACK, BASE_PORT + 3), 0);
    server = sys_accept((int)listener, 0);
    if (server < 0)
        return fail("accept", server);

    EXPECT("write", write((int)client, "unread", 6), 6);
    EXPECT("poll before close", poll_wait_fd((int)server, POLL_READ, IO_TIMEOUT_MS),
           POLL_READ);
    EXPECT("server close", close((int)server), 0);
    EXPECT("client sees reset", read((int)client, buf, sizeof(buf)), ERR_ECONNRESET);
    EXPECT("reset reported once", read((int)client, buf, sizeof(buf)), 0);
    EXPECT("write after reset", write((int)client, "x", 1), ERR_EPIPE);

    EXPECT("close listener", close((int)listener), 0);
    EXPECT("close client", close((int)client), 0);
    client = stream_socket();
    EXPECT("connect to closed listener",
           sys_connect((int)client, LOOPBACK, BASE_PORT + 3), ERR_ECONNREFUSED);
    close((int)client);
    return 0;
}

/* A SYN that finds the accept queue full is dropped and retransmitted: the
 * child's connect() stays blocked until the parent made room. */
static int check_backlog(void) {
    long listener = make_listener(BASE_PORT + 4);
    long first = stream_socket();
    long accepted;
    long queued;
    char text[8];
    const char *argv[] = { "tcptest", "late", text, 0 };
    long pid;
    int status = -1;

    if (listener < 0)
        return fail("listener", listener);
    EXPECT("listen backlog 1", sys_listen((int)listener, 1), 0);
    EXPECT("first connect", sys_connect((int)first, LOOPBACK, BASE_PORT + 4), 0);

    snprintf(text, sizeof(text), "%u", BASE_PORT + 4);
    pid = spawn_args("/tcptest", argv, 0, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL,
                     SPAWN_STDIO_INHERIT);
    if (pid < 0)
        return fail("spawn late client", pid);
    sleep_ms(LATE_CONNECT_DELAY_MS);
    EXPECT("late client still waiting", try_wait_pid((int)pid, &status), 0);

    accepted = sys_accept((int)listener, 0);
    if (accepted < 0)
        return fail("accept first", accepted);
    queued = sys_accept((int)listener, 0);
    if (queued < 0)
        return fail("accept late", queued);
    EXPECT("reap late client", wait_pid((int)pid, &status), pid);
    EXPECT("late client status", status, 0);

    close((int)first);
    close((int)accepted);
    close((int)queued);
    close((int)listener);
    return 0;
}

static int run_late_client(unsigned int port) {
    long fd = stream_socket();
    long rc = sys_connect((int)fd, LOOPBACK, port);

    if (rc != 0)
        return fail("late connect", rc);
    close((int)fd);
    return 0;
}

static int run_server(unsigned int port) {
    unsigned char buf[2048];
    long listener = make_listener(port);
    long conn;

    if (listener < 0)
        return fail("serve listener", listener);
    conn = sys_accept((int)listener, 0);
    if (conn < 0)
        return fail("serve accept", conn);
    for (;;) {
        long n = read((int)conn, buf, sizeof(buf));

        if (n < 0)
            return fail("serve read", n);
        if (n == 0)
            break;
        if (write_all((int)conn, buf, (unsigned long)n) != n)
            return fail("serve write", n);
    }
    EXPECT("serve shutdown", sys_shutdown_sock((int)conn, NET_SHUT_WR), 0);
    close((int)conn);
    close((int)listener);
    return 0;
}

static long connect_with_retry(int fd, unsigned int port) {
    long rc = ERR_ECONNREFUSED;

    for (int i = 0; i < CONNECT_TRIES && rc == ERR_ECONNREFUSED; ++i) {
        if (i > 0)
            sleep_ms(CONNECT_WAIT_MS);
        rc = sys_connect(fd, LOOPBACK, port);
    }
    return rc;
}

/* Sends 64 KiB to an echo server in another process while reading the echo
 * back. Either side blocks when its 8 KiB buffers fill, so the transfer only
 * completes if windows, acknowledgements and wakeups work in both directions. */
static int check_echo_transfer(void) {
    static unsigned char buf[CHUNK_SIZE];
    char text[8];
    const char *argv[] = { "tcptest", "serve", text, 0 };
    unsigned long sent = 0;
    unsigned long received = 0;
    int shut = 0;
    long client = stream_socket();
    long pid;
    int status = -1;
    unsigned long deadline;

    snprintf(text, sizeof(text), "%u", ECHO_PORT);
    pid = spawn_args("/tcptest", argv, 0, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL,
                     SPAWN_STDIO_INHERIT);
    if (pid < 0)
        return fail("spawn server", pid);
    EXPECT("connect to server", connect_with_retry((int)client, ECHO_PORT), 0);

    deadline = time_ms() + IO_TIMEOUT_MS;
    while (received < TRANSFER_SIZE) {
        poll_fd_t pfd;
        long ready;

        if (time_ms() > deadline)
            return fail("transfer timeout", (long)received);
        pfd.fd = (int)client;
        pfd.events = POLL_READ | (sent < TRANSFER_SIZE ? POLL_WRITE : 0u);
        pfd.revents = 0;
        ready = poll_many_wait(&pfd, 1, 1000);
        if (ready < 0)
            return fail("poll", ready);

        if (pfd.revents & POLL_WRITE) {
            unsigned long len = TRANSFER_SIZE - sent;
            static unsigned char out[CHUNK_SIZE];
            long n;

            if (len > CHUNK_SIZE)
                len = CHUNK_SIZE;
            for (unsigned long i = 0; i < len; ++i)
                out[i] = pattern_byte(sent + i);
            n = write((int)client, out, len);
            if (n < 0)
                return fail("client write", n);
            sent += (unsigned long)n;
            if (sent == TRANSFER_SIZE && !shut) {
                EXPECT("client shutdown", sys_shutdown_sock((int)client, NET_SHUT_WR), 0);
                shut = 1;
            }
        }
        if (pfd.revents & POLL_READ) {
            long n = read((int)client, buf, sizeof(buf));

            if (n < 0)
                return fail("client read", n);
            if (n == 0)
                break;
            for (long i = 0; i < n; ++i) {
                if (buf[i] != pattern_byte(received + (unsigned long)i))
                    return fail("echo mismatch", (long)(received + (unsigned long)i));
            }
            received += (unsigned long)n;
        }
    }
    EXPECT("bytes sent", sent, TRANSFER_SIZE);
    EXPECT("bytes echoed", received, TRANSFER_SIZE);
    EXPECT("orderly eof", read((int)client, buf, sizeof(buf)), 0);
    EXPECT("close client", close((int)client), 0);
    EXPECT("reap server", wait_pid((int)pid, &status), pid);
    EXPECT("server status", status, 0);
    return 0;
}

static unsigned int parse_uint(const char *s) {
    unsigned int value = 0;

    for (; *s >= '0' && *s <= '9'; ++s)
        value = value * 10 + (unsigned int)(*s - '0');
    return value;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "serve") == 0)
        return run_server(parse_uint(argv[2]));
    if (argc == 3 && strcmp(argv[1], "late") == 0)
        return run_late_client(parse_uint(argv[2]));

    if (check_socket_errors() != 0 || check_handshake_and_close() != 0 ||
        check_reset() != 0 || check_backlog() != 0 || check_echo_transfer() != 0)
        return 1;

    write(1, "TCP PASS\n", 9);
    return 0;
}
