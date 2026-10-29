#include "virtio_net.h"

#include "errno.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"
#include "virtio.h"

#include <stddef.h>
#include <string.h>

#define VIRTIO_NET_F_MAC      5
#define VIRTIO_NET_F_STATUS   16
#define VIRTIO_F_VERSION_1_HI 0 /* bit 32 = bit 0 of the high feature word */
#define VIRTIO_NET_S_LINK_UP  1

#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
#define PCI_CMD_INTX_DISABLE (1u << 10)

#define VNET_RX_QUEUE   0
#define VNET_TX_QUEUE   1
#define VNET_QUEUE_MAX  64
#define VNET_BUF_SIZE   2048
#define VNET_PAGE_SIZE  4096
#define VNET_RESET_SPIN 1000000u
#define VNET_CAP_WALK_LIMIT 48

typedef struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed)) virtio_net_hdr_t;

typedef struct virtio_net_config {
    uint8_t mac[VIRTIO_NET_MAC_LEN];
    uint16_t status;
} __attribute__((packed)) virtio_net_config_t;

typedef struct vnet_queue {
    virtqueue_t vq;
    uintptr_t ring_phys;
    size_t ring_pages;
    uintptr_t buf_phys;
    size_t buf_pages;
    volatile uint16_t *notify;
    uint16_t index;
    uint16_t free_ids[VNET_QUEUE_MAX];
    uint16_t free_count;
} vnet_queue_t;

typedef struct virtio_net_device {
    pci_device_t pci;
    volatile virtio_pci_common_cfg_t *common;
    volatile uint8_t *notify_base;
    uint32_t notify_length;
    uint32_t notify_multiplier;
    volatile virtio_net_config_t *config;
    uint32_t config_length;
    int has_status;
    vnet_queue_t rx;
    vnet_queue_t tx;
    uint8_t mac[VIRTIO_NET_MAC_LEN];
} virtio_net_device_t;

static virtio_net_device_t g_vnet;
static int g_vnet_present = 0;

static inline void vnet_compiler_barrier(void) {
    __asm__ volatile("" ::: "memory");
}

/* Ring updates must be globally visible before the device sees the notify. */
static inline void vnet_full_barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static void vnet_write64(size_t offset, uint64_t value) {
    volatile uint32_t *reg =
        (volatile uint32_t *)((volatile uint8_t *)g_vnet.common + offset);
    reg[0] = (uint32_t)value;
    reg[1] = (uint32_t)(value >> 32);
}

static volatile uint8_t *vnet_map_cap(uint8_t bar, uint32_t offset,
                                      uint32_t length) {
    pci_device_t *dev = &g_vnet.pci;
    if (bar >= 6 || dev->bar_is_io[bar] || dev->bar_size[bar] == 0)
        return 0;
    if ((uint64_t)offset + length > dev->bar_size[bar])
        return 0;
    /* A 64-bit BAR placed above 4 GiB cannot be expressed by the PCI layer. */
    if ((dev->bar[bar] & 0x6u) == PCI_BAR_TYPE_MEM64 &&
        (bar == 5 || dev->bar[bar + 1] != 0))
        return 0;
    uint8_t *base = (uint8_t *)pci_map_bar(dev, bar);
    if (!base)
        return 0;
    return base + offset;
}

static void vnet_map_capabilities(void) {
    pci_device_t *dev = &g_vnet.pci;
    uint8_t ptr = pci_find_capability(dev, PCI_CAP_ID_VND);
    int remaining = VNET_CAP_WALK_LIMIT;

    while (ptr != 0 && remaining-- > 0) {
        if (pci_config_read8(dev->bus, dev->slot, dev->func, ptr) == PCI_CAP_ID_VND) {
            uint8_t type = pci_config_read8(dev->bus, dev->slot, dev->func,
                                            (uint8_t)(ptr + 3));
            uint8_t bar = pci_config_read8(dev->bus, dev->slot, dev->func,
                                           (uint8_t)(ptr + 4));
            uint32_t offset = pci_config_read32(dev->bus, dev->slot, dev->func,
                                                (uint8_t)(ptr + 8));
            uint32_t length = pci_config_read32(dev->bus, dev->slot, dev->func,
                                                (uint8_t)(ptr + 12));

            /* The spec lets devices repeat capabilities; the first usable one wins. */
            if (type == VIRTIO_PCI_CAP_COMMON_CFG && !g_vnet.common &&
                length >= sizeof(virtio_pci_common_cfg_t)) {
                g_vnet.common = (volatile virtio_pci_common_cfg_t *)
                    vnet_map_cap(bar, offset, length);
            } else if (type == VIRTIO_PCI_CAP_NOTIFY_CFG && !g_vnet.notify_base) {
                g_vnet.notify_base = vnet_map_cap(bar, offset, length);
                g_vnet.notify_length = length;
                g_vnet.notify_multiplier = pci_config_read32(
                    dev->bus, dev->slot, dev->func,
                    (uint8_t)(ptr + sizeof(virtio_pci_cap_t)));
            } else if (type == VIRTIO_PCI_CAP_DEVICE_CFG && !g_vnet.config &&
                       length >= sizeof(virtio_net_config_t)) {
                g_vnet.config = (volatile virtio_net_config_t *)
                    vnet_map_cap(bar, offset, length);
                g_vnet.config_length = length;
            }
        }
        ptr = pci_config_read8(dev->bus, dev->slot, dev->func, (uint8_t)(ptr + 1));
    }
}

