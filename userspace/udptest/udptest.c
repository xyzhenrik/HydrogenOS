#include "dns.h"
#include "mini_libc.h"
#include "syscall.h"

/*
 * udptest - exercises the socket syscalls and the UDP stack.
 *
 * Everything except the last steps runs over loopback and needs no external
 * network, not even a NIC. The wire steps send to the QEMU resolver while a
 * CPU-bound sibling keeps the scheduler busy; a missing upstream (timeout,
 * NXDOMAIN, SERVFAIL) or an absent or isolated network (no NIC, link down,
 * neighbour never answers ARP) is acceptable, a broken send path is not.
 *
 * Helper children: "udptest send <port>" sends one datagram to
 * 127.0.0.1:<port> after a short delay; "udptest spin <ms>" burns CPU for
 * that long.
 */

#define ERR_EBADF           (-9)
#define ERR_EAGAIN          (-11)
#define ERR_ENODEV          (-19)
#define ERR_EFAULT          (-14)
#define ERR_EINVAL          (-22)
#define ERR_ENFILE          (-23)
#define ERR_ENOTSOCK        (-88)
#define ERR_EMSGSIZE        (-90)
#define ERR_EPROTOTYPE      (-91)
#define ERR_EPROTONOSUPPORT (-93)
#define ERR_EAFNOSUPPORT    (-97)
#define ERR_EADDRINUSE      (-98)
#define ERR_EADDRNOTAVAIL   (-99)
#define ERR_ENETDOWN        (-100)
#define ERR_ENETUNREACH     (-101)
#define ERR_EHOSTUNREACH    (-113)

#define LOOPBACK       NET_IPV4_ADDR(127, 0, 0, 1)
#define LOOPBACK_ALT   NET_IPV4_ADDR(127, 0, 0, 2)
#define FOREIGN        NET_IPV4_ADDR(1, 2, 3, 4)
#define SILENT_HOST    NET_IPV4_ADDR(10, 0, 2, 99)
#define BASE_PORT      41000u
#define EPHEMERAL_LOW  49152u
#define PAYLOAD_MAX    1472
#define SOCKET_LIMIT   64
#define RX_QUEUE_DEPTH 8
#define UNMAPPED_PTR   ((void *)0x300000000000UL)
#define KERNEL_IMAGE   ((void *)0x100000UL)

#define SENDER_DELAY_MS   100
#define WAKE_TIMEOUT_MS   5000
#define IDLE_POLL_MS      100
#define DNS_TIMEOUT_MS    2000
#define SPIN_MS           3500

/* The interface address, or 0 without a network device. */
static unsigned int own_address;

static int fail(const char *what, long value) {
    dprintf(2, "udptest: FAIL %s (%ld)\n", what, value);
    return 1;
}

#define EXPECT(what, actual, expected)                    \
    do {                                                  \
        long actual_value = (actual);                     \
        if (actual_value != (long)(expected))             \
            return fail(what, actual_value);              \
    } while (0)

static long udp_socket(void) {
    return sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
}

static long send_text(int fd, unsigned int ip, unsigned int port, const char *text) {
    return sys_sendto(fd, text, strlen(text), ip, port);
}

static int same_bytes(const void *a, const void *b, size_t len) {
    const unsigned char *x = a;
    const unsigned char *y = b;
    for (size_t i = 0; i < len; ++i) {
        if (x[i] != y[i])
            return 0;
    }
    return 1;
}

static int run_sender(unsigned int port) {
    long fd = udp_socket();
    if (fd < 0)
        return 1;
    sleep_ms(SENDER_DELAY_MS);
    return send_text((int)fd, LOOPBACK, port, "wake") == 4 ? 0 : 1;
}

static int run_spinner(unsigned int duration_ms) {
    unsigned long start = time_ms();

    while (time_ms() - start < duration_ms)
        __asm__ volatile("pause");
    return 0;
}

static int check_socket_creation(void) {
    long sockets[SOCKET_LIMIT];
    long rc = 0;
    int count = 0;

    EXPECT("socket bad family", sys_socket(3, NET_SOCK_DGRAM, 0), ERR_EAFNOSUPPORT);
    EXPECT("socket stream", sys_socket(NET_AF_INET, 1, 0), ERR_EPROTOTYPE);
    EXPECT("socket bad protocol", sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 6),
           ERR_EPROTONOSUPPORT);

    /* The kernel socket table is smaller than the handle table. */
    while (count < SOCKET_LIMIT) {
        rc = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
        if (rc < 0)
            break;
        sockets[count++] = rc;
    }
    EXPECT("socket table full", rc, ERR_ENFILE);
    if (count == 0)
        return fail("socket table empty", count);
    for (int i = 0; i < count; ++i)
        EXPECT("close socket", close((int)sockets[i]), 0);
    rc = udp_socket();
    if (rc < 0)
        return fail("socket after release", rc);
    close((int)rc);
    return 0;
}

