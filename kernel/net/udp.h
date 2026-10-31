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
 * 127.0.0.0/8) are looped back without touching the NIC. Never blocks: when
 * the next hop's MAC is still being resolved, the datagram is parked and a
 * positive ticket is returned; udp_send_wait_check() then completes it.
 * Returns 0 (sent), a ticket, or -errno (-EMSGSIZE, -ENETDOWN, -ENETUNREACH,
 * -ENOMEM when too many datagrams are parked). */
int udp_output(uint32_t dst, uint16_t src_port, uint16_t dst_port,
               const uint8_t *data, size_t len);

/* Kernel-wait callback for a parked datagram. Retries the transmission and
 * returns 1 with the syscall result (the payload length, or -EHOSTUNREACH once
 * neighbour resolution timed out) in *out_result, or 0 while it still waits. */
int udp_send_wait_check(uintptr_t ticket, uint64_t *out_result);

/* Drops a parked datagram whose waiter could not be blocked. */
void udp_send_cancel(int ticket);
