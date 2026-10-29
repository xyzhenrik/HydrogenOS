#pragma once

#include <stddef.h>
#include <stdint.h>

#define NET_ETH_ADDR_LEN  6
#define NET_ETH_HDR_LEN   14
#define NET_ETH_MTU       1500
#define NET_ETH_FRAME_MIN 60
#define NET_ETH_FRAME_MAX (NET_ETH_HDR_LEN + NET_ETH_MTU)

/* State of the single network interface, shared by the protocol layers.
 * IPv4 addresses are host-order integers with the first octet in the most
 * significant byte. The whole stack runs with interrupts disabled on one CPU
 * and therefore needs no locking. */
typedef struct net_interface {
    int up;
    uint8_t mac[NET_ETH_ADDR_LEN];
    uint32_t ipv4;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_dropped;
} net_interface_t;

const net_interface_t *net_interface(void);
void net_count_rx_drop(void);

/* Transmits a complete Ethernet frame. 0 or -errno from the driver. */
int net_link_transmit(const uint8_t *frame, size_t len);

/* Drains the receive ring through the protocol stack. */
void net_poll(void);
