#include "tcp.h"

#include "core/time.h"
#include "errno.h"
#include "net.h"
#include "net_bytes.h"
#include "netif.h"
#include "socket.h"

#include <stddef.h>
#include <string.h>

/*
 * Minimal TCP (RFC 793 state machine, RFC 6298 retransmission timer).
 *
 * Deliberately left out: congestion control (the 8 KiB send buffer bounds the
 * data in flight), out-of-order reassembly (a gap drops the segment and the
 * sender's timer repairs it), window scaling, selective acknowledgements and
 * urgent data.
 *
 * The stack runs with interrupts disabled on one CPU and needs no locking.
 * Segments to this host never touch the NIC; they are queued and delivered
 * iteratively by tcp_loopback_drain(), which keeps a long exchange of
 * segments and acknowledgements from recursing on the kernel stack.
 */

#define TCP_CONN_MAX        24
#define TCP_LOOPBACK_DEPTH  16

#define TCP_RTO_INITIAL_MS  300u
#define TCP_RTO_MIN_MS      200u
#define TCP_RTO_MAX_MS      8000u
#define TCP_SYN_RETRIES     5u
#define TCP_DATA_RETRIES    6u
#define TCP_TIME_WAIT_MS    1000u
/* An orphaned connection still waiting for its peer is dropped after this. */
#define TCP_ORPHAN_LIFE_MS  30000u
#define TCP_MSS_DEFAULT     536u
#define TCP_MSS_FLOOR       64u

#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u

#define TCP_OPT_END 0
#define TCP_OPT_NOP 1
#define TCP_OPT_MSS 2

typedef enum tcp_state {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
} tcp_state_t;

enum {
    TCP_F_ORPHAN = 1u << 0,      /* no socket handle refers to the block */
    TCP_F_EMBRYO = 1u << 1,      /* spawned by a listener, not yet accepted */
    TCP_F_FIN_PENDING = 1u << 2, /* write side shut down; FIN follows the data */
    TCP_F_PEER_FIN = 1u << 3,    /* the peer's FIN was received */
    TCP_F_READ_SHUT = 1u << 4,   /* incoming data is discarded */
    TCP_F_ACK_NOW = 1u << 5,
    TCP_F_RTT_TIMING = 1u << 6,
};

struct tcp_conn {
    uint8_t used;
    uint8_t state;
    uint8_t flags;
    uint8_t retries;
    uint8_t backlog;
    int error; /* errno of a failed connection, reported once */
    uint32_t local_addr; /* 0 = any (listeners only) */
    uint32_t remote_addr;
    uint16_t local_port;
    uint16_t remote_port;
    uint16_t mss;
    uint32_t iss;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t snd_max; /* highest sequence number ever sent */
    uint32_t snd_wnd;
    uint32_t snd_wl1;
    uint32_t snd_wl2;
    uint32_t irs;
    uint32_t rcv_nxt;
    uint32_t rcv_adv; /* right edge of the window last advertised */
    uint32_t srtt;
    uint32_t rttvar;
    uint32_t rto;
    uint32_t rtt_seq;
    uint64_t rtt_start_ms;
    uint64_t timer_deadline_ms; /* retransmission or persist timer, 0 = off */
    uint64_t life_deadline_ms;  /* TIME_WAIT and orphan expiry, 0 = off */
    uint32_t accept_order;
    struct tcp_conn *listener;
    uint16_t tx_head;
    uint16_t tx_len;
    uint16_t rx_head;
    uint16_t rx_len;
    /* Cleared by tcp_alloc() up to here; the rings are gated by their lengths. */
    uint8_t tx[TCP_BUFFER_SIZE];
    uint8_t rx[TCP_BUFFER_SIZE];
};

typedef struct tcp_segment {
    uint32_t src;
    uint32_t dst;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    uint16_t window;
    uint16_t mss; /* 0 when the option is absent */
    const uint8_t *data;
    uint32_t len;
} tcp_segment_t;

typedef struct tcp_loop_entry {
    uint32_t src;
    uint32_t dst;
    uint16_t len;
    uint8_t data[IPV4_PAYLOAD_MAX];
} tcp_loop_entry_t;

static tcp_conn_t tcp_conns[TCP_CONN_MAX];
static unsigned tcp_active;
static uint32_t tcp_accept_counter;
static uint8_t tcp_frame[NET_ETH_FRAME_MAX];
static tcp_loop_entry_t tcp_loopback[TCP_LOOPBACK_DEPTH];
static unsigned tcp_loopback_head;
static unsigned tcp_loopback_count;
static int tcp_draining;
static int tcp_ticking;

static int seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

static int seq_le(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) <= 0;
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static void tcp_loopback_drain(void);
static void tcp_output(tcp_conn_t *conn);

static uint16_t tcp_checksum(uint32_t src, uint32_t dst, const uint8_t *segment,
                             size_t len) {
    uint8_t pseudo[12];

    net_put32(pseudo + 0, src);
    net_put32(pseudo + 4, dst);
    pseudo[8] = 0;
    pseudo[9] = IPV4_PROTO_TCP;
    net_put16(pseudo + 10, (uint16_t)len);
    return ipv4_checksum_fold(
        ipv4_checksum_add(ipv4_checksum_add(0, pseudo, sizeof(pseudo)), segment, len));
}

/* Builds a segment and hands it to the wire, or to the loopback queue when dst
 * is this host. Returns 0 or -errno; callers treat a loss like any other, the
 * retransmission timer repairs it. */
