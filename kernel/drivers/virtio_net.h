#pragma once

#include <stddef.h>
#include <stdint.h>

#define VIRTIO_NET_VENDOR_ID         0x1AF4
#define VIRTIO_NET_DEVICE_MODERN     0x1041
#define VIRTIO_NET_DEVICE_TRANSITION 0x1000

#define VIRTIO_NET_MAC_LEN   6
#define VIRTIO_NET_FRAME_MAX 1514

/*
 * Polled virtio-net driver (virtio 1.0 PCI transport). All entry points must
 * be called with interrupts disabled; the driver keeps no locks.
 */

typedef void (*virtio_net_rx_fn)(const uint8_t *frame, size_t len);

/* Probes and initializes the first virtio-net device. 0 on success, -errno if
 * no usable device exists; the kernel keeps running without networking. */
int virtio_net_init(void);
int virtio_net_present(void);
int virtio_net_link_up(void);
void virtio_net_get_mac(uint8_t out[VIRTIO_NET_MAC_LEN]);

/* Copies one Ethernet frame (without FCS) into a TX buffer and hands it to
 * the device. 0 on success, -EINVAL for bad lengths, -EBUSY if the TX ring is
 * full, -ENODEV without a device. */
int virtio_net_transmit(const uint8_t *frame, size_t len);

/* Hands every received frame to deliver() and recycles its buffer afterwards.
 * deliver() may call virtio_net_transmit(). Returns the number of frames. */
size_t virtio_net_poll(virtio_net_rx_fn deliver);