static int check_bind_rules(void) {
    long a = udp_socket();
    long b = udp_socket();
    long c = udp_socket();
    long copy;

    if (a < 0 || b < 0 || c < 0)
        return fail("socket", a < 0 ? a : (b < 0 ? b : c));

    EXPECT("bind", sys_bind((int)a, 0, BASE_PORT), 0);
    EXPECT("bind twice", sys_bind((int)a, 0, BASE_PORT + 5), ERR_EINVAL);
    EXPECT("bind port in use", sys_bind((int)b, 0, BASE_PORT), ERR_EADDRINUSE);
    EXPECT("bind loopback vs any", sys_bind((int)b, LOOPBACK, BASE_PORT), ERR_EADDRINUSE);
    EXPECT("bind foreign address", sys_bind((int)b, FOREIGN, BASE_PORT + 1),
           ERR_EADDRNOTAVAIL);
    EXPECT("bind port range", sys_bind((int)b, 0, 65536), ERR_EINVAL);
    EXPECT("bind specific address",
           sys_bind((int)b, own_address ? own_address : LOOPBACK_ALT, BASE_PORT + 1), 0);
    EXPECT("bind any vs own", sys_bind((int)c, 0, BASE_PORT + 1), ERR_EADDRINUSE);
    EXPECT("close bound socket", close((int)b), 0);
    EXPECT("bind after close", sys_bind((int)c, 0, BASE_PORT + 1), 0);
    EXPECT("close", close((int)c), 0);

    /* A duplicated handle keeps the port reserved until the last close. */
    copy = dup((int)a);
    if (copy < 0)
        return fail("dup socket", copy);
    EXPECT("close original", close((int)a), 0);
    b = udp_socket();
    if (b < 0)
        return fail("socket", b);
    EXPECT("port held by duplicate", sys_bind((int)b, 0, BASE_PORT), ERR_EADDRINUSE);
    EXPECT("close duplicate", close((int)copy), 0);
    EXPECT("port released", sys_bind((int)b, 0, BASE_PORT), 0);
    return close((int)b) == 0 ? 0 : fail("close", 0);
}

static int check_loopback(void) {
    const unsigned int destinations[] = { LOOPBACK, own_address };
    const unsigned int destination_count = own_address ? 2 : 1;
    char buf[PAYLOAD_MAX + 8];
    unsigned char pattern[PAYLOAD_MAX];
    net_sockaddr_t from;
    unsigned int first_port = 0;
    long rx = udp_socket();
    long tx = udp_socket();
    long tx2 = udp_socket();

    if (rx < 0 || tx < 0 || tx2 < 0)
        return fail("socket", rx);
    EXPECT("bind", sys_bind((int)rx, 0, BASE_PORT), 0);

    for (unsigned int i = 0; i < destination_count; ++i) {
        EXPECT("send", send_text((int)tx, destinations[i], BASE_PORT, "hello"), 5);
        EXPECT("poll readable", sys_poll_wait((int)rx, POLL_READ, WAKE_TIMEOUT_MS),
               POLL_READ);
        EXPECT("recv", sys_recvfrom((int)rx, buf, sizeof(buf), 0, &from), 5);
        if (!same_bytes(buf, "hello", 5))
            return fail("payload", 0);
        EXPECT("source address", from.ipv4, destinations[i]);
        if (from.port < EPHEMERAL_LOW)
            return fail("ephemeral source port", from.port);
        if (i == 0)
            first_port = from.port;
        else
            EXPECT("source port is stable", from.port, first_port);
    }

    EXPECT("send other", send_text((int)tx2, LOOPBACK, BASE_PORT, "x"), 1);
    EXPECT("recv other", sys_recvfrom((int)rx, buf, sizeof(buf), 0, &from), 1);
    if (from.port < EPHEMERAL_LOW || from.port == first_port)
        return fail("distinct ephemeral port", from.port);

    /* Odd and maximum length, truncation and the empty datagram. */
    for (int i = 0; i < PAYLOAD_MAX; ++i)
        pattern[i] = (unsigned char)(i * 7 + 3);
    EXPECT("send max", sys_sendto((int)tx, pattern, PAYLOAD_MAX, LOOPBACK, BASE_PORT),
           PAYLOAD_MAX);
    EXPECT("recv max", sys_recvfrom((int)rx, buf, sizeof(buf), 0, 0), PAYLOAD_MAX);
    if (!same_bytes(buf, pattern, PAYLOAD_MAX))
        return fail("max payload", 0);
    EXPECT("send odd", sys_sendto((int)tx, pattern, PAYLOAD_MAX - 1, LOOPBACK, BASE_PORT),
           PAYLOAD_MAX - 1);
    EXPECT("recv odd", sys_recvfrom((int)rx, buf, sizeof(buf), 0, 0), PAYLOAD_MAX - 1);
    if (!same_bytes(buf, pattern, PAYLOAD_MAX - 1))
        return fail("odd payload", 0);
    EXPECT("send truncated", sys_sendto((int)tx, pattern, 10, LOOPBACK, BASE_PORT), 10);
    EXPECT("recv truncated", sys_recvfrom((int)rx, buf, 4, 0, 0), 4);
    EXPECT("send empty", sys_sendto((int)tx, 0, 0, LOOPBACK, BASE_PORT), 0);
    EXPECT("recv empty", sys_recvfrom((int)rx, buf, sizeof(buf), 0, &from), 0);
    EXPECT("empty datagram source", from.ipv4, LOOPBACK);

    EXPECT("recv on empty queue",
           sys_recvfrom((int)rx, buf, sizeof(buf), NET_MSG_DONTWAIT, 0), ERR_EAGAIN);
    EXPECT("write is not supported", write((int)rx, "x", 1) < 0, 1);

    close((int)rx);
    close((int)tx);
    close((int)tx2);
    return 0;
}