static int tcp_send_raw(uint32_t src, uint32_t dst, uint16_t src_port,
                        uint16_t dst_port, uint32_t seq, uint32_t ack,
                        uint8_t flags, uint16_t window, int with_mss,
                        const uint8_t *data, size_t len) {
    size_t header_len = TCP_HDR_LEN + (with_mss ? 4 : 0);
    size_t total = header_len + len;
    int loopback = net_ipv4_is_own(dst);
    uint8_t *segment;
    tcp_loop_entry_t *entry = 0;

    if (loopback) {
        if (tcp_loopback_count == TCP_LOOPBACK_DEPTH)
            return -ENOMEM;
        entry = &tcp_loopback[(tcp_loopback_head + tcp_loopback_count) %
                              TCP_LOOPBACK_DEPTH];
        segment = entry->data;
    } else {
        if (!net_link_up())
            return -ENETDOWN;
        segment = tcp_frame + IPV4_PAYLOAD_OFFSET;
    }

    net_put16(segment + 0, src_port);
    net_put16(segment + 2, dst_port);
    net_put32(segment + 4, seq);
    net_put32(segment + 8, ack);
    segment[12] = (uint8_t)((header_len / 4) << 4);
    segment[13] = flags;
    net_put16(segment + 14, window);
    net_put16(segment + 16, 0);
    net_put16(segment + 18, 0);
    if (with_mss) {
        segment[20] = TCP_OPT_MSS;
        segment[21] = 4;
        net_put16(segment + 22, TCP_MSS);
    }
    if (len > 0)
        memcpy(segment + header_len, data, len);
    net_put16(segment + 16, tcp_checksum(src, dst, segment, total));

    if (loopback) {
        entry->src = src;
        entry->dst = dst;
        entry->len = (uint16_t)total;
        tcp_loopback_count++;
        return 0;
    }
    return ipv4_output(tcp_frame, total, dst, IPV4_PROTO_TCP);
}

static void tcp_reply_reset(const tcp_segment_t *seg) {
    uint32_t seg_len = seg->len + ((seg->flags & TCP_FLAG_SYN) ? 1u : 0u) +
                       ((seg->flags & TCP_FLAG_FIN) ? 1u : 0u);

    if (seg->flags & TCP_FLAG_ACK)
        (void)tcp_send_raw(seg->dst, seg->src, seg->dst_port, seg->src_port,
                           seg->ack, 0, TCP_FLAG_RST, 0, 0, 0, 0);
    else
        (void)tcp_send_raw(seg->dst, seg->src, seg->dst_port, seg->src_port, 0,
                           seg->seq + seg_len, TCP_FLAG_RST | TCP_FLAG_ACK, 0, 0, 0, 0);
}

static tcp_conn_t *tcp_alloc(void) {
    for (int i = 0; i < TCP_CONN_MAX; ++i) {
        tcp_conn_t *conn = &tcp_conns[i];

        if (conn->used)
            continue;
        memset(conn, 0, offsetof(tcp_conn_t, tx));
        conn->used = 1;
        conn->rto = TCP_RTO_INITIAL_MS;
        conn->mss = TCP_MSS_DEFAULT;
        tcp_active++;
        return conn;
    }
    return 0;
}

static void tcp_free(tcp_conn_t *conn) {
    conn->used = 0;
    tcp_active--;
}

/* Receive window that may be advertised. The right edge never moves left, and
 * it only moves right in steps of at least min(MSS, half the buffer) so that a
 * slow reader does not provoke a stream of tiny segments (RFC 1122 4.2.3.3). */
static uint16_t tcp_window_peek(const tcp_conn_t *conn) {
    uint32_t free_space = TCP_BUFFER_SIZE - conn->rx_len;
    uint32_t offered =
        seq_lt(conn->rcv_nxt, conn->rcv_adv) ? conn->rcv_adv - conn->rcv_nxt : 0;
    uint32_t step = min_u32(conn->mss, TCP_BUFFER_SIZE / 2);

    if (free_space > offered && free_space - offered >= step)
        offered = free_space;
    return (uint16_t)offered;
}

static int tcp_emit(tcp_conn_t *conn, uint32_t seq, uint8_t flags,
                    const uint8_t *data, size_t len, int with_mss) {
    uint16_t window = tcp_window_peek(conn);

    conn->rcv_adv = conn->rcv_nxt + window;
    if (flags & TCP_FLAG_ACK)
        conn->flags &= (uint8_t)~TCP_F_ACK_NOW;
    return tcp_send_raw(conn->local_addr, conn->remote_addr, conn->local_port,
                        conn->remote_port, seq,
                        (flags & TCP_FLAG_ACK) ? conn->rcv_nxt : 0, flags, window,
                        with_mss, data, len);
}

static void tcp_note_sent(tcp_conn_t *conn) {
    if (seq_lt(conn->snd_max, conn->snd_nxt))
        conn->snd_max = conn->snd_nxt;
}

/* (Re)sends the SYN or SYN+ACK that opens the connection. */
static int tcp_send_syn(tcp_conn_t *conn) {
    uint8_t flags = TCP_FLAG_SYN;
    int rc;

    if (conn->state == TCP_SYN_RECEIVED)
        flags |= TCP_FLAG_ACK;
    rc = tcp_emit(conn, conn->iss, flags, 0, 0, 1);
    conn->snd_nxt = conn->iss + 1;
    tcp_note_sent(conn);
    conn->timer_deadline_ms = time_ms() + conn->rto;
    return rc;
}

static void tcp_rtt_sample(tcp_conn_t *conn, uint32_t sample) {
    uint32_t rto;

    if (sample == 0)
        sample = 1;
    if (conn->srtt == 0) {
        conn->srtt = sample;
        conn->rttvar = sample / 2;
    } else {
        uint32_t delta = conn->srtt > sample ? conn->srtt - sample : sample - conn->srtt;

        conn->rttvar = (3 * conn->rttvar + delta) / 4;
        conn->srtt = (7 * conn->srtt + sample) / 8;
    }
    rto = conn->srtt + 4 * conn->rttvar;
    conn->rto = rto < TCP_RTO_MIN_MS ? TCP_RTO_MIN_MS
                                     : (rto > TCP_RTO_MAX_MS ? TCP_RTO_MAX_MS : rto);
}

/* Karn's algorithm: only segments that were never retransmitted are timed. */
static void tcp_rtt_ack(tcp_conn_t *conn, uint32_t ack) {
    if ((conn->flags & TCP_F_RTT_TIMING) && seq_le(conn->rtt_seq, ack)) {
        conn->flags &= (uint8_t)~TCP_F_RTT_TIMING;
        tcp_rtt_sample(conn, (uint32_t)(time_ms() - conn->rtt_start_ms));
    }
}

