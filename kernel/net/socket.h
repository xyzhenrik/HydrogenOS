#pragma once

#include <stddef.h>
#include <stdint.h>

#define NET_AF_INET     2
#define NET_SOCK_DGRAM  2
#define NET_IPPROTO_UDP 17

#define NET_MSG_DONTWAIT (1u << 0)

#define NET_PORT_MAX 65535u

typedef struct net_socket net_socket_t;

/* Peer address as seen by SYS_RECVFROM. IPv4 addresses have the first octet in
 * the most significant byte. */
typedef struct net_sockaddr {
    uint32_t ipv4;
    uint16_t port;
    uint16_t reserved;
} net_sockaddr_t;

/* Creates an unbound UDP socket holding one reference. Works without a
 * network device (loopback needs none). Returns 0 or -errno (-EAFNOSUPPORT,
 * -EPROTOTYPE, -EPROTONOSUPPORT, -ENFILE when the socket table is full). */
int net_socket_create(int family, int type, int protocol, net_socket_t **out);

/* Handle duplication and close. Acquire fails with -EMFILE on overflow of the
 * reference count; the last release unbinds the port and frees the socket. */
int net_socket_acquire(net_socket_t *socket);
void net_socket_release(net_socket_t *socket);

/* Binds the local address (0 = any local address) and port (0 = ephemeral).
 * Returns 0 or -EINVAL (already bound), -EADDRNOTAVAIL, -EADDRINUSE. */
int net_socket_bind(net_socket_t *socket, uint32_t ipv4, uint16_t port);

/* Sends one datagram, binding an ephemeral port first when the socket is
 * unbound. Returns 0 once sent, a positive ticket when the datagram waits for
 * neighbour resolution (see udp_send_wait_check()), or -errno (see
 * udp_output(); -EINVAL for a zero port or a non-unicast destination,
 * -EADDRINUSE when no ephemeral port is free). */
int net_socket_sendto(net_socket_t *socket, const uint8_t *data, size_t len,
                      uint32_t ipv4, uint16_t port);

/* Dequeues the oldest datagram into buf, truncating it to cap bytes. 'from'
 * may be NULL. Returns 0 with the copied length in *out_len, or -EAGAIN when
 * the receive queue is empty. */
int net_socket_recvfrom(net_socket_t *socket, uint8_t *buf, size_t cap,
                        net_sockaddr_t *from, uint32_t *out_len);

/* 1 when a datagram is queued. Does not poll the NIC. */
int net_socket_pending(const net_socket_t *socket);

/* Polls the NIC, then reports whether a datagram is queued. */
int net_socket_poll_readable(const net_socket_t *socket);

/* True once since the last call if any socket received a datagram. Lets the
 * scheduler wake poll waiters without inspecting their poll sets. */
int net_socket_take_rx_event(void);

/* UDP demultiplexer entry point: queues the payload on the socket bound to
 * dst:dst_port. Dropped when nobody listens or the receive queue is full. */
void net_socket_deliver_udp(uint32_t src, uint16_t src_port, uint32_t dst,
                            uint16_t dst_port, const uint8_t *data, size_t len);
