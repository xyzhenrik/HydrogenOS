#include "socket.h"

#include "errno.h"
#include "net_bytes.h"
#include "netif.h"
#include "tcp.h"
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
    uint8_t type;  /* NET_SOCK_DGRAM or NET_SOCK_STREAM */
    uint8_t bound;
    uint16_t local_port;
    uint32_t local_addr; /* 0 = any */
    tcp_conn_t *tcp; /* stream sockets: set by connect() or listen() */
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

/* UDP and TCP ports are separate spaces. A TCP port also stays taken while a
 * connection block that outlived its socket (TIME_WAIT, closing) uses it. */
static int net_port_in_use(uint8_t type, uint32_t addr, uint16_t port) {
    for (int i = 0; i < NET_SOCKET_MAX; ++i) {
        const net_socket_t *other = &net_sockets[i];
        if (other->refs != 0 && other->type == type && other->bound &&
            other->local_port == port &&
            (other->local_addr == 0 || addr == 0 || other->local_addr == addr))
            return 1;
    }
    return type == NET_SOCK_STREAM && tcp_port_in_use(addr, port);
}

/* Unpredictable source ports make blind spoofing of replies harder. */
static int net_ephemeral_port(uint8_t type, uint32_t addr, uint16_t *out_port) {
    const uint32_t span = NET_EPHEMERAL_LAST - NET_EPHEMERAL_FIRST + 1;
    uint32_t start = (net_entropy() >> 8) % span;

    for (uint32_t tries = 0; tries < span; ++tries) {
        uint16_t port = (uint16_t)(NET_EPHEMERAL_FIRST + (start + tries) % span);

        if (!net_port_in_use(type, addr, port)) {
            *out_port = port;
            return 0;
        }
    }
    return -EADDRINUSE;
}