static void tcp_backoff(tcp_conn_t *conn) {
    conn->rto = conn->rto * 2 > TCP_RTO_MAX_MS ? TCP_RTO_MAX_MS : conn->rto * 2;
}

/* The connection is over without an error: the block returns to the table
 * unless a socket still needs it to drain buffered data and read EOF. */
static void tcp_finish(tcp_conn_t *conn) {
    conn->state = TCP_CLOSED;
    conn->timer_deadline_ms = 0;
    conn->life_deadline_ms = 0;
    net_socket_signal_event();
    if (conn->flags & (TCP_F_ORPHAN | TCP_F_EMBRYO))
        tcp_free(conn);
}

static void tcp_fail(tcp_conn_t *conn, int error) {
    conn->error = error;
    conn->rx_len = 0;
    conn->tx_len = 0;
    tcp_finish(conn);
}

/* Resets the peer and drops the connection. */
static void tcp_abort(tcp_conn_t *conn, int error) {
    if (conn->state != TCP_SYN_SENT && conn->state != TCP_CLOSED)
        (void)tcp_send_raw(conn->local_addr, conn->remote_addr, conn->local_port,
                           conn->remote_port, conn->snd_nxt, conn->rcv_nxt,
                           TCP_FLAG_RST | TCP_FLAG_ACK, 0, 0, 0, 0);
    tcp_fail(conn, error);
}

static void tcp_enter_time_wait(tcp_conn_t *conn) {
    conn->state = TCP_TIME_WAIT;
    conn->timer_deadline_ms = 0;
    conn->life_deadline_ms = time_ms() + TCP_TIME_WAIT_MS;
}

static int tcp_data_state(const tcp_conn_t *conn) {
    switch (conn->state) {
    case TCP_ESTABLISHED:
    case TCP_FIN_WAIT_1:
    case TCP_CLOSE_WAIT:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
        return 1;
    default:
        return 0;
    }
}

/* Transmits what the peer's window allows, then the FIN once the queued data
 * is out, then a bare ACK if one is still owed, and arms the timers. */
static void tcp_output(tcp_conn_t *conn) {
    uint64_t now = time_ms();

    if (conn->state == TCP_SYN_RECEIVED) {
        if (conn->flags & TCP_F_ACK_NOW)
            (void)tcp_send_syn(conn);
        return;
    }
    if (conn->state == TCP_FIN_WAIT_2 || conn->state == TCP_TIME_WAIT) {
        if (conn->flags & TCP_F_ACK_NOW)
            (void)tcp_emit(conn, conn->snd_nxt, TCP_FLAG_ACK, 0, 0, 0);
        return;
    }
    if (!tcp_data_state(conn))
        return;

    /* Data sent beyond a window that has closed is discarded by the peer. It is
     * sent again once the window opens; until then snd_nxt must stay where the
     * peer expects it, or our own acknowledgements would be rejected too. */
    if (conn->snd_wnd == 0 && conn->tx_len > 0)
        conn->snd_nxt = conn->snd_una;

    for (;;) {
        uint32_t in_flight = conn->snd_nxt - conn->snd_una;
        uint32_t unsent;
        uint32_t len;
        uint32_t offset;
        uint8_t flags = TCP_FLAG_ACK;

        if (in_flight >= conn->tx_len || conn->snd_wnd <= in_flight)
            break;
        unsent = conn->tx_len - in_flight;
        len = min_u32(min_u32(unsent, conn->snd_wnd - in_flight), conn->mss);
        /* Sender-side silly window avoidance: a window-limited fragment waits
         * for an acknowledgement unless nothing else is in flight. */
        if (len < unsent && len < conn->mss && in_flight > 0)
            break;
        if (len == unsent)
            flags |= TCP_FLAG_PSH;
        offset = (conn->tx_head + in_flight) % TCP_BUFFER_SIZE;
        len = min_u32(len, TCP_BUFFER_SIZE - offset);
        (void)tcp_emit(conn, conn->snd_nxt, flags, conn->tx + offset, len, 0);
        if (!(conn->flags & TCP_F_RTT_TIMING)) {
            conn->flags |= TCP_F_RTT_TIMING;
            conn->rtt_seq = conn->snd_nxt + len;
            conn->rtt_start_ms = now;
        }
        conn->snd_nxt += len;
        tcp_note_sent(conn);
    }

    if ((conn->flags & TCP_F_FIN_PENDING) &&
        conn->snd_nxt == conn->snd_una + conn->tx_len) {
        (void)tcp_emit(conn, conn->snd_nxt, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0, 0);
        conn->snd_nxt++;
        tcp_note_sent(conn);
        if (conn->state == TCP_ESTABLISHED)
            conn->state = TCP_FIN_WAIT_1;
        else if (conn->state == TCP_CLOSE_WAIT)
            conn->state = TCP_LAST_ACK;
    }

    if (conn->flags & TCP_F_ACK_NOW)
        (void)tcp_emit(conn, conn->snd_nxt, TCP_FLAG_ACK, 0, 0, 0);

    /* Either something is unacknowledged or queued data waits for the peer to
     * open a zero window; in both cases a timer has to be running. */
    if (conn->snd_una != conn->snd_nxt ||
        (conn->tx_len > 0 && conn->snd_wnd == 0)) {
        if (conn->timer_deadline_ms == 0)
            conn->timer_deadline_ms = now + conn->rto;
    } else {
        conn->timer_deadline_ms = 0;
    }
}

