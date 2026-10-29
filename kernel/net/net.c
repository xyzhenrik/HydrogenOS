#include "net.h"

#include "drivers/virtio_net.h"
#include "errno.h"
#include "ethernet.h"
#include "icmp.h"
#include "net_bytes.h"
#include "netif.h"
#include "serial.h"

#include <string.h>

/* Static configuration matching QEMU user networking (slirp). */
#define NET_STATIC_IPV4    NET_IPV4_ADDR(10, 0, 2, 15)
#define NET_STATIC_NETMASK NET_IPV4_ADDR(255, 255, 255, 0)
#define NET_STATIC_GATEWAY NET_IPV4_ADDR(10, 0, 2, 2)

#define NET_IPV4_LOOPBACK_NET  NET_IPV4_ADDR(127, 0, 0, 0)
#define NET_IPV4_LOOPBACK_MASK NET_IPV4_ADDR(255, 0, 0, 0)
#define NET_IPV4_MULTICAST_NET  NET_IPV4_ADDR(224, 0, 0, 0)
#define NET_IPV4_MULTICAST_MASK NET_IPV4_ADDR(240, 0, 0, 0)

static net_interface_t net_iface;

const net_interface_t *net_interface(void) {
    return &net_iface;
}

void net_count_rx_drop(void) {
    net_iface.rx_dropped++;
}

int net_link_transmit(const uint8_t *frame, size_t len) {
    int rc = virtio_net_transmit(frame, len);
    if (rc == 0)
        net_iface.tx_frames++;
    return rc;
}

static void net_deliver(const uint8_t *frame, size_t len) {
    net_iface.rx_frames++;
    ethernet_input(frame, len);
}

void net_poll(void) {
    if (net_iface.up)
        (void)virtio_net_poll(net_deliver);
}

static void net_format_config(char *out) {
    static const char hex[] = "0123456789abcdef";
    size_t n = 0;

    for (int i = 0; i < NET_ETH_ADDR_LEN; ++i) {
        out[n++] = hex[net_iface.mac[i] >> 4];
        out[n++] = hex[net_iface.mac[i] & 0x0F];
        out[n++] = (i + 1 < NET_ETH_ADDR_LEN) ? ':' : ' ';
    }
    for (int i = 3; i >= 0; --i) {
        unsigned octet = (net_iface.ipv4 >> (i * 8)) & 0xFFu;
        if (octet >= 100)
            out[n++] = (char)('0' + octet / 100);
        if (octet >= 10)
            out[n++] = (char)('0' + (octet / 10) % 10);
        out[n++] = (char)('0' + octet % 10);
        out[n++] = i > 0 ? '.' : '\0';
    }
}

void net_init(void) {
    char config[40];

    memset(&net_iface, 0, sizeof(net_iface));
    if (virtio_net_init() != 0)
        return;

    virtio_net_get_mac(net_iface.mac);
    net_iface.ipv4 = NET_STATIC_IPV4;
    net_iface.netmask = NET_STATIC_NETMASK;
    net_iface.gateway = NET_STATIC_GATEWAY;
    net_iface.up = 1;

    net_format_config(config);
    serial_write("[NET] up: ");
    serial_write(config);
    serial_write("/24\n");
}

int net_link_up(void) {
    return net_iface.up && virtio_net_link_up();
}

int net_get_info(net_info_t *out) {
    if (!net_iface.up)
        return -ENODEV;
    memset(out, 0, sizeof(*out));
    memcpy(out->mac, net_iface.mac, NET_ETH_ADDR_LEN);
    out->flags = NET_INFO_PRESENT;
    if (virtio_net_link_up())
        out->flags |= NET_INFO_LINK_UP;
    out->ipv4 = net_iface.ipv4;
    out->netmask = net_iface.netmask;
    out->gateway = net_iface.gateway;
    out->rx_frames = net_iface.rx_frames;
    out->tx_frames = net_iface.tx_frames;
    out->rx_dropped = net_iface.rx_dropped;
    return 0;
}

int net_ping_begin(uint32_t dst, uint32_t timeout_ms) {
    if (!net_iface.up)
        return -ENODEV;
    if (!virtio_net_link_up())
        return -ENETDOWN;
    if (timeout_ms == 0 || timeout_ms > NET_PING_TIMEOUT_MAX_MS)
        return -EINVAL;

    uint32_t host_bits = ~net_iface.netmask;
    int on_link = (dst & net_iface.netmask) == (net_iface.ipv4 & net_iface.netmask);
    if (dst == 0 || dst == 0xFFFFFFFFu ||
        (dst & NET_IPV4_MULTICAST_MASK) == NET_IPV4_MULTICAST_NET ||
        (on_link && ((dst & host_bits) == host_bits || (dst & host_bits) == 0)))
        return -EINVAL;
    if ((dst & NET_IPV4_LOOPBACK_MASK) == NET_IPV4_LOOPBACK_NET)
        return -ENETUNREACH;

    net_poll();
    return icmp_ping_begin(dst, timeout_ms);
}

int net_ping_wait_check(uintptr_t ticket, uint64_t *out_result) {
    int64_t result = 0;

    net_poll();
    if (!icmp_ping_poll((int)ticket, &result))
        return 0;
    *out_result = (uint64_t)result;
    return 1;
}
