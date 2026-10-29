#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ipv4.h"

#define UDP_HDR_LEN     8
#define UDP_PAYLOAD_MAX (IPV4_PAYLOAD_MAX - UDP_HDR_LEN)

/* Validates a received datagram (length, checksum) and hands the payload to
 * the matching socket. Datagrams without a receiver are dropped silently. */
void udp_input(uint32_t src, uint32_t dst, const uint8_t *datagram, size_t len);

/* Sends one datagram. Destinations owned by this host (own address,
 * 127.0.0.0/8) are looped back without touching the NIC. Resolving the next
 * hop's MAC may wait up to UDP_ARP_TIMEOUT_MS. Returns 0 or -errno (-EINVAL,
 * -EMSGSIZE, -ENETDOWN, -ENETUNREACH, -EHOSTUNREACH). */
int udp_output(uint32_t dst, uint16_t src_port, uint16_t dst_port,
               const uint8_t *data, size_t len);
