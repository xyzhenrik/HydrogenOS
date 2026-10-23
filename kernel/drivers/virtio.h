#pragma once
#include <stdint.h>
#include <stddef.h>

/* Virtio PCI Capability types */
#define VIRTIO_PCI_CAP_COMMON_CFG        1
#define VIRTIO_PCI_CAP_NOTIFY_CFG        2
#define VIRTIO_PCI_CAP_ISR_CFG           3
#define VIRTIO_PCI_CAP_DEVICE_CFG         4
#define VIRTIO_PCI_CAP_PCI_CFG           5

#define PCI_CAP_ID_VND 0x09

typedef struct virtio_pci_cap {
    uint8_t cap_vndr;     /* Generic PCI field: PCI_CAP_ID_VND */
    uint8_t cap_next;     /* Generic PCI field: next ptr */
    uint8_t cap_len;      /* Generic PCI field: capability length */
    uint8_t cfg_type;     /* Identifies the structure */
    uint8_t bar;          /* Where to find it */
    uint8_t padding[3];   /* Pad to full dword */
    uint32_t offset;      /* Offset within bar */
    uint32_t length;      /* Length of the structure */
} __attribute__((packed)) virtio_pci_cap_t;

typedef struct virtio_pci_common_cfg {
    /* About the whole device */
    uint32_t device_feature_select; /* read-write */
    uint32_t device_feature;        /* read-only */
    uint32_t driver_feature_select; /* read-write */
    uint32_t driver_feature;        /* read-write */
    uint16_t msix_config;           /* read-write */
    uint16_t num_queues;            /* read-only */
    uint8_t  device_status;         /* read-write */
    uint8_t  config_generation;     /* read-only */

    /* About a specific virtqueue */
    uint16_t queue_select;          /* read-write */
    uint16_t queue_size;            /* read-write */
    uint16_t queue_msix_vector;     /* read-write */
    uint16_t queue_enable;          /* read-write */
    uint16_t queue_notify_off;      /* read-only */
    uint64_t queue_desc;            /* read-write */
    uint64_t queue_avail;           /* read-write */
    uint64_t queue_used;            /* read-write */
} __attribute__((packed)) virtio_pci_common_cfg_t;

/* -----------------------------------------------------------------------
 * Virtqueue Structures (Split Virtqueue)
 * ----------------------------------------------------------------------- */

#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2
#define VIRTQ_DESC_F_INDIRECT 4

typedef struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; /* Variable size */
} __attribute__((packed)) virtq_avail_t;

typedef struct virtq_used_item {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_item_t;

typedef struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    virtq_used_item_t ring[]; /* Variable size */
} __attribute__((packed)) virtq_used_t;

/* Internal handle for a virtqueue */
typedef struct virtqueue {
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    uint16_t num;
    uint16_t last_used_idx;
    uint16_t free_head;
    uint16_t num_free;
} virtqueue_t;

/* Virtio Device Status */
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED      128

int virtqueue_setup(virtqueue_t *vq, uint16_t num);
uintptr_t virtio_get_phys(void *virt);
