#include "socket.h"

#include "errno.h"
#include "net_bytes.h"
#include "netif.h"
#include "udp.h"

#include <string.h>

#define NET_SOCKET_MAX         16
#define NET_SOCKET_RX_DEPTH    8
#define NET_EPHEMERAL_FIRST    49152u
#define NET_EPHEMERAL_LAST     65535u

typedef struct net_datagram {
    uint32_t src;
    uint16_t src_port;
    uint16_t len;
    uint8_t data[UDP_PAYLOAD_MAX];
} net_datagram_t;

struct net_socket {
    uint32_t refs; /* 0 = slot free */
    uint8_t bound;
    uint16_t local_port;
    uint32_t local_addr; /* 0 = any */
    uint8_t rx_head;
    uint8_t rx_count;
    net_datagram_t rx[NET_SOCKET_RX_DEPTH];
};

/* Single CPU, interrupts disabled inside the stack: no locking needed. */
static net_socket_t net_sockets[NET_SOCKET_MAX];
static int net_rx_event;

static int net_address_is_local(uint32_t ipv4) {
    return ipv4 == 0 || net_ipv4_is_own(ipv4);
}

static int net_port_in_use(uint32_t addr, uint16_t port) {
    for (int i = 0; i < NET_SOCKET_MAX; ++i) {
        const net_socket_t *other = &net_sockets[i];
        if (other->refs != 0 && other->bound && other->local_port == port &&
            (other->local_addr == 0 || addr == 0 || other->local_addr == addr))
            return 1;
    }
    return 0;
}

/* Unpredictable source ports make blind spoofing of replies harder. The TSC
 * is the only entropy source the kernel has; a multiplicative hash spreads its
 * fast-changing low bits over the whole range. */
static uint32_t net_port_entropy(void) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (low ^ (high * 0x9E3779B1u)) * 2654435761u;
}

static int net_ephemeral_port(uint32_t addr, uint16_t *out_port) {
    const uint32_t span = NET_EPHEMERAL_LAST - NET_EPHEMERAL_FIRST + 1;
    uint32_t start = (net_port_entropy() >> 8) % span;

    for (uint32_t tries = 0; tries < span; ++tries) {
        uint16_t port = (uint16_t)(NET_EPHEMERAL_FIRST + (start + tries) % span);

        if (!net_port_in_use(addr, port)) {
            *out_port = port;
            return 0;
        }
    }
    return -EADDRINUSE;
}

int net_socket_create(int family, int type, int protocol, net_socket_t **out) {
    if (family != NET_AF_INET)
        return -EAFNOSUPPORT;
    if (type != NET_SOCK_DGRAM)
        return -EPROTOTYPE;
    if (protocol != 0 && protocol != NET_IPPROTO_UDP)
        return -EPROTONOSUPPORT;

    for (int i = 0; i < NET_SOCKET_MAX; ++i) {
        if (net_sockets[i].refs == 0) {
            memset(&net_sockets[i], 0, sizeof(net_sockets[i]));
            net_sockets[i].refs = 1;
            *out = &net_sockets[i];
            return 0;
        }
    }
    return -ENFILE;
}

int net_socket_acquire(net_socket_t *socket) {
    if (socket->refs == 0 || socket->refs == UINT32_MAX)
        return -EMFILE;
    socket->refs++;
    return 0;
}

void net_socket_release(net_socket_t *socket) {
    if (socket->refs > 0)
        socket->refs--;
}

int net_socket_bind(net_socket_t *socket, uint32_t ipv4, uint16_t port) {
    if (socket->bound)
        return -EINVAL;
    if (!net_address_is_local(ipv4))
        return -EADDRNOTAVAIL;

    if (port == 0) {
        int rc = net_ephemeral_port(ipv4, &port);
        if (rc != 0)
            return rc;
    } else if (net_port_in_use(ipv4, port)) {
        return -EADDRINUSE;
    }
    socket->local_addr = ipv4;
    socket->local_port = port;
    socket->bound = 1;
    return 0;
}

int net_socket_sendto(net_socket_t *socket, const uint8_t *data, size_t len,
                      uint32_t ipv4, uint16_t port) {
    int rc;

    if (port == 0 || !net_ipv4_is_unicast_destination(ipv4))
        return -EINVAL;
    if (len > UDP_PAYLOAD_MAX)
        return -EMSGSIZE;
    if (!socket->bound) {
        rc = net_socket_bind(socket, 0, 0);
        if (rc != 0)
            return rc;
    }
    return udp_output(ipv4, socket->local_port, port, data, len);
}

int net_socket_recvfrom(net_socket_t *socket, uint8_t *buf, size_t cap,
                        net_sockaddr_t *from, uint32_t *out_len) {
    if (socket->rx_count == 0)
        net_poll();
    if (socket->rx_count == 0)
        return -EAGAIN;

    const net_datagram_t *datagram = &socket->rx[socket->rx_head];
    size_t copied = datagram->len < cap ? datagram->len : cap;

    memcpy(buf, datagram->data, copied);
    if (from) {
        from->ipv4 = datagram->src;
        from->port = datagram->src_port;
        from->reserved = 0;
    }
    socket->rx_head = (uint8_t)((socket->rx_head + 1) % NET_SOCKET_RX_DEPTH);
    socket->rx_count--;
    *out_len = (uint32_t)copied;
    return 0;
}

int net_socket_pending(const net_socket_t *socket) {
    return socket->rx_count != 0;
}

int net_socket_poll_readable(const net_socket_t *socket) {
    if (socket->rx_count == 0)
        net_poll();
    return socket->rx_count != 0;
}

int net_socket_take_rx_event(void) {
    int event = net_rx_event;
    net_rx_event = 0;
    return event;
}

void net_socket_deliver_udp(uint32_t src, uint16_t src_port, uint32_t dst,
                            uint16_t dst_port, const uint8_t *data, size_t len) {
    if (len > UDP_PAYLOAD_MAX) {
        net_count_rx_drop();
        return;
    }
    for (int i = 0; i < NET_SOCKET_MAX; ++i) {
        net_socket_t *socket = &net_sockets[i];

        if (socket->refs == 0 || !socket->bound || socket->local_port != dst_port ||
            (socket->local_addr != 0 && socket->local_addr != dst))
            continue;
        if (socket->rx_count == NET_SOCKET_RX_DEPTH) {
            net_count_rx_drop();
            return;
        }

        net_datagram_t *slot =
            &socket->rx[(socket->rx_head + socket->rx_count) % NET_SOCKET_RX_DEPTH];
        slot->src = src;
        slot->src_port = src_port;
        slot->len = (uint16_t)len;
        memcpy(slot->data, data, len);
        socket->rx_count++;
        net_rx_event = 1;
        return;
    }
}