static void tcp_timeout(tcp_conn_t *conn) {
    uint64_t now = time_ms();

    conn->flags &= (uint8_t)~TCP_F_RTT_TIMING;
    if (conn->state == TCP_SYN_SENT || conn->state == TCP_SYN_RECEIVED) {
        if (++conn->retries > TCP_SYN_RETRIES) {
            tcp_fail(conn, ETIMEDOUT);
            return;
        }
        tcp_backoff(conn);
        (void)tcp_send_syn(conn);
        return;
    }
    if (!tcp_data_state(conn)) {
        conn->timer_deadline_ms = 0;
        return;
    }

    /* A closed window is probed for as long as the peer keeps answering; that
     * is not a failure of the connection. The probe is an empty segment just
     * left of the peer's window: it takes no sequence space, is never
     * acceptable, and so always draws an acknowledgement with the current
     * window. A probe that carried a byte would move snd_nxt ahead of the
     * peer's rcv_nxt, and a peer with a closed window discards every segment
     * but the exact next one, including our pure acknowledgements. */
    if (conn->snd_wnd == 0 && conn->tx_len > 0) {
        tcp_backoff(conn);
        (void)tcp_emit(conn, conn->snd_una - 1, TCP_FLAG_ACK, 0, 0, 0);
        conn->timer_deadline_ms = now + conn->rto;
        return;
    }
    if (conn->snd_una == conn->snd_nxt) {
        conn->timer_deadline_ms = 0;
        return;
    }
    if (++conn->retries > TCP_DATA_RETRIES) {
        tcp_abort(conn, ETIMEDOUT);
        return;
    }
    tcp_backoff(conn);
    /* Go-back-N: everything unacknowledged, including a FIN, goes out again. */
    conn->snd_nxt = conn->snd_una;
    conn->timer_deadline_ms = now + conn->rto;
    tcp_output(conn);
}

static void tcp_ack_advance(tcp_conn_t *conn, uint32_t ack) {
    uint32_t fin_seq = conn->snd_una + conn->tx_len;
    uint32_t acked = ack - conn->snd_una;
    int fin_acked = 0;

    if ((conn->flags & TCP_F_FIN_PENDING) && seq_lt(fin_seq, ack)) {
        fin_acked = 1;
        acked--;
    }
    if (acked > conn->tx_len)
        acked = conn->tx_len;
    conn->tx_head = (uint16_t)((conn->tx_head + acked) % TCP_BUFFER_SIZE);
    conn->tx_len = (uint16_t)(conn->tx_len - acked);
    conn->snd_una = ack;
    if (seq_lt(conn->snd_nxt, ack))
        conn->snd_nxt = ack;
    conn->retries = 0;
    tcp_rtt_ack(conn, ack);
    conn->timer_deadline_ms = conn->snd_una == conn->snd_nxt ? 0 : time_ms() + conn->rto;
    if (acked > 0)
        net_socket_signal_event();

    if (!fin_acked)
        return;
    switch (conn->state) {
    case TCP_FIN_WAIT_1:
        conn->state = TCP_FIN_WAIT_2;
        break;
    case TCP_CLOSING:
        tcp_enter_time_wait(conn);
        break;
    case TCP_LAST_ACK:
        tcp_finish(conn);
        break;
    default:
        break;
    }
}

static void tcp_ring_put(tcp_conn_t *conn, const uint8_t *data, uint32_t len) {
    uint32_t tail = (conn->rx_head + conn->rx_len) % TCP_BUFFER_SIZE;
    uint32_t first = min_u32(len, TCP_BUFFER_SIZE - tail);

    memcpy(conn->rx + tail, data, first);
    memcpy(conn->rx, data + first, len - first);
    conn->rx_len = (uint16_t)(conn->rx_len + len);
}

static int tcp_in_window(const tcp_conn_t *conn, uint32_t window, uint32_t seq) {
    return seq_le(conn->rcv_nxt, seq) && seq_lt(seq, conn->rcv_nxt + window);
}

static void tcp_syn_sent_input(tcp_conn_t *conn, const tcp_segment_t *seg) {
    int ack_ok = 0;

    if (seg->flags & TCP_FLAG_ACK) {
        if (!(seq_lt(conn->iss, seg->ack) && seq_le(seg->ack, conn->snd_nxt))) {
            if (!(seg->flags & TCP_FLAG_RST))
                tcp_reply_reset(seg);
            return;
        }
        ack_ok = 1;
    }
    if (seg->flags & TCP_FLAG_RST) {
        if (ack_ok)
            tcp_fail(conn, ECONNREFUSED);
        return;
    }
    if (!(seg->flags & TCP_FLAG_SYN))
        return;

    conn->irs = seg->seq;
    conn->rcv_nxt = seg->seq + 1;
    conn->rcv_adv = conn->rcv_nxt;
    if (seg->mss != 0)
        conn->mss = (uint16_t)(seg->mss < TCP_MSS_FLOOR ? TCP_MSS_FLOOR
                                                        : min_u32(seg->mss, TCP_MSS));
    conn->snd_wnd = seg->window;
    conn->snd_wl1 = seg->seq;
    conn->snd_wl2 = seg->ack;
    if (ack_ok) {
        conn->snd_una = seg->ack;
        conn->retries = 0;
        conn->timer_deadline_ms = 0;
        tcp_rtt_ack(conn, seg->ack);
        conn->state = TCP_ESTABLISHED;
        conn->flags |= TCP_F_ACK_NOW;
        net_socket_signal_event();
    } else {
        /* Simultaneous open. */
        conn->state = TCP_SYN_RECEIVED;
        conn->flags |= TCP_F_ACK_NOW;
    }
}

static void tcp_reset_input(tcp_conn_t *conn, const tcp_segment_t *seg) {
    /* Both directions are closed in TIME_WAIT. A reset must not destroy the
     * unread data of a connection that ended in good order (RFC 1337), and the
     * peer sends one when it answers a retransmitted acknowledgement after it
     * has already forgotten the connection. */
    if (conn->state == TCP_TIME_WAIT)
        return;
    /* Only an exact match resets (RFC 5961): a blind attacker has to guess the
     * sequence number, a near miss gets a challenge acknowledgement. */
    if (seg->seq != conn->rcv_nxt) {
        conn->flags |= TCP_F_ACK_NOW;
        return;
    }
    switch (conn->state) {
    case TCP_CLOSING:
    case TCP_LAST_ACK:
        tcp_finish(conn);
        break;
    default:
        tcp_fail(conn, conn->state == TCP_SYN_RECEIVED ? ECONNREFUSED : ECONNRESET);
        break;
    }
}