static int check_queue_bound(void) {
    unsigned char payload;
    unsigned char value;
    long rx = udp_socket();
    long tx = udp_socket();
    int received = 0;

    if (rx < 0 || tx < 0)
        return fail("socket", rx);
    EXPECT("bind", sys_bind((int)rx, 0, BASE_PORT), 0);
    for (int i = 0; i < 20; ++i) {
        payload = (unsigned char)i;
        EXPECT("send burst", sys_sendto((int)tx, &payload, 1, LOOPBACK, BASE_PORT), 1);
    }
    while (sys_recvfrom((int)rx, &value, 1, NET_MSG_DONTWAIT, 0) == 1) {
        if (value != received)
            return fail("queue order", value);
        ++received;
    }
    EXPECT("bounded queue", received, RX_QUEUE_DEPTH);
    close((int)rx);
    close((int)tx);
    return 0;
}

static int check_argument_errors(void) {
    static char big[PAYLOAD_MAX + 1];
    char buf[16];
    net_sockaddr_t from;
    long fd = udp_socket();
    long file = open("/etc/init.rc", OPEN_READ);

    if (fd < 0 || file < 0)
        return fail("setup", fd < 0 ? fd : file);
    EXPECT("send too large", sys_sendto((int)fd, big, sizeof(big), LOOPBACK, BASE_PORT),
           ERR_EMSGSIZE);
    EXPECT("send null buffer", sys_sendto((int)fd, 0, 4, LOOPBACK, BASE_PORT), ERR_EFAULT);
    EXPECT("send port zero", sys_sendto((int)fd, "x", 1, LOOPBACK, 0), ERR_EINVAL);
    EXPECT("send port range", sys_sendto((int)fd, "x", 1, LOOPBACK, 65536), ERR_EINVAL);
    EXPECT("send to any", sys_sendto((int)fd, "x", 1, 0, BASE_PORT), ERR_EINVAL);
    EXPECT("send to broadcast",
           sys_sendto((int)fd, "x", 1, NET_IPV4_ADDR(255, 255, 255, 255), BASE_PORT),
           ERR_EINVAL);
    EXPECT("send to multicast",
           sys_sendto((int)fd, "x", 1, NET_IPV4_ADDR(224, 0, 0, 1), BASE_PORT),
           ERR_EINVAL);
    EXPECT("recv null buffer", sys_recvfrom((int)fd, 0, 4, NET_MSG_DONTWAIT, 0), ERR_EFAULT);
    EXPECT("recv bad from",
           sys_recvfrom((int)fd, buf, sizeof(buf), NET_MSG_DONTWAIT, (net_sockaddr_t *)8),
           ERR_EFAULT);
    EXPECT("send unmapped buffer",
           sys_sendto((int)fd, UNMAPPED_PTR, 4, LOOPBACK, BASE_PORT), ERR_EFAULT);
    EXPECT("recv unmapped buffer",
           sys_recvfrom((int)fd, UNMAPPED_PTR, 4, NET_MSG_DONTWAIT, 0), ERR_EFAULT);
    EXPECT("recv unmapped from",
           sys_recvfrom((int)fd, buf, sizeof(buf), NET_MSG_DONTWAIT, UNMAPPED_PTR),
           ERR_EFAULT);
    EXPECT("recv kernel buffer",
           sys_recvfrom((int)fd, KERNEL_IMAGE, 4, NET_MSG_DONTWAIT, 0), ERR_EFAULT);
    EXPECT("bind", sys_bind((int)fd, 0, BASE_PORT + 2), 0);
    EXPECT("send to self", send_text((int)fd, LOOPBACK, BASE_PORT + 2, "data"), 4);
    EXPECT("recv unmapped buffer with data queued",
           sys_recvfrom((int)fd, UNMAPPED_PTR, 4, NET_MSG_DONTWAIT, 0), ERR_EFAULT);
    EXPECT("datagram survives rejected recv",
           sys_recvfrom((int)fd, buf, sizeof(buf), NET_MSG_DONTWAIT, 0), 4);
    EXPECT("recv unknown flags", sys_recvfrom((int)fd, buf, sizeof(buf), 0x80, &from),
           ERR_EINVAL);
    EXPECT("bind file", sys_bind((int)file, 0, BASE_PORT), ERR_ENOTSOCK);
    EXPECT("sendto file", sys_sendto((int)file, "x", 1, LOOPBACK, BASE_PORT), ERR_ENOTSOCK);
    EXPECT("recv file", sys_recvfrom((int)file, buf, sizeof(buf), NET_MSG_DONTWAIT, 0),
           ERR_ENOTSOCK);
    EXPECT("bind invalid fd", sys_bind(60, 0, BASE_PORT), ERR_EBADF);
    EXPECT("close", close((int)fd), 0);
    EXPECT("bind closed fd", sys_bind((int)fd, 0, BASE_PORT), ERR_EBADF);
    close((int)file);
    return 0;
}

