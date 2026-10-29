#include "ipv4.h"

#include "arp.h"
#include "errno.h"
#include "ethernet.h"
#include "icmp.h"
#include "net_bytes.h"
#include "udp.h"

#define IPV4_VERSION_IHL   0x45
#define IPV4_DEFAULT_TTL   64
#define IPV4_FLAG_DF       0x4000u
#define IPV4_FLAG_MF       0x2000u
#define IPV4_FRAG_OFFSET   0x1FFFu
#define IPV4_BROADCAST     0xFFFFFFFFu

static uint16_t ipv4_next_id = 1;

uint32_t ipv4_checksum_add(uint32_t sum, const uint8_t *data, size_t len) {
    size_t i = 0;

    for (; i + 1 < len; i += 2)
        sum += net_get16(data + i);
    if (i < len)
        sum += (uint32_t)data[i] << 8;
    return sum;
}

uint16_t ipv4_checksum_fold(uint32_t sum) {
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t ipv4_checksum(const uint8_t *data, size_t len) {
    return ipv4_checksum_fold(ipv4_checksum_add(0, data, len));
}

static int ipv4_is_local_destination(uint32_t dst) {
    const net_interface_t *iface = net_interface();
    uint32_t directed_broadcast = iface->ipv4 | ~iface->netmask;
    return dst == iface->ipv4 || dst == IPV4_BROADCAST || dst == directed_broadcast;
}

void ipv4_input(const uint8_t *packet, size_t len) {
    if (len < IPV4_HDR_LEN || (packet[0] >> 4) != 4) {
        net_count_rx_drop();
        return;
    }

    size_t header_len = (size_t)(packet[0] & 0x0F) * 4;
    size_t total_len = net_get16(packet + 2);
    /* total_len, not len: Ethernet pads short frames. */
    if (header_len < IPV4_HDR_LEN || total_len < header_len || total_len > len ||
        ipv4_checksum(packet, header_len) != 0) {
        net_count_rx_drop();
        return;
    }

    /* No reassembly: fragments are dropped. */
    uint16_t fragment = net_get16(packet + 6);
    if ((fragment & IPV4_FLAG_MF) || (fragment & IPV4_FRAG_OFFSET)) {
        net_count_rx_drop();
        return;
    }

    uint32_t src = net_get32(packet + 12);
    uint32_t dst = net_get32(packet + 16);
    if (!ipv4_is_local_destination(dst))
        return;

    const uint8_t *payload = packet + header_len;
    size_t payload_len = total_len - header_len;
    switch (packet[9]) {
    case IPV4_PROTO_ICMP:
        icmp_input(src, payload, payload_len);
        break;
    case IPV4_PROTO_UDP:
        udp_input(src, dst, payload, payload_len);
        break;
    default:
        break;
    }
}

static int ipv4_next_hop(uint32_t dst, uint32_t *out_hop) {
    const net_interface_t *iface = net_interface();
    if ((dst & iface->netmask) == (iface->ipv4 & iface->netmask)) {
        *out_hop = dst;
        return 0;
    }
    if (iface->gateway == 0)
        return -ENETUNREACH;
    *out_hop = iface->gateway;
    return 0;
}

int ipv4_output(uint8_t *frame, size_t payload_len, uint32_t dst,
                uint8_t protocol) {
    const net_interface_t *iface = net_interface();
    uint8_t mac[NET_ETH_ADDR_LEN];
    uint32_t hop = 0;
    int rc;

    if (payload_len > IPV4_PAYLOAD_MAX)
        return -EINVAL;

    rc = ipv4_next_hop(dst, &hop);
    if (rc != 0)
        return rc;
    rc = arp_resolve(hop, mac);
    if (rc != 0)
        return rc;

    uint8_t *header = frame + NET_ETH_HDR_LEN;
    header[0] = IPV4_VERSION_IHL;
    header[1] = 0;
    net_put16(header + 2, (uint16_t)(IPV4_HDR_LEN + payload_len));
    net_put16(header + 4, ipv4_next_id++);
    net_put16(header + 6, IPV4_FLAG_DF);
    header[8] = IPV4_DEFAULT_TTL;
    header[9] = protocol;
    net_put16(header + 10, 0);
    net_put32(header + 12, iface->ipv4);
    net_put32(header + 16, dst);
    net_put16(header + 10, ipv4_checksum(header, IPV4_HDR_LEN));

    return ethernet_output(frame, IPV4_HDR_LEN + payload_len, mac, ETHERTYPE_IPV4);
}