static int vnet_reset(void) {
    g_vnet.common->device_status = 0;
    for (uint32_t spin = 0; spin < VNET_RESET_SPIN; ++spin) {
        if (g_vnet.common->device_status == 0)
            return 0;
        __asm__ volatile("pause");
    }
    return -EIO;
}

static void vnet_free_pages(uintptr_t phys, size_t pages) {
    for (size_t i = 0; i < pages; ++i)
        pmm_free_page(phys + i * VNET_PAGE_SIZE);
}

static void vnet_queue_release(vnet_queue_t *q) {
    if (q->ring_phys)
        vnet_free_pages(q->ring_phys, q->ring_pages);
    if (q->buf_phys)
        vnet_free_pages(q->buf_phys, q->buf_pages);
    memset(q, 0, sizeof(*q));
}

static size_t vnet_ring_bytes(uint16_t num, size_t *avail_off, size_t *used_off) {
    size_t desc_bytes = sizeof(virtq_desc_t) * num;
    size_t avail_bytes = sizeof(virtq_avail_t) + sizeof(uint16_t) * num + sizeof(uint16_t);
    size_t used_bytes = sizeof(virtq_used_t) + sizeof(virtq_used_item_t) * num + sizeof(uint16_t);
    *avail_off = desc_bytes;
    *used_off = (desc_bytes + avail_bytes + 3u) & ~(size_t)3u;
    return *used_off + used_bytes;
}

static uint8_t *vnet_buffer(const vnet_queue_t *q, uint16_t id) {
    return (uint8_t *)(q->buf_phys + (uintptr_t)id * VNET_BUF_SIZE);
}

static void vnet_notify(const vnet_queue_t *q) {
    vnet_full_barrier();
    *q->notify = q->index;
}

static void vnet_publish(vnet_queue_t *q, uint16_t id) {
    volatile virtq_avail_t *avail = q->vq.avail;
    uint16_t idx = avail->idx;
    avail->ring[idx % q->vq.num] = id;
    vnet_compiler_barrier();
    avail->idx = (uint16_t)(idx + 1);
}

static int vnet_queue_setup(vnet_queue_t *q, uint16_t index) {
    volatile virtio_pci_common_cfg_t *common = g_vnet.common;
    size_t avail_off = 0;
    size_t used_off = 0;

    memset(q, 0, sizeof(*q));
    common->queue_select = index;
    uint16_t device_max = common->queue_size;
    uint16_t num = VNET_QUEUE_MAX;
    while (num > device_max)
        num = (uint16_t)(num >> 1);
    if (num == 0)
        return -ENODEV;

    size_t ring_bytes = vnet_ring_bytes(num, &avail_off, &used_off);
    q->ring_pages = (ring_bytes + VNET_PAGE_SIZE - 1) / VNET_PAGE_SIZE;
    q->buf_pages = ((size_t)num * VNET_BUF_SIZE + VNET_PAGE_SIZE - 1) / VNET_PAGE_SIZE;
    q->ring_phys = pmm_alloc_contiguous(q->ring_pages);
    q->buf_phys = pmm_alloc_contiguous(q->buf_pages);
    if (!q->ring_phys || !q->buf_phys) {
        vnet_queue_release(q);
        return -ENOMEM;
    }
    memset((void *)q->ring_phys, 0, q->ring_pages * VNET_PAGE_SIZE);
    memset((void *)q->buf_phys, 0, q->buf_pages * VNET_PAGE_SIZE);

    q->index = index;
    q->vq.num = num;
    q->vq.desc = (virtq_desc_t *)q->ring_phys;
    q->vq.avail = (virtq_avail_t *)(q->ring_phys + avail_off);
    q->vq.used = (virtq_used_t *)(q->ring_phys + used_off);
    q->vq.last_used_idx = 0;
    q->vq.avail->flags = VIRTQ_AVAIL_F_NO_INTERRUPT;

    for (uint16_t id = 0; id < num; ++id) {
        q->vq.desc[id].addr = virtio_get_phys(vnet_buffer(q, id));
        q->vq.desc[id].len = VNET_BUF_SIZE;
        q->vq.desc[id].flags = 0;
        q->vq.desc[id].next = 0;
    }

    uint32_t notify_off = (uint32_t)common->queue_notify_off * g_vnet.notify_multiplier;
    if ((uint64_t)notify_off + sizeof(uint16_t) > g_vnet.notify_length) {
        vnet_queue_release(q);
        return -ENODEV;
    }
    q->notify = (volatile uint16_t *)(g_vnet.notify_base + notify_off);

    common->queue_size = num;
    vnet_write64(offsetof(virtio_pci_common_cfg_t, queue_desc),
                 virtio_get_phys(q->vq.desc));
    vnet_write64(offsetof(virtio_pci_common_cfg_t, queue_avail),
                 virtio_get_phys(q->vq.avail));
    vnet_write64(offsetof(virtio_pci_common_cfg_t, queue_used),
                 virtio_get_phys(q->vq.used));
    common->queue_enable = 1;
    return 0;
}