static long spawn_sender(unsigned int port) {
    char text[8];
    const char *argv[] = { "udptest", "send", text, 0 };

    snprintf(text, sizeof(text), "%u", port);
    return spawn_args("/lib/tests/udptest", argv, 0, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL,
                      SPAWN_STDIO_NULL);
}

static int reap(long pid) {
    int status = -1;
    EXPECT("reap sender", wait_pid((int)pid, &status), pid);
    EXPECT("sender status", status, 0);
    return 0;
}

/* The receiver sleeps in the scheduler until another process sends. */
static int check_blocking_receive(void) {
    char buf[16];
    net_sockaddr_t from;
    long rx = udp_socket();
    long pid;

    if (rx < 0)
        return fail("socket", rx);
    EXPECT("bind", sys_bind((int)rx, 0, BASE_PORT), 0);
    pid = spawn_sender(BASE_PORT);
    if (pid < 0)
        return fail("spawn sender", pid);
    EXPECT("blocking recv", sys_recvfrom((int)rx, buf, sizeof(buf), 0, &from), 4);
    if (!same_bytes(buf, "wake", 4))
        return fail("blocking payload", 0);
    EXPECT("blocking source", from.ipv4, LOOPBACK);
    if (reap(pid) != 0)
        return 1;
    close((int)rx);
    return 0;
}

