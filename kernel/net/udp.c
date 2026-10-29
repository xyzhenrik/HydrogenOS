#include "udp.h"

#include "core/time.h"
#include "errno.h"
#include "net.h"
#include "net_bytes.h"
#include "netif.h"
#include "socket.h"

#include <string.h>

#define UDP_ARP_TIMEOUT_MS 1000u

#define NET_IPV4_LOOPBACK_NET  NET_IPV4_ADDR(127, 0, 0, 0)
#define NET_IPV4_LOOPBACK_MASK NET_IPV4_ADDR(255, 0, 0, 0)

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

static int udp_is_loopback_destination(uint32_t dst) {
    return dst == net_interface()->ipv4 ||
           (dst & NET_IPV4_LOOPBACK_MASK) == NET_IPV4_LOOPBACK_NET;
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

static int udp_output_wire(uint32_t dst, uint16_t src_port, uint16_t dst_port,
                           const uint8_t *data, size_t len) {
    uint8_t frame[NET_ETH_FRAME_MAX];
    uint64_t deadline;
    int rc;

    if (!net_link_up())
        return -ENETDOWN;

    udp_encode(frame + IPV4_PAYLOAD_OFFSET, net_interface()->ipv4, dst, src_port,
               dst_port, data, len);

    /* The first datagram to a neighbour triggers ARP; wait for the reply
     * instead of dropping the datagram. */
    deadline = time_ms() + UDP_ARP_TIMEOUT_MS;
    for (;;) {
        rc = ipv4_output(frame, UDP_HDR_LEN + len, dst, IPV4_PROTO_UDP);
        if (rc != -EAGAIN)
            return rc;
        if (time_ms() >= deadline)
            return -EHOSTUNREACH;
        __asm__ volatile("sti\n\thlt\n\tcli");
        net_poll();
    }
}

int udp_output(uint32_t dst, uint16_t src_port, uint16_t dst_port,
               const uint8_t *data, size_t len) {
    if (len > UDP_PAYLOAD_MAX)
        return -EMSGSIZE;

    if (udp_is_loopback_destination(dst)) {
        uint8_t datagram[UDP_HDR_LEN + UDP_PAYLOAD_MAX];

        /* The receiver sees the destination as the sender, so replies reach us. */
        udp_encode(datagram, dst, dst, src_port, dst_port, data, len);
        udp_input(dst, dst, datagram, UDP_HDR_LEN + len);
        return 0;
    }
    return udp_output_wire(dst, src_port, dst_port, data, len);
}