int net_socket_create(int family, int type, int protocol, net_socket_t **out) {
    if (family != NET_AF_INET)
        return -EAFNOSUPPORT;
    if (type != NET_SOCK_DGRAM && type != NET_SOCK_STREAM)
        return -EPROTOTYPE;
    if (protocol != 0 &&
        protocol != (type == NET_SOCK_STREAM ? NET_IPPROTO_TCP : NET_IPPROTO_UDP))
        return -EPROTONOSUPPORT;

    for (int i = 0; i < NET_SOCKET_MAX; ++i) {
        if (net_sockets[i].refs == 0) {
            memset(&net_sockets[i], 0, sizeof(net_sockets[i]));
            net_sockets[i].refs = 1;
            net_sockets[i].type = (uint8_t)type;
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
    if (socket->refs == 0)
        return;
    socket->refs--;
    if (socket->refs == 0 && socket->type == NET_SOCK_STREAM)
        tcp_close(socket->tcp);
}

int net_socket_bind(net_socket_t *socket, uint32_t ipv4, uint16_t port) {
    if (socket->bound)
        return -EINVAL;
    if (!net_address_is_local(ipv4))
        return -EADDRNOTAVAIL;

    if (port == 0) {
        int rc = net_ephemeral_port(socket->type, ipv4, &port);
        if (rc != 0)
            return rc;
    } else if (net_port_in_use(socket->type, ipv4, port)) {
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

    if (socket->type != NET_SOCK_DGRAM)
        return -EOPNOTSUPP;
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
    if (socket->type != NET_SOCK_DGRAM)
        return -EOPNOTSUPP;
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

int net_socket_take_rx_event(void) {
    int event = net_rx_event;
    net_rx_event = 0;
    return event;
}

void net_socket_signal_event(void) {
    net_rx_event = 1;
}

void net_socket_deliver_udp(uint32_t src, uint16_t src_port, uint32_t dst,
                            uint16_t dst_port, const uint8_t *data, size_t len) {
    if (len > UDP_PAYLOAD_MAX) {
        net_count_rx_drop();
        return;
    }
    for (int i = 0; i < NET_SOCKET_MAX; ++i) {
        net_socket_t *socket = &net_sockets[i];

        if (socket->refs == 0 || socket->type != NET_SOCK_DGRAM || !socket->bound ||
            socket->local_port != dst_port ||
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

_Static_assert(TCP_POLL_READ == NET_POLL_READ && TCP_POLL_WRITE == NET_POLL_WRITE &&
                   TCP_POLL_HUP == NET_POLL_HUP,
               "stream readiness is reported with the socket layer's bits");

int net_socket_is_stream(const net_socket_t *socket) {
    return socket->type == NET_SOCK_STREAM;
}

static int net_socket_bind_if_needed(net_socket_t *socket) {
    return socket->bound ? 0 : net_socket_bind(socket, 0, 0);
}

int net_socket_connect(net_socket_t *socket, uint32_t ipv4, uint16_t port) {
    int rc;

    if (!net_socket_is_stream(socket))
        return -EOPNOTSUPP;
    rc = net_socket_bind_if_needed(socket);
    if (rc != 0)
        return rc;
    return tcp_connect(&socket->tcp, socket->local_addr, socket->local_port, ipv4,
                       port);
}

int net_socket_connect_status(net_socket_t *socket) {
    if (!socket->tcp)
        return -ENOTCONN;
    return tcp_connect_status(socket->tcp);
}

int net_socket_listen(net_socket_t *socket, int backlog) {
    int rc;

    if (!net_socket_is_stream(socket))
        return -EOPNOTSUPP;
    rc = net_socket_bind_if_needed(socket);
    if (rc != 0)
        return rc;
    return tcp_listen(&socket->tcp, socket->local_addr, socket->local_port, backlog);
}

int net_socket_accept_ready(const net_socket_t *listener) {
    if (!net_socket_is_stream(listener))
        return -EOPNOTSUPP;
    if (!listener->tcp)
        return -EINVAL;
    return tcp_accept_ready(listener->tcp);
}

void net_socket_accept(net_socket_t *listener, net_socket_t *child,
                       net_sockaddr_t *peer) {
    uint32_t addr = 0;
    uint16_t port = 0;

    child->tcp = tcp_accept(listener->tcp, &addr, &port);
    child->bound = 1;
    child->local_addr = listener->local_addr;
    child->local_port = listener->local_port;
    if (peer) {
        peer->ipv4 = addr;
        peer->port = port;
        peer->reserved = 0;
    }
}

int net_socket_stream_read(net_socket_t *socket, uint8_t *buf, size_t cap,
                           uint32_t *out_len) {
    int rc;

    if (!net_socket_is_stream(socket))
        return -EOPNOTSUPP;
    if (!socket->tcp)
        return -ENOTCONN;
    rc = tcp_read(socket->tcp, buf, cap, out_len);
    if (rc == -EAGAIN) {
        net_poll();
        rc = tcp_read(socket->tcp, buf, cap, out_len);
    }
    return rc;
}

int net_socket_stream_write(net_socket_t *socket, const uint8_t *data, size_t len,
                            uint32_t *out_len) {
    if (!net_socket_is_stream(socket))
        return -EOPNOTSUPP;
    if (!socket->tcp)
        return -ENOTCONN;
    return tcp_write(socket->tcp, data, len, out_len);
}

int net_socket_shutdown(net_socket_t *socket, int how) {
    if (!net_socket_is_stream(socket))
        return -EOPNOTSUPP;
    if (how != NET_SHUT_RD && how != NET_SHUT_WR && how != NET_SHUT_RDWR)
        return -EINVAL;
    if (!socket->tcp)
        return -ENOTCONN;
    return tcp_shutdown(socket->tcp,
                        (how != NET_SHUT_WR ? TCP_SHUT_READ : 0u) |
                            (how != NET_SHUT_RD ? TCP_SHUT_WRITE : 0u));
}

unsigned net_socket_poll(const net_socket_t *socket) {
    unsigned ready;

    if (!net_socket_is_stream(socket)) {
        if (socket->rx_count == 0)
            net_poll();
        return (socket->rx_count != 0 ? NET_POLL_READ : 0u) | NET_POLL_WRITE;
    }
    if (!socket->tcp)
        return NET_POLL_WRITE | NET_POLL_HUP;
    ready = tcp_poll(socket->tcp);
    if (ready == 0) {
        net_poll();
        ready = tcp_poll(socket->tcp);
    }
    return ready;
}

int net_socket_wait_ready(const net_socket_t *socket, net_wait_kind_t kind) {
    unsigned ready;

    if (kind == NET_WAIT_RECV)
        return socket->rx_count != 0;
    if (!socket->tcp)
        return 1;

    ready = tcp_poll(socket->tcp);
    switch (kind) {
    case NET_WAIT_READ:
    case NET_WAIT_ACCEPT:
        return (ready & NET_POLL_READ) != 0;
    case NET_WAIT_WRITE:
        return (ready & NET_POLL_WRITE) != 0;
    default:
        /* A connection that is still opening reports nothing at all. */
        return ready != 0;
    }
}