static void vnet_read_mac(void) {
    uint8_t generation;
    do {
        generation = g_vnet.common->config_generation;
        for (int i = 0; i < VIRTIO_NET_MAC_LEN; ++i)
            g_vnet.mac[i] = g_vnet.config->mac[i];
    } while (generation != g_vnet.common->config_generation);
}

static int vnet_negotiate(void) {
    volatile virtio_pci_common_cfg_t *common = g_vnet.common;

    common->device_feature_select = 0;
    uint32_t offered_lo = common->device_feature;
    common->device_feature_select = 1;
    uint32_t offered_hi = common->device_feature;
    if (!(offered_hi & (1u << VIRTIO_F_VERSION_1_HI)))
        return -ENODEV;

    uint32_t wanted_lo = offered_lo &
        ((1u << VIRTIO_NET_F_MAC) | (1u << VIRTIO_NET_F_STATUS));
    common->driver_feature_select = 0;
    common->driver_feature = wanted_lo;
    common->driver_feature_select = 1;
    common->driver_feature = 1u << VIRTIO_F_VERSION_1_HI;

    common->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if (!(common->device_status & VIRTIO_STATUS_FEATURES_OK))
        return -ENODEV;

    if (!(wanted_lo & (1u << VIRTIO_NET_F_MAC)))
        return -ENODEV;
    g_vnet.has_status = (wanted_lo & (1u << VIRTIO_NET_F_STATUS)) != 0;
    return 0;
}

static int vnet_find_device(void) {
    if (pci_find_device(VIRTIO_NET_VENDOR_ID, VIRTIO_NET_DEVICE_MODERN, &g_vnet.pci) == 0)
        return 0;
    if (pci_find_device(VIRTIO_NET_VENDOR_ID, VIRTIO_NET_DEVICE_TRANSITION, &g_vnet.pci) == 0)
        return 0;
    return -ENODEV;
}

static int vnet_bring_up(void) {
    pci_device_t *dev = &g_vnet.pci;
    int rc;

    /* The driver polls; INTx stays off so an unrouted line cannot fire. */
    uint16_t cmd = pci_config_read16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    cmd |= PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER | PCI_CMD_INTX_DISABLE;
    pci_config_write16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);

    vnet_map_capabilities();
    if (!g_vnet.common || !g_vnet.notify_base || !g_vnet.config)
        return -ENODEV;

    rc = vnet_reset();
    if (rc != 0)
        return rc;
    g_vnet.common->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
    g_vnet.common->device_status |= VIRTIO_STATUS_DRIVER;

    rc = vnet_negotiate();
    if (rc != 0)
        return rc;

    rc = vnet_queue_setup(&g_vnet.rx, VNET_RX_QUEUE);
    if (rc != 0)
        return rc;
    rc = vnet_queue_setup(&g_vnet.tx, VNET_TX_QUEUE);
    if (rc != 0)
        return rc;

    for (uint16_t id = 0; id < g_vnet.rx.vq.num; ++id) {
        g_vnet.rx.vq.desc[id].flags = VIRTQ_DESC_F_WRITE;
        vnet_publish(&g_vnet.rx, id);
    }
    for (uint16_t id = 0; id < g_vnet.tx.vq.num; ++id)
        g_vnet.tx.free_ids[g_vnet.tx.free_count++] = id;

    vnet_read_mac();
    g_vnet.common->device_status |= VIRTIO_STATUS_DRIVER_OK;
    if (g_vnet.common->device_status & VIRTIO_STATUS_NEEDS_RESET)
        return -EIO;
    vnet_notify(&g_vnet.rx);
    return 0;
}