static int check_poll(void) {
    char buf[16];
    poll_fd_t set[2];
    long idle = udp_socket();
    long rx = udp_socket();
    unsigned long start;
    long pid;

    if (idle < 0 || rx < 0)
        return fail("socket", idle);
    EXPECT("bind idle", sys_bind((int)idle, 0, BASE_PORT + 1), 0);
    EXPECT("bind", sys_bind((int)rx, 0, BASE_PORT), 0);

    EXPECT("writable", sys_poll((int)rx, POLL_WRITE), POLL_WRITE);
    start = time_ms();
    EXPECT("poll timeout", sys_poll_wait((int)rx, POLL_READ, IDLE_POLL_MS), 0);
    if (time_ms() - start < IDLE_POLL_MS - 20)
        return fail("poll returned early", (long)(time_ms() - start));

    pid = spawn_sender(BASE_PORT);
    if (pid < 0)
        return fail("spawn sender", pid);
    EXPECT("poll wakes", sys_poll_wait((int)rx, POLL_READ, WAKE_TIMEOUT_MS), POLL_READ);
    EXPECT("recv", sys_recvfrom((int)rx, buf, sizeof(buf), NET_MSG_DONTWAIT, 0), 4);
    if (reap(pid) != 0)
        return 1;

    set[0].fd = (int)idle;
    set[0].events = POLL_READ;
    set[1].fd = (int)rx;
    set[1].events = POLL_READ;
    pid = spawn_sender(BASE_PORT);
    if (pid < 0)
        return fail("spawn sender", pid);
    EXPECT("poll_many wakes", poll_many_wait(set, 2, WAKE_TIMEOUT_MS), 1);
    EXPECT("idle socket revents", set[0].revents, 0);
    EXPECT("active socket revents", set[1].revents, POLL_READ);
    EXPECT("recv", sys_recvfrom((int)rx, buf, sizeof(buf), NET_MSG_DONTWAIT, 0), 4);
    if (reap(pid) != 0)
        return 1;
    close((int)idle);
    close((int)rx);
    return 0;
}