static void tcp_text_input(tcp_conn_t *conn, const tcp_segment_t *seg) {
    uint32_t seq = seg->seq;
    const uint8_t *data = seg->data;
    uint32_t len = seg->len;
    int takes_data = conn->state == TCP_ESTABLISHED || conn->state == TCP_FIN_WAIT_1 ||
                     conn->state == TCP_FIN_WAIT_2;

    if (seq_lt(seq, conn->rcv_nxt)) {
        uint32_t skip = min_u32(conn->rcv_nxt - seq, len);

        data += skip;
        len -= skip;
        seq += skip;
    }

    if (len > 0) {
        if (!takes_data) {
            conn->flags |= TCP_F_ACK_NOW;
        } else if (seq != conn->rcv_nxt) {
            /* A gap: no reassembly, the duplicate ACK asks for a resend. */
            conn->flags |= TCP_F_ACK_NOW;
        } else if (conn->flags & TCP_F_ORPHAN) {
            /* Nobody can read this any more (RFC 1122 4.2.2.13). */
            tcp_abort(conn, ECONNRESET);
            return;
        } else if (conn->flags & TCP_F_READ_SHUT) {
            conn->rcv_nxt += len;
            conn->flags |= TCP_F_ACK_NOW;
        } else {
            uint32_t take = min_u32(len, TCP_BUFFER_SIZE - conn->rx_len);

            tcp_ring_put(conn, data, take);
            conn->rcv_nxt += take;
            conn->flags |= TCP_F_ACK_NOW;
            net_socket_signal_event();
        }
    }

    if ((seg->flags & TCP_FLAG_FIN) && takes_data && seq + len == conn->rcv_nxt) {
        conn->rcv_nxt++;
        conn->flags |= TCP_F_PEER_FIN | TCP_F_ACK_NOW;
        net_socket_signal_event();
        switch (conn->state) {
        case TCP_ESTABLISHED:
            conn->state = TCP_CLOSE_WAIT;
            break;
        case TCP_FIN_WAIT_1:
            conn->state = TCP_CLOSING;
            break;
        default:
            tcp_enter_time_wait(conn);
            break;
        }
    }
}

/* Segment arrives in every state but CLOSED and LISTEN (RFC 793 3.9). */
static void tcp_segment_input(tcp_conn_t *conn, const tcp_segment_t *seg) {
    uint8_t flags = seg->flags;
    uint32_t window = TCP_BUFFER_SIZE - conn->rx_len;
    uint32_t seg_len = seg->len + ((flags & TCP_FLAG_SYN) ? 1u : 0u) +
                       ((flags & TCP_FLAG_FIN) ? 1u : 0u);
    int acceptable;

    if (conn->state == TCP_SYN_SENT) {
        tcp_syn_sent_input(conn, seg);
        return;
    }

    if (seg_len == 0)
        acceptable = window == 0 ? seg->seq == conn->rcv_nxt
                                 : tcp_in_window(conn, window, seg->seq);
    else if (window == 0)
        acceptable = seg->len == 0 && seg->seq == conn->rcv_nxt;
    else
        acceptable = tcp_in_window(conn, window, seg->seq) ||
                     tcp_in_window(conn, window, seg->seq + seg_len - 1);
    if (!acceptable) {
        if (!(flags & TCP_FLAG_RST))
            conn->flags |= TCP_F_ACK_NOW;
        if (conn->state == TCP_TIME_WAIT && (flags & TCP_FLAG_FIN))
            conn->life_deadline_ms = time_ms() + TCP_TIME_WAIT_MS;
        return;
    }

    if (flags & TCP_FLAG_RST) {
        tcp_reset_input(conn, seg);
        return;
    }
    if (flags & TCP_FLAG_SYN) {
        conn->flags |= TCP_F_ACK_NOW;
        return;
    }
    if (!(flags & TCP_FLAG_ACK))
        return;

    if (conn->state == TCP_SYN_RECEIVED) {
        if (!(seq_lt(conn->snd_una, seg->ack) && seq_le(seg->ack, conn->snd_nxt))) {
            tcp_reply_reset(seg);
            return;
        }
        conn->snd_una = seg->ack;
        conn->snd_wnd = seg->window;
        conn->snd_wl1 = seg->seq;
        conn->snd_wl2 = seg->ack;
        conn->retries = 0;
        conn->timer_deadline_ms = 0;
        tcp_rtt_ack(conn, seg->ack);
        conn->state = TCP_ESTABLISHED;
        if (conn->flags & TCP_F_EMBRYO)
            conn->accept_order = ++tcp_accept_counter;
        net_socket_signal_event();
    } else {
        if (seq_lt(conn->snd_max, seg->ack)) {
            conn->flags |= TCP_F_ACK_NOW;
            return;
        }
        if (seq_lt(conn->snd_una, seg->ack))
            tcp_ack_advance(conn, seg->ack);
        if (!conn->used || conn->state == TCP_CLOSED)
            return;
        if (seq_le(conn->snd_una, seg->ack) &&
            (seq_lt(conn->snd_wl1, seg->seq) ||
             (conn->snd_wl1 == seg->seq && seq_le(conn->snd_wl2, seg->ack)))) {
            conn->snd_wnd = seg->window;
            conn->snd_wl1 = seg->seq;
            conn->snd_wl2 = seg->ack;
            net_socket_signal_event();
        }
        /* An answer that keeps the window closed is the reply to a probe; the
         * peer is alive, so the probes must not count towards giving up. */
        if (seg->window == 0)
            conn->retries = 0;
    }

    tcp_text_input(conn, seg);
}

static unsigned tcp_embryo_count(const tcp_conn_t *listener) {
    unsigned count = 0;

    for (int i = 0; i < TCP_CONN_MAX; ++i) {
        const tcp_conn_t *conn = &tcp_conns[i];

        if (conn->used && (conn->flags & TCP_F_EMBRYO) && conn->listener == listener)
            count++;
    }
    return count;
}