int virtio_net_init(void) {
    memset(&g_vnet, 0, sizeof(g_vnet));
    g_vnet_present = 0;

    if (vnet_find_device() != 0) {
        serial_write("[VNET] no virtio-net device\n");
        return -ENODEV;
    }

    int rc = vnet_bring_up();
    if (rc != 0) {
        if (g_vnet.common) {
            /* Ring memory is only reusable once the device has let go of it;
             * a device that refuses to reset keeps its pages. */
            if (vnet_reset() == 0) {
                vnet_queue_release(&g_vnet.rx);
                vnet_queue_release(&g_vnet.tx);
            }
            g_vnet.common->device_status = VIRTIO_STATUS_FAILED;
        }
        serial_write("[VNET] device initialization failed\n");
        return rc;
    }

    g_vnet_present = 1;
    serial_write("[VNET] virtio-net ready, rx/tx queue size ");
    serial_write_hex(g_vnet.rx.vq.num);
    serial_write("\n");
    return 0;
}

int virtio_net_present(void) {
    return g_vnet_present;
}

int virtio_net_link_up(void) {
    if (!g_vnet_present)
        return 0;
    if (!g_vnet.has_status)
        return 1;
    return (g_vnet.config->status & VIRTIO_NET_S_LINK_UP) != 0;
}

void virtio_net_get_mac(uint8_t out[VIRTIO_NET_MAC_LEN]) {
    for (int i = 0; i < VIRTIO_NET_MAC_LEN; ++i)
        out[i] = g_vnet_present ? g_vnet.mac[i] : 0;
}

static void vnet_tx_reclaim(void) {
    vnet_queue_t *q = &g_vnet.tx;
    volatile virtq_used_t *used = q->vq.used;

    while (q->vq.last_used_idx != used->idx) {
        vnet_compiler_barrier();
        uint32_t id = used->ring[q->vq.last_used_idx % q->vq.num].id;
        if (id < q->vq.num && q->free_count < q->vq.num)
            q->free_ids[q->free_count++] = (uint16_t)id;
        q->vq.last_used_idx++;
    }
}

int virtio_net_transmit(const uint8_t *frame, size_t len) {
    vnet_queue_t *q = &g_vnet.tx;

    if (!g_vnet_present)
        return -ENODEV;
    if (!frame || len == 0 || len > VIRTIO_NET_FRAME_MAX)
        return -EINVAL;

    vnet_tx_reclaim();
    if (q->free_count == 0)
        return -EBUSY;

    uint16_t id = q->free_ids[--q->free_count];
    uint8_t *buf = vnet_buffer(q, id);
    memset(buf, 0, sizeof(virtio_net_hdr_t));
    memcpy(buf + sizeof(virtio_net_hdr_t), frame, len);
    q->vq.desc[id].len = (uint32_t)(sizeof(virtio_net_hdr_t) + len);
    q->vq.desc[id].flags = 0;

    vnet_publish(q, id);
    vnet_notify(q);
    return 0;
}

size_t virtio_net_poll(virtio_net_rx_fn deliver) {
    vnet_queue_t *q = &g_vnet.rx;
    size_t frames = 0;

    if (!g_vnet_present || !deliver)
        return 0;

    volatile virtq_used_t *used = q->vq.used;
    /* Bound the work per call so a flooding peer cannot pin the CPU here. */
    while (q->vq.last_used_idx != used->idx && frames < q->vq.num) {
        vnet_compiler_barrier();
        uint16_t slot = (uint16_t)(q->vq.last_used_idx % q->vq.num);
        uint32_t id = used->ring[slot].id;
        uint32_t len = used->ring[slot].len;
        q->vq.last_used_idx++;
        if (id >= q->vq.num)
            continue;

        if (len > sizeof(virtio_net_hdr_t) && len <= VNET_BUF_SIZE)
            deliver(vnet_buffer(q, (uint16_t)id) + sizeof(virtio_net_hdr_t),
                    len - sizeof(virtio_net_hdr_t));

        q->vq.desc[id].len = VNET_BUF_SIZE;
        q->vq.desc[id].flags = VIRTQ_DESC_F_WRITE;
        vnet_publish(q, (uint16_t)id);
        frames++;
    }

    if (frames > 0)
        vnet_notify(q);
    return frames;
}