static void put16(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

/* Appends an A record (owner = pointer to the question name) to a response. */
static size_t append_answer(unsigned char *msg, size_t len, unsigned int ip) {
    static const unsigned char head[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4 };

    memcpy(msg + len, head, sizeof(head));
    len += sizeof(head);
    msg[len++] = (unsigned char)(ip >> 24);
    msg[len++] = (unsigned char)(ip >> 16);
    msg[len++] = (unsigned char)(ip >> 8);
    msg[len++] = (unsigned char)ip;
    return len;
}

/* Runs the resolver's codec over a loopback "server" in this process. */
static int check_dns_codec(void) {
    static const unsigned char cname[] = {
        0xC0, 0x0C, 0, 5, 0, 1, 0, 0, 0, 60, 0, 2, 0xC0, 0x0C
    };
    unsigned char query[DNS_MESSAGE_MAX];
    unsigned char reply[DNS_MESSAGE_MAX];
    net_sockaddr_t client;
    unsigned int address = 0;
    size_t reply_len;
    long server = udp_socket();
    long resolver = udp_socket();
    long query_len;

    if (server < 0 || resolver < 0)
        return fail("socket", server);
    EXPECT("bind server", sys_bind((int)server, 0, BASE_PORT), 0);

    query_len = dns_build_query(query, sizeof(query), 0x1234, "Example.Org");
    if (query_len <= 0)
        return fail("build query", query_len);
    EXPECT("send query",
           sys_sendto((int)resolver, query, (unsigned long)query_len, LOOPBACK, BASE_PORT),
           query_len);
    EXPECT("server recv", sys_recvfrom((int)server, query, sizeof(query), 0, &client),
           query_len);

    memcpy(reply, query, (size_t)query_len);
    put16(reply + 2, 0x8180);
    put16(reply + 6, 2);
    reply_len = (size_t)query_len;
    memcpy(reply + reply_len, cname, sizeof(cname));
    reply_len += sizeof(cname);
    reply_len = append_answer(reply, reply_len, NET_IPV4_ADDR(93, 184, 216, 34));
    EXPECT("send reply",
           sys_sendto((int)server, reply, reply_len, client.ipv4, client.port),
           reply_len);
    EXPECT("resolver recv", sys_recvfrom((int)resolver, reply, sizeof(reply), 0, 0),
           reply_len);

    EXPECT("parse answer", dns_parse_response(reply, reply_len, 0x1234, "example.org",
                                              &address), 0);
    EXPECT("parsed address", address, NET_IPV4_ADDR(93, 184, 216, 34));
    EXPECT("wrong id", dns_parse_response(reply, reply_len, 0x1235, "example.org",
                                          &address), DNS_ERR_MISMATCH);
    EXPECT("wrong name", dns_parse_response(reply, reply_len, 0x1234, "example.net",
                                           &address), DNS_ERR_MISMATCH);
    EXPECT("truncated answer",
           dns_parse_response(reply, reply_len - 2, 0x1234, "example.org", &address),
           DNS_ERR_MALFORMED);
    put16(reply + 2, 0x8183);
    EXPECT("nxdomain", dns_parse_response(reply, reply_len, 0x1234, "example.org",
                                          &address), DNS_ERR_NXDOMAIN);
    put16(reply + 2, 0x8182);
    EXPECT("servfail", dns_parse_response(reply, reply_len, 0x1234, "example.org",
                                          &address), DNS_ERR_SERVFAIL);
    put16(reply + 2, 0x8180);
    put16(reply + 6, 0);
    EXPECT("no address", dns_parse_response(reply, (size_t)query_len, 0x1234,
                                            "example.org", &address),
           DNS_ERR_NO_ADDRESS);
    EXPECT("invalid name", dns_build_query(query, sizeof(query), 1, "bad name"),
           DNS_ERR_NAME);
    EXPECT("empty label", dns_build_query(query, sizeof(query), 1, "a..b"), DNS_ERR_NAME);

    close((int)server);
    close((int)resolver);
    return 0;
}

/* A network that is absent, down or isolated is not a udptest failure. */
static int network_unavailable(long rc) {
    return rc == ERR_ENODEV || rc == ERR_ENETDOWN || rc == ERR_ENETUNREACH ||
           rc == ERR_EHOSTUNREACH;
}

/*
 * A cold neighbour makes the kernel park the datagram until ARP resolves. A
 * silent host must fail with -EHOSTUNREACH (or -ENETDOWN without link) while
 * the CPU-bound sibling keeps running; the process must neither hang nor
 * take the kernel down.
 */
static int check_silent_neighbour(void) {
    long fd = udp_socket();
    long rc;

    if (fd < 0)
        return fail("socket", fd);
    rc = send_text((int)fd, SILENT_HOST, DNS_PORT, "ping");
    if (rc != 4 && !network_unavailable(rc))
        return fail("send to silent host", rc);
    close((int)fd);
    return 0;
}

/* One real query through ARP, the NIC and the checksum path. */
static int check_dns_wire(void) {
    unsigned int address = 0;
    int rc = dns_resolve_a(DNS_DEFAULT_SERVER, "example.com", DNS_TIMEOUT_MS, &address);

    switch (rc) {
    case 0:
        dprintf(2, "udptest: example.com is %u.%u.%u.%u\n", address >> 24,
                (address >> 16) & 0xFF, (address >> 8) & 0xFF, address & 0xFF);
        return 0;
    case DNS_ERR_TIMEOUT:
    case DNS_ERR_NXDOMAIN:
    case DNS_ERR_SERVFAIL:
    case DNS_ERR_NO_ADDRESS:
    case DNS_ERR_TRUNCATED:
        dprintf(2, "udptest: resolver answered: %s\n", dns_error_text(rc));
        return 0;
    default:
        if (network_unavailable(rc)) {
            dprintf(2, "udptest: no usable network: %s\n", dns_error_text(rc));
            return 0;
        }
        return fail("dns query", rc);
    }
}

static int check_wire_with_busy_sibling(void) {
    char text[8];
    const char *argv[] = { "udptest", "spin", text, 0 };
    long pid;

    snprintf(text, sizeof(text), "%u", SPIN_MS);
    pid = spawn_args("/lib/tests/udptest", argv, 0, SPAWN_STDIO_NULL, SPAWN_STDIO_NULL,
                     SPAWN_STDIO_NULL);
    if (pid < 0)
        return fail("spawn spinner", pid);
    if (check_silent_neighbour() != 0 || check_dns_wire() != 0)
        return 1;
    return reap(pid);
}

static unsigned int parse_uint(const char *s) {
    unsigned int value = 0;
    for (; *s >= '0' && *s <= '9'; ++s)
        value = value * 10 + (unsigned int)(*s - '0');
    return value;
}

int main(int argc, char **argv) {
    if (argc == 3 && strlen(argv[1]) == 4 && same_bytes(argv[1], "send", 4))
        return run_sender(parse_uint(argv[2]));
    if (argc == 3 && strlen(argv[1]) == 4 && same_bytes(argv[1], "spin", 4))
        return run_spinner(parse_uint(argv[2]));

    net_info_t info;
    if (sys_net_info(&info) == 0)
        own_address = info.ipv4;

    if (check_socket_creation() != 0 || check_bind_rules() != 0 ||
        check_loopback() != 0 || check_queue_bound() != 0 ||
        check_argument_errors() != 0 || check_blocking_receive() != 0 ||
        check_poll() != 0 || check_dns_codec() != 0 ||
        check_wire_with_busy_sibling() != 0)
        return 1;

    write(1, "UDP PASS\n", 9);
    return 0;
}