static void tcp_listen_input(tcp_conn_t *listener, const tcp_segment_t *seg) {
    tcp_conn_t *child;

    if (seg->flags & TCP_FLAG_RST)
        return;
    if (seg->flags & TCP_FLAG_ACK) {
        tcp_reply_reset(seg);
        return;
    }
    if (!(seg->flags & TCP_FLAG_SYN))
        return;
    /* A full queue drops the SYN; the client retransmits it. */
    if (tcp_embryo_count(listener) >= listener->backlog)
        return;
    child = tcp_alloc();
    if (!child)
        return;

    child->state = TCP_SYN_RECEIVED;
    child->flags = TCP_F_EMBRYO;
    child->listener = listener;
    child->local_addr = seg->dst;
    child->local_port = seg->dst_port;
    child->remote_addr = seg->src;
    child->remote_port = seg->src_port;
    child->irs = seg->seq;
    child->rcv_nxt = seg->seq + 1;
    child->rcv_adv = child->rcv_nxt;
    child->iss = net_entropy() + (uint32_t)time_ms() * 250u;
    child->snd_una = child->iss;
    child->snd_nxt = child->iss;
    child->snd_max = child->iss;
    child->snd_wnd = seg->window;
    child->snd_wl1 = seg->seq;
    child->mss = (uint16_t)(seg->mss == 0 ? TCP_MSS_DEFAULT
                            : (seg->mss < TCP_MSS_FLOOR ? TCP_MSS_FLOOR
                                                        : min_u32(seg->mss, TCP_MSS)));
    child->flags |= TCP_F_RTT_TIMING;
    child->rtt_seq = child->iss + 1;
    child->rtt_start_ms = time_ms();
    (void)tcp_send_syn(child);
}

static tcp_conn_t *tcp_find_connection(uint32_t local, uint16_t local_port,
                                       uint32_t remote, uint16_t remote_port) {
    for (int i = 0; i < TCP_CONN_MAX; ++i) {
        tcp_conn_t *conn = &tcp_conns[i];

        if (conn->used && conn->state != TCP_CLOSED && conn->state != TCP_LISTEN &&
            conn->local_port == local_port && conn->remote_port == remote_port &&
            conn->local_addr == local && conn->remote_addr == remote)
            return conn;
    }
    return 0;
}

static tcp_conn_t *tcp_find_listener(uint32_t local, uint16_t local_port) {
    for (int i = 0; i < TCP_CONN_MAX; ++i) {
        tcp_conn_t *conn = &tcp_conns[i];

        if (conn->used && conn->state == TCP_LISTEN && conn->local_port == local_port &&
            (conn->local_addr == 0 || conn->local_addr == local))
            return conn;
    }
    return 0;
}

static int tcp_parse(uint32_t src, uint32_t dst, const uint8_t *segment, size_t len,
                     tcp_segment_t *seg) {
    size_t header_len;

    if (len < TCP_HDR_LEN)
        return -1;
    header_len = (size_t)(segment[12] >> 4) * 4;
    if (header_len < TCP_HDR_LEN || header_len > len)
        return -1;
    if (tcp_checksum(src, dst, segment, len) != 0)
        return -1;

    seg->src = src;
    seg->dst = dst;
    seg->src_port = net_get16(segment + 0);
    seg->dst_port = net_get16(segment + 2);
    seg->seq = net_get32(segment + 4);
    seg->ack = net_get32(segment + 8);
    seg->flags = segment[13];
    seg->window = net_get16(segment + 14);
    seg->data = segment + header_len;
    seg->len = (uint32_t)(len - header_len);
    seg->mss = 0;

    for (size_t i = TCP_HDR_LEN; i < header_len;) {
        uint8_t kind = segment[i];

        if (kind == TCP_OPT_END)
            break;
        if (kind == TCP_OPT_NOP) {
            i++;
            continue;
        }
        if (i + 1 >= header_len || segment[i + 1] < 2 || i + segment[i + 1] > header_len)
            break;
        if (kind == TCP_OPT_MSS && segment[i + 1] == 4)
            seg->mss = net_get16(segment + i + 2);
        i += segment[i + 1];
    }
    return 0;
}

void tcp_input(uint32_t src, uint32_t dst, const uint8_t *segment, size_t len) {
    tcp_segment_t seg;
    tcp_conn_t *conn;

    if (!net_ipv4_is_own(dst) || !net_ipv4_is_unicast_destination(src))
        return;
    if (tcp_parse(src, dst, segment, len, &seg) != 0) {
        net_count_rx_drop();
        return;
    }

    conn = tcp_find_connection(dst, seg.dst_port, src, seg.src_port);
    /* A new SYN beyond the old connection's sequence space may reuse the
     * four-tuple while it lingers in TIME_WAIT (RFC 1122 4.2.2.13). */
    if (conn && conn->state == TCP_TIME_WAIT && (seg.flags & TCP_FLAG_SYN) &&
        !(seg.flags & TCP_FLAG_ACK) && seq_lt(conn->rcv_nxt, seg.seq)) {
        tcp_finish(conn);
        conn = 0;
    }

    if (conn) {
        tcp_segment_input(conn, &seg);
        if (conn->used)
            tcp_output(conn);
    } else {
        tcp_conn_t *listener = tcp_find_listener(dst, seg.dst_port);

        if (listener)
            tcp_listen_input(listener, &seg);
        else if (!(seg.flags & TCP_FLAG_RST))
            tcp_reply_reset(&seg);
    }
    net_socket_signal_event();
    tcp_loopback_drain();
}

static void tcp_loopback_drain(void) {
    if (tcp_draining)
        return;
    tcp_draining = 1;
    while (tcp_loopback_count > 0) {
        const tcp_loop_entry_t *entry = &tcp_loopback[tcp_loopback_head];

        /* The entry stays queued while it is processed so that segments it
         * provokes append behind it instead of overwriting it. */
        tcp_input(entry->src, entry->dst, entry->data, entry->len);
        tcp_loopback_head = (tcp_loopback_head + 1) % TCP_LOOPBACK_DEPTH;
        tcp_loopback_count--;
    }
    tcp_draining = 0;
}

