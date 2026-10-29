#pragma once

#include <stddef.h>
#include <stdint.h>

#include "netif.h"

void arp_input(const uint8_t *packet, size_t len);

/* 0 with mac_out filled on a cache hit. On a miss a request is broadcast
 * (rate-limited per address) and -EAGAIN is returned. */
int arp_resolve(uint32_t ipv4, uint8_t mac_out[NET_ETH_ADDR_LEN]);
