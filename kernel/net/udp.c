#include "udp.h"

#include "core/time.h"
#include "errno.h"
#include "net.h"
#include "net_bytes.h"
#include "netif.h"
#include "socket.h"

#include <string.h>

#define UDP_ARP_TIMEOUT_MS 1000u

#define UDP_PENDING_MAX    4

/* A datagram waiting for ARP to resolve its next hop. The encoded frame is
 * kept so the transmission can be retried from the scheduler. */
typedef struct udp_pending {
    int ticket; /* 0 = slot free */
    uint32_t dst;
    uint64_t deadline_ms;
    size_t payload_len; /* UDP header + data */
    size_t data_len;
    uint8_t frame[NET_ETH_FRAME_MAX];
} udp_pending_t;

/* Single CPU, interrupts disabled inside the stack: no locking needed. */
static udp_pending_t udp_pending[UDP_PENDING_MAX];
static int udp_next_ticket = 1;

/* RFC 768: the checksum covers an IPv4 pseudo-header plus the datagram. */
static uint16_t udp_checksum(uint32_t src, uint32_t dst, const uint8_t *datagram,
                             size_t len) {
    uint8_t pseudo[12];

    net_put32(pseudo + 0, src);
    net_put32(pseudo + 4, dst);
    pseudo[8] = 0;
    pseudo[9] = IPV4_PROTO_UDP;
    net_put16(pseudo + 10, (uint16_t)len);
    return ipv4_checksum_fold(
        ipv4_checksum_add(ipv4_checksum_add(0, pseudo, sizeof(pseudo)), datagram, len));
}

void udp_input(uint32_t src, uint32_t dst, const uint8_t *datagram, size_t len) {
    if (len < UDP_HDR_LEN) {
        net_count_rx_drop();
        return;
    }

    size_t udp_len = net_get16(datagram + 4);
    /* udp_len, not len: Ethernet pads short frames and IP may carry trailers. */
    if (udp_len < UDP_HDR_LEN || udp_len > len) {
        net_count_rx_drop();
        return;
    }
    /* A zero checksum means the sender did not compute one (RFC 768). */
    if (net_get16(datagram + 6) != 0 &&
        udp_checksum(src, dst, datagram, udp_len) != 0) {
        net_count_rx_drop();
        return;
    }

    net_socket_deliver_udp(src, net_get16(datagram + 0), dst, net_get16(datagram + 2),
                           datagram + UDP_HDR_LEN, udp_len - UDP_HDR_LEN);
}

/* Fills the UDP header and payload at 'datagram' (UDP_HDR_LEN + len bytes). */
static void udp_encode(uint8_t *datagram, uint32_t src, uint32_t dst,
                       uint16_t src_port, uint16_t dst_port,
                       const uint8_t *data, size_t len) {
    uint16_t checksum;

    net_put16(datagram + 0, src_port);
    net_put16(datagram + 2, dst_port);
    net_put16(datagram + 4, (uint16_t)(UDP_HDR_LEN + len));
    net_put16(datagram + 6, 0);
    memcpy(datagram + UDP_HDR_LEN, data, len);
    checksum = udp_checksum(src, dst, datagram, UDP_HDR_LEN + len);
    /* An all-zero result is transmitted as all-ones (RFC 768). */
    net_put16(datagram + 6, checksum == 0 ? 0xFFFFu : checksum);
}

static udp_pending_t *udp_pending_find(int ticket) {
    for (int i = 0; i < UDP_PENDING_MAX; ++i) {
        if (udp_pending[i].ticket == ticket)
            return &udp_pending[i];
    }
    return 0;
}

/* A waiter that died while blocked never collects its ticket. Live waiters
 * finish within a scheduler pass after their deadline, so a slot that is a
 * whole timeout past it is abandoned. */
static udp_pending_t *udp_pending_acquire(void) {
    uint64_t now = time_ms();

    for (int i = 0; i < UDP_PENDING_MAX; ++i) {
        udp_pending_t *slot = &udp_pending[i];

        if (slot->ticket != 0 && now >= slot->deadline_ms + UDP_ARP_TIMEOUT_MS)
            slot->ticket = 0;
        if (slot->ticket == 0)
            return slot;
    }
    return 0;
}

static int udp_output_wire(uint32_t dst, uint16_t src_port, uint16_t dst_port,
                           const uint8_t *data, size_t len) {
    udp_pending_t *slot = udp_pending_acquire();
    int rc;

    if (!net_link_up())
        return -ENETDOWN;
    if (!slot)
        return -ENOMEM;

    udp_encode(slot->frame + IPV4_PAYLOAD_OFFSET, net_interface()->ipv4, dst,
               src_port, dst_port, data, len);
    rc = ipv4_output(slot->frame, UDP_HDR_LEN + len, dst, IPV4_PROTO_UDP);
    if (rc != -EAGAIN)
        return rc;

    /* The first datagram to a neighbour triggers ARP. Park the datagram
     * instead of waiting here: the caller must stay schedulable. */
    slot->dst = dst;
    slot->deadline_ms = time_ms() + UDP_ARP_TIMEOUT_MS;
    slot->payload_len = UDP_HDR_LEN + len;
    slot->data_len = len;
    slot->ticket = udp_next_ticket;
    udp_next_ticket = udp_next_ticket == INT32_MAX ? 1 : udp_next_ticket + 1;
    return slot->ticket;
}

int udp_output(uint32_t dst, uint16_t src_port, uint16_t dst_port,
               const uint8_t *data, size_t len) {
    if (len > UDP_PAYLOAD_MAX)
        return -EMSGSIZE;

    if (net_ipv4_is_own(dst)) {
        uint8_t datagram[UDP_HDR_LEN + UDP_PAYLOAD_MAX];

        /* The receiver sees the destination as the sender, so replies reach us. */
        udp_encode(datagram, dst, dst, src_port, dst_port, data, len);
        udp_input(dst, dst, datagram, UDP_HDR_LEN + len);
        return 0;
    }
    return udp_output_wire(dst, src_port, dst_port, data, len);
}

int udp_send_wait_check(uintptr_t ticket, uint64_t *out_result) {
    udp_pending_t *slot = udp_pending_find((int)ticket);
    int rc;

    if (!slot) {
        *out_result = (uint64_t)-EHOSTUNREACH;
        return 1;
    }

    net_poll();
    rc = ipv4_output(slot->frame, slot->payload_len, slot->dst, IPV4_PROTO_UDP);
    if (rc == -EAGAIN) {
        if (time_ms() < slot->deadline_ms)
            return 0;
        rc = -EHOSTUNREACH;
    }
    *out_result = rc == 0 ? (uint64_t)slot->data_len : (uint64_t)(int64_t)rc;
    slot->ticket = 0;
    return 1;
}

void udp_send_cancel(int ticket) {
    udp_pending_t *slot = udp_pending_find(ticket);

    if (slot)
        slot->ticket = 0;
}