void tcp_tick(void) {
    static uint64_t last_ms;
    uint64_t now;

    if (tcp_active == 0 || tcp_ticking)
        return;
    now = time_ms();
    if (now == last_ms)
        return;
    last_ms = now;

    tcp_ticking = 1;
    net_poll();
    for (int i = 0; i < TCP_CONN_MAX; ++i) {
        tcp_conn_t *conn = &tcp_conns[i];

        if (!conn->used)
            continue;
        if (conn->life_deadline_ms != 0 && now >= conn->life_deadline_ms) {
            tcp_finish(conn);
            continue;
        }
        if (conn->timer_deadline_ms != 0 && now >= conn->timer_deadline_ms)
            tcp_timeout(conn);
    }
    tcp_loopback_drain();
    tcp_ticking = 0;
}

int tcp_port_in_use(uint32_t addr, uint16_t port) {
    for (int i = 0; i < TCP_CONN_MAX; ++i) {
        const tcp_conn_t *conn = &tcp_conns[i];

        if (conn->used && conn->local_port == port &&
            (conn->local_addr == 0 || addr == 0 || conn->local_addr == addr))
            return 1;
    }
    return 0;
}

int tcp_connect(tcp_conn_t **slot, uint32_t local_addr, uint16_t local_port,
                uint32_t remote, uint16_t remote_port) {
    tcp_conn_t *conn = *slot;
    uint32_t source = local_addr;
    int local_peer;
    int rc;

    if (conn) {
        switch ((tcp_state_t)conn->state) {
        case TCP_SYN_SENT:
        case TCP_SYN_RECEIVED:
            return 0;
        case TCP_LISTEN:
            return -EINVAL;
        case TCP_CLOSED:
            break;
        default:
            return -EISCONN;
        }
    }
    if (remote_port == 0 || !net_ipv4_is_unicast_destination(remote))
        return -EINVAL;

    local_peer = net_ipv4_is_own(remote);
    if (!local_peer && !net_link_up())
        return -ENETDOWN;
    if (source == 0)
        source = local_peer ? remote : net_interface()->ipv4;
    else if (!local_peer && source != net_interface()->ipv4)
        return -EADDRNOTAVAIL;
    if (tcp_find_connection(source, local_port, remote, remote_port))
        return -EADDRINUSE;

    if (!conn) {
        conn = tcp_alloc();
        if (!conn)
            return -ENOMEM;
        *slot = conn;
    } else {
        memset(conn, 0, offsetof(tcp_conn_t, tx));
        conn->used = 1;
        conn->rto = TCP_RTO_INITIAL_MS;
        conn->mss = TCP_MSS_DEFAULT;
    }

    conn->state = TCP_SYN_SENT;
    conn->local_addr = source;
    conn->local_port = local_port;
    conn->remote_addr = remote;
    conn->remote_port = remote_port;
    conn->iss = net_entropy() + (uint32_t)time_ms() * 250u;
    conn->snd_una = conn->iss;
    conn->snd_nxt = conn->iss;
    conn->snd_max = conn->iss;
    conn->flags = TCP_F_RTT_TIMING;
    conn->rtt_seq = conn->iss + 1;
    conn->rtt_start_ms = time_ms();

    rc = tcp_send_syn(conn);
    if (rc == -ENETUNREACH || rc == -ENETDOWN) {
        conn->state = TCP_CLOSED;
        conn->timer_deadline_ms = 0;
        return rc;
    }
    tcp_loopback_drain();
    return 0;
}

int tcp_connect_status(tcp_conn_t *conn) {
    int error;

    if (conn->state == TCP_SYN_SENT || conn->state == TCP_SYN_RECEIVED)
        return -EAGAIN;
    if (conn->state != TCP_CLOSED)
        return 0;
    error = conn->error;
    conn->error = 0;
    return error != 0 ? -error : -ECONNRESET;
}

int tcp_listen(tcp_conn_t **slot, uint32_t local_addr, uint16_t local_port,
               int backlog) {
    tcp_conn_t *conn = *slot;

    if (backlog < 1)
        backlog = 1;
    if (backlog > TCP_BACKLOG_MAX)
        backlog = TCP_BACKLOG_MAX;

    if (conn) {
        if (conn->state != TCP_LISTEN)
            return -EINVAL;
        conn->backlog = (uint8_t)backlog;
        return 0;
    }
    conn = tcp_alloc();
    if (!conn)
        return -ENOMEM;
    conn->state = TCP_LISTEN;
    conn->local_addr = local_addr;
    conn->local_port = local_port;
    conn->backlog = (uint8_t)backlog;
    *slot = conn;
    return 0;
}

/* The oldest embryo of listener that finished its handshake, or NULL. */
static tcp_conn_t *tcp_accept_candidate(const tcp_conn_t *listener) {
    tcp_conn_t *best = 0;

    for (int i = 0; i < TCP_CONN_MAX; ++i) {
        tcp_conn_t *conn = &tcp_conns[i];

        if (!conn->used || !(conn->flags & TCP_F_EMBRYO) || conn->listener != listener ||
            (conn->state != TCP_ESTABLISHED && conn->state != TCP_CLOSE_WAIT))
            continue;
        if (!best || conn->accept_order < best->accept_order)
            best = conn;
    }
    return best;
}

int tcp_accept_ready(const tcp_conn_t *listener) {
    if (listener->state != TCP_LISTEN)
        return -EINVAL;
    return tcp_accept_candidate(listener) ? 0 : -EAGAIN;
}

tcp_conn_t *tcp_accept(tcp_conn_t *listener, uint32_t *remote_addr,
                       uint16_t *remote_port) {
    tcp_conn_t *conn = tcp_accept_candidate(listener);

    conn->flags &= (uint8_t)~TCP_F_EMBRYO;
    conn->listener = 0;
    *remote_addr = conn->remote_addr;
    *remote_port = conn->remote_port;
    return conn;
}

