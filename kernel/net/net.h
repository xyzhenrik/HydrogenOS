#pragma once

#include <stdint.h>

#define NET_INFO_PRESENT (1u << 0)
#define NET_INFO_LINK_UP (1u << 1)

#define NET_PING_TIMEOUT_MAX_MS 60000u

/* SYS_NET_INFO result. IPv4 addresses have the first octet in the most
 * significant byte (10.0.2.15 == 0x0A00020F). */
typedef struct net_info {
    uint8_t mac[6];
    uint16_t flags;
    uint32_t ipv4;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_dropped;
} net_info_t;

/* Probes the NIC and applies the static configuration. Without a device the
 * kernel continues and every network call reports -ENODEV. */
void net_init(void);

/* 0, or -ENODEV without a network device. */
int net_get_info(net_info_t *out);

/* Sends an ICMP echo request. Returns a ticket for net_ping_wait_check() or
 * -errno (-ENODEV, -ENETDOWN, -EINVAL, -ENETUNREACH, -EBUSY). */
int net_ping_begin(uint32_t dst, uint32_t timeout_ms);

/* Kernel-wait callback: polls the NIC and returns 1 once the ping finished,
 * storing the RTT in ms or a negative errno (-ETIMEDOUT) in *out_result. */
int net_ping_wait_check(uintptr_t ticket, uint64_t *out_result);
