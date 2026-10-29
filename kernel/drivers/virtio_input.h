#pragma once

#include "virtio.h"
#include "pci.h"
#include <stdint.h>
#include <stddef.h>

/* PCI identifiers */
#define VIRTIO_INPUT_VENDOR_ID      0x1AF4
#define VIRTIO_INPUT_DEVICE_MODERN  0x1052
#define VIRTIO_INPUT_DEVICE_LEGACY  0x1004

/* Event types (Linux input event types) */
#define VIRTIO_INPUT_EV_SYN  0
#define VIRTIO_INPUT_EV_KEY  1
#define VIRTIO_INPUT_EV_REL  2
#define VIRTIO_INPUT_EV_ABS  3

/* Virtio input event (matches virtio_input_event) */
typedef struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed)) virtio_input_event_t;

/* Virtio input device configuration space */
typedef struct virtio_input_config {
    uint8_t select;
    uint8_t subsel;
    uint8_t size;
    uint8_t reserved[5];
    uint8_t data[128];
} __attribute__((packed)) virtio_input_config_t;

void virtio_input_init(void);
int virtio_input_present(void);

/* Poll the device for pending events. */
void virtio_input_poll(void);

/* Returns the next pending key code (Linux input key code), or -1 if none. */
int virtio_input_get_key_event(void);

/* Last observed event (updated on poll/get). */
extern virtio_input_event_t virtio_input_last_event;
extern int virtio_input_last_key_value;
