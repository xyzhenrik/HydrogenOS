#include "ethernet.h"

#include "arp.h"
#include "errno.h"
#include "ipv4.h"
#include "net_bytes.h"

#include <string.h>

const uint8_t ethernet_broadcast[NET_ETH_ADDR_LEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

void ethernet_input(const uint8_t *frame, size_t len) {
    const net_interface_t *iface = net_interface();

    if (len < NET_ETH_HDR_LEN) {
        net_count_rx_drop();
        return;
    }
    if (memcmp(frame, iface->mac, NET_ETH_ADDR_LEN) != 0 &&
        memcmp(frame, ethernet_broadcast, NET_ETH_ADDR_LEN) != 0)
        return;

    const uint8_t *payload = frame + NET_ETH_HDR_LEN;
    size_t payload_len = len - NET_ETH_HDR_LEN;
    switch (net_get16(frame + 12)) {
    case ETHERTYPE_ARP:
        arp_input(payload, payload_len);
        break;
    case ETHERTYPE_IPV4:
        ipv4_input(payload, payload_len);
        break;
    default:
        break;
    }
}

int ethernet_output(uint8_t *frame, size_t payload_len,
                    const uint8_t dst[NET_ETH_ADDR_LEN], uint16_t ethertype) {
    const net_interface_t *iface = net_interface();

    if (payload_len > NET_ETH_MTU)
        return -EINVAL;

    memcpy(frame, dst, NET_ETH_ADDR_LEN);
    memcpy(frame + NET_ETH_ADDR_LEN, iface->mac, NET_ETH_ADDR_LEN);
    net_put16(frame + 12, ethertype);

    size_t len = NET_ETH_HDR_LEN + payload_len;
    if (len < NET_ETH_FRAME_MIN) {
        memset(frame + len, 0, NET_ETH_FRAME_MIN - len);
        len = NET_ETH_FRAME_MIN;
    }
    return net_link_transmit(frame, len);
}
