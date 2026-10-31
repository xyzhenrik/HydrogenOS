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

/* Event codes (Linux input codes) used by the tablet path */
#define VIRTIO_INPUT_ABS_X      0
#define VIRTIO_INPUT_ABS_Y      1
#define VIRTIO_INPUT_REL_WHEEL  8
#define VIRTIO_INPUT_BTN_LEFT   0x110
#define VIRTIO_INPUT_BTN_RIGHT  0x111
#define VIRTIO_INPUT_BTN_MIDDLE 0x112

/* Device configuration selectors */
#define VIRTIO_INPUT_CFG_EV_BITS  0x11
#define VIRTIO_INPUT_CFG_ABS_INFO 0x12

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

/* Result of VIRTIO_INPUT_CFG_ABS_INFO (matches virtio_input_absinfo). */
typedef struct virtio_input_absinfo {
    uint32_t min;
    uint32_t max;
    uint32_t fuzz;
    uint32_t flat;
    uint32_t res;
} __attribute__((packed)) virtio_input_absinfo_t;

/* Probes every virtio-input PCI device: keyboards feed the key queue,
   tablets (EV_ABS) drive the shared mouse state in mouse.h. */
void virtio_input_init(void);
int virtio_input_present(void);

/* Poll all devices for pending events. */
void virtio_input_poll(void);

/* Returns the next pending key code (Linux input key code), or -1 if none. */
int virtio_input_get_key_event(void);

/* Last observed event (updated on poll/get). */
extern virtio_input_event_t virtio_input_last_event;
extern int virtio_input_last_key_value;
