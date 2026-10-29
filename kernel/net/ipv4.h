#pragma once

#include <stddef.h>
#include <stdint.h>

#include "netif.h"

#define IPV4_HDR_LEN        20
#define IPV4_PAYLOAD_MAX    (NET_ETH_MTU - IPV4_HDR_LEN)
#define IPV4_PAYLOAD_OFFSET (NET_ETH_HDR_LEN + IPV4_HDR_LEN)
#define IPV4_PROTO_ICMP     1

/* RFC 1071 Internet checksum, ready to be stored big-endian. */
uint16_t ipv4_checksum(const uint8_t *data, size_t len);

void ipv4_input(const uint8_t *packet, size_t len);

/* frame carries the payload at IPV4_PAYLOAD_OFFSET and must be large enough
 * for ethernet_output(). Returns 0, -EAGAIN while the next hop's MAC is still
 * being resolved, or another -errno. */
int ipv4_output(uint8_t *frame, size_t payload_len, uint32_t dst,
                uint8_t protocol);