int tcp_read(tcp_conn_t *conn, uint8_t *buf, size_t cap, uint32_t *out_len) {
    uint32_t count;
    uint32_t first;
    uint32_t offered;

    if (conn->state == TCP_LISTEN || conn->state == TCP_SYN_SENT ||
        conn->state == TCP_SYN_RECEIVED)
        return -ENOTCONN;
    if (conn->error != 0) {
        int error = conn->error;

        conn->error = 0;
        return -error;
    }
    if (conn->rx_len == 0) {
        if ((conn->flags & (TCP_F_PEER_FIN | TCP_F_READ_SHUT)) ||
            conn->state == TCP_CLOSED) {
            *out_len = 0;
            return 0;
        }
        return -EAGAIN;
    }

    count = cap < conn->rx_len ? (uint32_t)cap : conn->rx_len;
    first = min_u32(count, TCP_BUFFER_SIZE - conn->rx_head);
    memcpy(buf, conn->rx + conn->rx_head, first);
    memcpy(buf + first, conn->rx, count - first);
    conn->rx_head = (uint16_t)((conn->rx_head + count) % TCP_BUFFER_SIZE);
    conn->rx_len = (uint16_t)(conn->rx_len - count);
    *out_len = count;

    offered = seq_lt(conn->rcv_nxt, conn->rcv_adv) ? conn->rcv_adv - conn->rcv_nxt : 0;
    if (tcp_window_peek(conn) > offered) {
        conn->flags |= TCP_F_ACK_NOW;
        tcp_output(conn);
        tcp_loopback_drain();
    }
    return 0;
}

int tcp_write(tcp_conn_t *conn, const uint8_t *data, size_t len, uint32_t *out_len) {
    uint32_t space;
    uint32_t count;
    uint32_t tail;
    uint32_t first;

    if (conn->state == TCP_LISTEN || conn->state == TCP_SYN_SENT ||
        conn->state == TCP_SYN_RECEIVED)
        return -ENOTCONN;
    if (conn->error != 0) {
        int error = conn->error;

        conn->error = 0;
        return -error;
    }
    if ((conn->flags & TCP_F_FIN_PENDING) ||
        (conn->state != TCP_ESTABLISHED && conn->state != TCP_CLOSE_WAIT))
        return -EPIPE;

    space = TCP_BUFFER_SIZE - conn->tx_len;
    if (space == 0)
        return -EAGAIN;
    count = len < space ? (uint32_t)len : space;
    tail = (conn->tx_head + conn->tx_len) % TCP_BUFFER_SIZE;
    first = min_u32(count, TCP_BUFFER_SIZE - tail);
    memcpy(conn->tx + tail, data, first);
    memcpy(conn->tx, data + first, count - first);
    conn->tx_len = (uint16_t)(conn->tx_len + count);
    *out_len = count;

    tcp_output(conn);
    tcp_loopback_drain();
    return 0;
}

int tcp_shutdown(tcp_conn_t *conn, unsigned how) {
    if (conn->state == TCP_LISTEN || conn->state == TCP_SYN_SENT ||
        conn->state == TCP_SYN_RECEIVED || conn->state == TCP_CLOSED)
        return -ENOTCONN;

    if (how & TCP_SHUT_READ) {
        conn->flags |= TCP_F_READ_SHUT;
        conn->rx_len = 0;
    }
    if (how & TCP_SHUT_WRITE) {
        conn->flags |= TCP_F_FIN_PENDING;
        tcp_output(conn);
    }
    tcp_loopback_drain();
    return 0;
}

void tcp_close(tcp_conn_t *conn) {
    if (!conn)
        return;
    conn->flags |= TCP_F_ORPHAN;

    switch ((tcp_state_t)conn->state) {
    case TCP_LISTEN:
        for (int i = 0; i < TCP_CONN_MAX; ++i) {
            tcp_conn_t *embryo = &tcp_conns[i];

            if (embryo->used && (embryo->flags & TCP_F_EMBRYO) &&
                embryo->listener == conn)
                tcp_abort(embryo, 0);
        }
        tcp_free(conn);
        break;
    case TCP_CLOSED:
    case TCP_SYN_SENT:
        tcp_free(conn);
        break;
    case TCP_SYN_RECEIVED:
        tcp_abort(conn, 0);
        break;
    case TCP_ESTABLISHED:
    case TCP_CLOSE_WAIT:
        if (conn->rx_len > 0) {
            tcp_abort(conn, 0);
            break;
        }
        /* fall through */
    default:
        conn->rx_len = 0;
        conn->flags |= TCP_F_FIN_PENDING;
        if (conn->state != TCP_TIME_WAIT)
            conn->life_deadline_ms = time_ms() + TCP_ORPHAN_LIFE_MS;
        tcp_output(conn);
        break;
    }
    tcp_loopback_drain();
}

unsigned tcp_poll(const tcp_conn_t *conn) {
    unsigned ready = 0;
    int write_possible;

    switch ((tcp_state_t)conn->state) {
    case TCP_LISTEN:
        return tcp_accept_candidate(conn) ? TCP_POLL_READ : 0;
    case TCP_SYN_SENT:
    case TCP_SYN_RECEIVED:
        return 0;
    case TCP_CLOSED:
        return TCP_POLL_READ | TCP_POLL_WRITE | TCP_POLL_HUP;
    default:
        break;
    }

    if (conn->error != 0)
        return TCP_POLL_READ | TCP_POLL_WRITE | TCP_POLL_HUP;
    if (conn->rx_len > 0 || (conn->flags & (TCP_F_PEER_FIN | TCP_F_READ_SHUT)))
        ready |= TCP_POLL_READ;
    /* A write that would fail at once counts as ready: it does not block. */
    write_possible = !(conn->flags & TCP_F_FIN_PENDING) &&
                     (conn->state == TCP_ESTABLISHED || conn->state == TCP_CLOSE_WAIT);
    if (!write_possible || conn->tx_len < TCP_BUFFER_SIZE)
        ready |= TCP_POLL_WRITE;
    if ((conn->flags & TCP_F_PEER_FIN) && (conn->flags & TCP_F_FIN_PENDING))
        ready |= TCP_POLL_HUP;
    return ready;
}
