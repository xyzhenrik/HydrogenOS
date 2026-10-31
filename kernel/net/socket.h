#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/waitq.h"

#define NET_AF_INET     2
#define NET_SOCK_STREAM 1
#define NET_SOCK_DGRAM  2
#define NET_IPPROTO_TCP 6
#define NET_IPPROTO_UDP 17

#define NET_MSG_DONTWAIT (1u << 0)

#define NET_PORT_MAX 65535u

/* Shutdown directions of net_socket_shutdown(); SYS_SHUTDOWN_SOCK's `how`. */
#define NET_SHUT_RD   0
#define NET_SHUT_WR   1
#define NET_SHUT_RDWR 2

typedef struct net_socket net_socket_t;

/* What a process blocked on a socket waits for. */
typedef enum net_wait_kind {
    NET_WAIT_RECV = 0, /* a datagram in the queue */
    NET_WAIT_READ,     /* stream data, end of stream or an error */
    NET_WAIT_WRITE,    /* room in the send buffer or an error */
    NET_WAIT_ACCEPT,   /* an established connection in the accept queue */
    NET_WAIT_CONNECT,  /* the handshake finished or failed */
} net_wait_kind_t;

/* Readiness bits of net_socket_poll(). */
#define NET_POLL_READ  (1u << 0)
#define NET_POLL_WRITE (1u << 1)
#define NET_POLL_HUP   (1u << 2)

/* Peer address as seen by SYS_RECVFROM. IPv4 addresses have the first octet in
 * the most significant byte. */
typedef struct net_sockaddr {
    uint32_t ipv4;
    uint16_t port;
    uint16_t reserved;
} net_sockaddr_t;

/* Creates an unbound UDP (SOCK_DGRAM) or TCP (SOCK_STREAM) socket holding one
 * reference. Works without a network device (loopback needs none). Returns 0
 * or -errno (-EAFNOSUPPORT, -EPROTOTYPE, -EPROTONOSUPPORT, -ENFILE when the
 * socket table is full). */
int net_socket_create(int family, int type, int protocol, net_socket_t **out);

/* Handle duplication and close. Acquire fails with -EMFILE on overflow of the
 * reference count; the last release unbinds the port and frees the socket. */
int net_socket_acquire(net_socket_t *socket);
void net_socket_release(net_socket_t *socket);

/* Binds the local address (0 = any local address) and port (0 = ephemeral).
 * UDP and TCP ports are separate spaces. Returns 0 or -EINVAL (already
 * bound), -EADDRNOTAVAIL, -EADDRINUSE. */
int net_socket_bind(net_socket_t *socket, uint32_t ipv4, uint16_t port);

/* Sends one datagram, binding an ephemeral port first when the socket is
 * unbound. Returns 0 once sent, a positive ticket when the datagram waits for
 * neighbour resolution (see udp_send_wait_check()), or -errno (see
 * udp_output(); -EINVAL for a zero port or a non-unicast destination,
 * -EADDRINUSE when no ephemeral port is free, -EOPNOTSUPP on a stream
 * socket). */
int net_socket_sendto(net_socket_t *socket, const uint8_t *data, size_t len,
                      uint32_t ipv4, uint16_t port);

/* Dequeues the oldest datagram into buf, truncating it to cap bytes. 'from'
 * may be NULL. Returns 0 with the copied length in *out_len, -EAGAIN when
 * the receive queue is empty, or -EOPNOTSUPP on a stream socket. */
int net_socket_recvfrom(net_socket_t *socket, uint8_t *buf, size_t cap,
                        net_sockaddr_t *from, uint32_t *out_len);

/* Queues the socket wakes when a datagram arrives or, for a stream socket,
 * when its connection makes progress. A waiter registers before it blocks and
 * re-checks the socket when woken. */
WaitQueue *net_socket_recv_waiters(net_socket_t *socket);
WaitQueue *net_socket_poll_waiters(net_socket_t *socket);

/* 1 when some process is blocked on a socket: the NIC then has to be polled,
 * because there is no interrupt-driven receive path. */
int net_socket_has_waiters(void);

/* Wakes the waiters of every stream socket after a connection changed state or
 * moved data. A connection block has no back pointer to its socket, so every
 * stream socket re-checks; the woken waiters block again when it was not theirs. */
void net_socket_signal_event(void);

/* Stream sockets. Operations that cannot proceed yet return -EAGAIN; the
 * caller waits on the socket's queues and repeats the call. Datagram sockets return -EOPNOTSUPP. */
int net_socket_is_stream(const net_socket_t *socket);

/* Starts the handshake with ipv4:port, binding an ephemeral port first when
 * the socket is unbound. Returns 0 once the SYN is out (also while a connect
 * is already running) or -errno; completion is reported by
 * net_socket_connect_status(): 0, -EAGAIN while pending, or -ECONNREFUSED,
 * -ETIMEDOUT, -ECONNRESET. */
int net_socket_connect(net_socket_t *socket, uint32_t ipv4, uint16_t port);
int net_socket_connect_status(net_socket_t *socket);

/* Turns a stream socket into a listener, binding an ephemeral port first when
 * it is unbound. Returns 0 or -errno (-EINVAL when already connected). */
int net_socket_listen(net_socket_t *socket, int backlog);

/* net_socket_accept_ready(): 0 when a connection waits, else -EAGAIN or
 * -EINVAL (not listening). net_socket_accept() then moves it onto `child`, a
 * fresh stream socket; `peer` may be NULL. */
int net_socket_accept_ready(const net_socket_t *listener);
void net_socket_accept(net_socket_t *listener, net_socket_t *child,
                       net_sockaddr_t *peer);

/* Stream I/O; see tcp_read() and tcp_write() for the result codes. */
int net_socket_stream_read(net_socket_t *socket, uint8_t *buf, size_t cap,
                           uint32_t *out_len);
int net_socket_stream_write(net_socket_t *socket, const uint8_t *data, size_t len,
                            uint32_t *out_len);

/* how is NET_SHUT_RD, NET_SHUT_WR or NET_SHUT_RDWR. */
int net_socket_shutdown(net_socket_t *socket, int how);

/* NET_POLL_* bits of what would not block: a datagram socket is readable with
 * a queued datagram and always writable. Polls the NIC when nothing is ready
 * yet. */
unsigned net_socket_poll(const net_socket_t *socket);

/* UDP demultiplexer entry point: queues the payload on the socket bound to
 * dst:dst_port. Dropped when nobody listens or the receive queue is full. */
void net_socket_deliver_udp(uint32_t src, uint16_t src_port, uint32_t dst,
                            uint16_t dst_port, const uint8_t *data, size_t len);
