#pragma once

#include <stddef.h>
#include <stdint.h>

void icmp_input(uint32_t src, const uint8_t *packet, size_t len);

/* Starts an echo request to dst; returns a ticket >= 0 or -errno. */
int icmp_ping_begin(uint32_t dst, uint32_t timeout_ms);

/* Advances the request. Returns 1 once it has finished, storing the
 * round-trip time in ms (or -errno) in *out_result and releasing the ticket;
 * returns 0 while it is still pending. */
int icmp_ping_poll(int ticket, int64_t *out_result);
