#include "arp.h"

#include "core/time.h"
#include "errno.h"
#include "ethernet.h"
#include "net_bytes.h"

#include <string.h>

#define ARP_HTYPE_ETHERNET 1
#define ARP_OP_REQUEST     1
#define ARP_OP_REPLY       2
#define ARP_PACKET_LEN     28

#define ARP_CACHE_SIZE       16
#define ARP_ENTRY_TTL_MS     60000u
#define ARP_RETRY_INTERVAL_MS 250u

typedef enum arp_entry_state {
    ARP_ENTRY_FREE = 0,
    ARP_ENTRY_PENDING,
    ARP_ENTRY_RESOLVED,
} arp_entry_state_t;

typedef struct arp_entry {
    arp_entry_state_t state;
    uint32_t ipv4;
    uint8_t mac[NET_ETH_ADDR_LEN];
    uint8_t requested;
    uint64_t stamp_ms; /* resolved: last update, pending: last request */
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

static arp_entry_t *arp_find(uint32_t ipv4) {
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].state != ARP_ENTRY_FREE && arp_cache[i].ipv4 == ipv4)
            return &arp_cache[i];
    }
    return 0;
}

static arp_entry_t *arp_alloc(uint32_t ipv4) {
    arp_entry_t *victim = &arp_cache[0];
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].state == ARP_ENTRY_FREE) {
            victim = &arp_cache[i];
            break;
        }
        if (arp_cache[i].stamp_ms < victim->stamp_ms)
            victim = &arp_cache[i];
    }
    memset(victim, 0, sizeof(*victim));
    victim->ipv4 = ipv4;
    return victim;
}

static void arp_store(arp_entry_t *entry, const uint8_t mac[NET_ETH_ADDR_LEN]) {
    memcpy(entry->mac, mac, NET_ETH_ADDR_LEN);
    entry->state = ARP_ENTRY_RESOLVED;
    entry->stamp_ms = time_ms();
}

static int arp_send(uint16_t op, const uint8_t target_mac[NET_ETH_ADDR_LEN],
                    uint32_t target_ip, const uint8_t eth_dst[NET_ETH_ADDR_LEN]) {
    const net_interface_t *iface = net_interface();
    uint8_t frame[NET_ETH_FRAME_MIN];
    uint8_t *p = frame + NET_ETH_HDR_LEN;

    net_put16(p + 0, ARP_HTYPE_ETHERNET);
    net_put16(p + 2, ETHERTYPE_IPV4);
    p[4] = NET_ETH_ADDR_LEN;
    p[5] = 4;
    net_put16(p + 6, op);
    memcpy(p + 8, iface->mac, NET_ETH_ADDR_LEN);
    net_put32(p + 14, iface->ipv4);
    memcpy(p + 18, target_mac, NET_ETH_ADDR_LEN);
    net_put32(p + 24, target_ip);
    return ethernet_output(frame, ARP_PACKET_LEN, eth_dst, ETHERTYPE_ARP);
}

void arp_input(const uint8_t *packet, size_t len) {
    const net_interface_t *iface = net_interface();

    if (len < ARP_PACKET_LEN ||
        net_get16(packet + 0) != ARP_HTYPE_ETHERNET ||
        net_get16(packet + 2) != ETHERTYPE_IPV4 ||
        packet[4] != NET_ETH_ADDR_LEN || packet[5] != 4) {
        net_count_rx_drop();
        return;
    }

    uint16_t op = net_get16(packet + 6);
    const uint8_t *sender_mac = packet + 8;
    uint32_t sender_ip = net_get32(packet + 14);
    uint32_t target_ip = net_get32(packet + 24);

    if (sender_ip == 0 || sender_ip == iface->ipv4 ||
        (sender_mac[0] & 0x01) != 0)
        return;

    /* RFC 826: refresh known senders; learn new ones only when addressed. */
    arp_entry_t *entry = arp_find(sender_ip);
    if (entry) {
        arp_store(entry, sender_mac);
    } else if (target_ip == iface->ipv4) {
        arp_store(arp_alloc(sender_ip), sender_mac);
    }

    if (op == ARP_OP_REQUEST && target_ip == iface->ipv4)
        (void)arp_send(ARP_OP_REPLY, sender_mac, sender_ip, sender_mac);
}

int arp_resolve(uint32_t ipv4, uint8_t mac_out[NET_ETH_ADDR_LEN]) {
    static const uint8_t unknown_mac[NET_ETH_ADDR_LEN] = { 0 };
    uint64_t now = time_ms();
    arp_entry_t *entry = arp_find(ipv4);

    if (entry && entry->state == ARP_ENTRY_RESOLVED) {
        if (now - entry->stamp_ms < ARP_ENTRY_TTL_MS) {
            memcpy(mac_out, entry->mac, NET_ETH_ADDR_LEN);
            return 0;
        }
        entry->state = ARP_ENTRY_PENDING;
        entry->requested = 0;
    }
    if (!entry) {
        entry = arp_alloc(ipv4);
        entry->state = ARP_ENTRY_PENDING;
        entry->stamp_ms = now;
    }

    if (!entry->requested || now - entry->stamp_ms >= ARP_RETRY_INTERVAL_MS) {
        int rc = arp_send(ARP_OP_REQUEST, unknown_mac, ipv4, ethernet_broadcast);
        if (rc != 0)
            return rc;
        entry->requested = 1;
        entry->stamp_ms = now;
    }
    return -EAGAIN;
}
