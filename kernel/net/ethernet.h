#pragma once

#include <stddef.h>
#include <stdint.h>

#include "netif.h"

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806

extern const uint8_t ethernet_broadcast[NET_ETH_ADDR_LEN];

void ethernet_input(const uint8_t *frame, size_t len);

/* frame must hold at least max(NET_ETH_FRAME_MIN, NET_ETH_HDR_LEN +
 * payload_len) bytes with the payload already at NET_ETH_HDR_LEN. Fills in
 * the header, pads runt frames and transmits. */
int ethernet_output(uint8_t *frame, size_t payload_len,
                    const uint8_t dst[NET_ETH_ADDR_LEN], uint16_t ethertype);
