#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ipv4.h"

#define TCP_HDR_LEN     20
#define TCP_MSS         (IPV4_PAYLOAD_MAX - TCP_HDR_LEN)
#define TCP_BUFFER_SIZE 8192u
#define TCP_BACKLOG_MAX 8

/* Bits reported by tcp_poll(). */
#define TCP_POLL_READ  (1u << 0)
#define TCP_POLL_WRITE (1u << 1)
#define TCP_POLL_HUP   (1u << 2)

/* tcp_shutdown() directions. */
#define TCP_SHUT_READ  (1u << 0)
#define TCP_SHUT_WRITE (1u << 1)

/* One connection control block. Blocks live in a fixed table and outlive the
 * socket handle that created them while the close handshake or TIME_WAIT is
 * still running. */
typedef struct tcp_conn tcp_conn_t;

/* Validates a received segment (length, checksum, addresses) and runs it
 * through the state machine of the matching connection. A segment for no
 * connection is answered with a reset. */
void tcp_input(uint32_t src, uint32_t dst, const uint8_t *segment, size_t len);

/* Drives retransmission, zero-window probing, TIME_WAIT and orphan expiry, and
 * polls the NIC while connections exist. Cheap to call on every scheduler
 * pass. */
void tcp_tick(void);

/* True when a local port is taken by a connection block; sockets only track
 * the ports of live handles. addr 0 matches every address. */
int tcp_port_in_use(uint32_t addr, uint16_t port);

/* Active open. *slot is the socket's block: NULL, or a CLOSED block left by an
 * earlier failed attempt. The SYN is sent before returning; completion is
 * reported by tcp_connect_status(). local_addr 0 selects the address facing
 * remote. Returns 0 (also while a connect is already running), -EISCONN,
 * -EINVAL (listening socket, bad destination), -EADDRNOTAVAIL, -EADDRINUSE,
 * -ENOMEM (no free block), -ENETDOWN or -ENETUNREACH. */
int tcp_connect(tcp_conn_t **slot, uint32_t local_addr, uint16_t local_port,
                uint32_t remote, uint16_t remote_port);

/* 0 once established, -EAGAIN while the handshake runs, otherwise the failure
 * (-ECONNREFUSED, -ETIMEDOUT, -ECONNRESET), which is reported once. */
int tcp_connect_status(tcp_conn_t *conn);

/* Passive open on a bound socket. A second call on a listener only changes the
 * backlog. Returns 0, -EINVAL (connection already exists) or -ENOMEM. */
int tcp_listen(tcp_conn_t **slot, uint32_t local_addr, uint16_t local_port,
               int backlog);

/* 0 when an established connection waits in the accept queue, -EAGAIN when
 * not, -EINVAL when conn does not listen. */
int tcp_accept_ready(const tcp_conn_t *listener);

/* Removes the oldest queued connection from the accept queue and hands it to
 * the caller, who must attach it to a socket. Only valid after
 * tcp_accept_ready() returned 0. */
tcp_conn_t *tcp_accept(tcp_conn_t *listener, uint32_t *remote_addr,
                       uint16_t *remote_port);

/* Copies up to cap buffered bytes. Returns 0 with the length in *out_len
 * (0 means the peer closed), -EAGAIN while nothing arrived, -ENOTCONN, or the
 * pending connection error (-ECONNRESET, -ETIMEDOUT), reported once. */
int tcp_read(tcp_conn_t *conn, uint8_t *buf, size_t cap, uint32_t *out_len);

/* Queues up to len bytes for transmission. Returns 0 with the accepted length
 * in *out_len, -EAGAIN while the send buffer is full, -EPIPE after shutdown or
 * close, -ENOTCONN, or the pending connection error. */
int tcp_write(tcp_conn_t *conn, const uint8_t *data, size_t len,
              uint32_t *out_len);

/* Half-close: TCP_SHUT_READ discards and refuses incoming data, TCP_SHUT_WRITE
 * sends a FIN once the queued data is out. Returns 0 or -ENOTCONN. */
int tcp_shutdown(tcp_conn_t *conn, unsigned how);

/* Drops the socket's claim on conn (NULL is ignored). An established
 * connection with unread data is reset, otherwise it is closed gracefully in
 * the background; a listener resets its queued connections. */
void tcp_close(tcp_conn_t *conn);

/* TCP_POLL_* bits describing what would not block. */
unsigned tcp_poll(const tcp_conn_t *conn);
