#include "virtio_input.h"
#include "pci.h"
#include "msi.h"
#include "virtio.h"
#include "serial.h"
#include "paging.h"
#include "kheap.h"
#include <string.h>

#define VINPUT_KEY_BUF_SIZE 128

typedef struct virtio_input_device {
    pci_device_t pci_dev;
    virtio_pci_common_cfg_t *common_cfg;
    uint32_t *notify_cfg;
    uint32_t notify_off_multiplier;
    uint8_t *isr_cfg;
    virtio_input_config_t *device_cfg;

    virtqueue_t vq;
    uint16_t notify_off;

    virtio_input_event_t *events;
    uint16_t num_events;

    int key_buf[VINPUT_KEY_BUF_SIZE];
    int key_val_buf[VINPUT_KEY_BUF_SIZE];
    uint8_t key_head;
    uint8_t key_tail;

    int present;
} virtio_input_device_t;

static virtio_input_device_t g_vin;
static int g_vin_present = 0;

virtio_input_event_t virtio_input_last_event;
int virtio_input_last_key_value = 0;

static void vinput_serial_uint(uint32_t v) {
    char buf[16];
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v > 0 && i < 15) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    buf[i] = '\0';
    for (int a = 0, b = i - 1; a < b; ++a, --b) { char t = buf[a]; buf[a] = buf[b]; buf[b] = t; }
    serial_write(buf);
}

static void vinput_notify(void) {
    uintptr_t notify_addr = (uintptr_t)g_vin.notify_cfg + ((uintptr_t)g_vin.notify_off * g_vin.notify_off_multiplier);
    *(volatile uint16_t *)notify_addr = 0;
}

void virtio_input_poll(void) {
    if (!g_vin_present) return;

    virtqueue_t *vq = &g_vin.vq;
    int notified = 0;

    while (vq->last_used_idx != vq->used->idx) {
        virtq_used_item_t *used = &vq->used->ring[vq->last_used_idx % vq->num];
        uint16_t id = (uint16_t)used->id;

        if (id < g_vin.num_events) {
            virtio_input_event_t *ev = &g_vin.events[id];

            if (ev->type == VIRTIO_INPUT_EV_KEY) {
                g_vin.key_buf[g_vin.key_head] = (int)ev->code;
                g_vin.key_val_buf[g_vin.key_head] = (int)ev->value;
                g_vin.key_head = (uint8_t)((g_vin.key_head + 1) % VINPUT_KEY_BUF_SIZE);
                if (g_vin.key_head == g_vin.key_tail) {
                    g_vin.key_tail = (uint8_t)((g_vin.key_tail + 1) % VINPUT_KEY_BUF_SIZE);
                }

                virtio_input_last_event = *ev;
                virtio_input_last_key_value = (int)ev->value;

                serial_write("[VINPUT] EV_KEY code=");
                vinput_serial_uint(ev->code);
                serial_write(" value=");
                vinput_serial_uint(ev->value);
                serial_write("\n");
            }

            /* Re-submit the buffer to the device. */
            vq->desc[id].addr = virtio_get_phys(&g_vin.events[id]);
            vq->desc[id].len = sizeof(virtio_input_event_t);
            vq->desc[id].flags = VIRTQ_DESC_F_WRITE;

            vq->avail->ring[vq->avail->idx % vq->num] = id;
            __asm__ volatile("" ::: "memory");
            vq->avail->idx++;
            __asm__ volatile("" ::: "memory");
        }

        vq->last_used_idx++;
        notified = 1;
    }

    if (notified) {
        vinput_notify();
    }
}

int virtio_input_get_key_event(void) {
    virtio_input_poll();

    if (g_vin.key_head == g_vin.key_tail) {
        return -1;
    }

    int code = g_vin.key_buf[g_vin.key_tail];
    virtio_input_last_key_value = g_vin.key_val_buf[g_vin.key_tail];
    g_vin.key_tail = (uint8_t)((g_vin.key_tail + 1) % VINPUT_KEY_BUF_SIZE);
    return code;
}

int virtio_input_present(void) {
    return g_vin_present;
}

void virtio_input_init(void) {
    pci_device_t dev;
    int found = 0;
    if (pci_find_device(VIRTIO_INPUT_VENDOR_ID, VIRTIO_INPUT_DEVICE_MODERN, &dev) == 0) {
        found = 1;
    } else if (pci_find_device(VIRTIO_INPUT_VENDOR_ID, VIRTIO_INPUT_DEVICE_LEGACY, &dev) == 0) {
        found = 1;
    }

    if (!found) {
        serial_write("[VINPUT] No Virtio-Input device found\n");
        return;
    }

    serial_write("[VINPUT] Found Virtio-Input device\n");
    g_vin.pci_dev = dev;

    pci_enable_busmaster(&dev);
    pci_enable_mmio(&dev);

    if (pci_find_capability(&dev, PCI_CAP_ID_MSI) != 0) {
        msi_enable(dev);
    }

    uint8_t cap_ptr = pci_find_capability(&dev, 0x09);
    while (cap_ptr != 0) {
        virtio_pci_cap_t cap;
        for (size_t i = 0; i < sizeof(cap); i++) {
            ((uint8_t *)&cap)[i] = pci_config_read8(dev.bus, dev.slot, dev.func, cap_ptr + (uint8_t)i);
        }

        void *mapped = (uint8_t *)pci_map_bar(&g_vin.pci_dev, cap.bar) + cap.offset;

        switch (cap.cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                g_vin.common_cfg = (virtio_pci_common_cfg_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                g_vin.notify_cfg = (uint32_t *)mapped;
                g_vin.notify_off_multiplier = pci_config_read32(dev.bus, dev.slot, dev.func, cap_ptr + sizeof(virtio_pci_cap_t));
                if (g_vin.notify_off_multiplier == 0) g_vin.notify_off_multiplier = 1;
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                g_vin.isr_cfg = (uint8_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                g_vin.device_cfg = (virtio_input_config_t *)mapped;
                break;
        }

        uint8_t next_ptr = pci_config_read8(dev.bus, dev.slot, dev.func, cap_ptr + 1);
        cap_ptr = 0;
        while (next_ptr != 0) {
            uint8_t id = pci_config_read8(dev.bus, dev.slot, dev.func, next_ptr);
            if (id == 0x09) {
                cap_ptr = next_ptr;
                break;
            }
            next_ptr = pci_config_read8(dev.bus, dev.slot, dev.func, next_ptr + 1);
        }
    }

    if (!g_vin.common_cfg || !g_vin.notify_cfg) {
        serial_write("[VINPUT] Failed to find required Virtio capabilities\n");
        return;
    }

    /* Reset device */
    g_vin.common_cfg->device_status = 0;
    g_vin.common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
    g_vin.common_cfg->device_status |= VIRTIO_STATUS_DRIVER;

    /* Negotiate features */
    g_vin.common_cfg->device_feature_select = 0;
    (void)g_vin.common_cfg->device_feature;

    g_vin.common_cfg->device_feature_select = 1;
    uint32_t features_high = g_vin.common_cfg->device_feature;

    g_vin.common_cfg->driver_feature_select = 0;
    g_vin.common_cfg->driver_feature = 0;
    g_vin.common_cfg->driver_feature_select = 1;
    g_vin.common_cfg->driver_feature = (features_high & 0x1); /* VERSION_1 */

    g_vin.common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if (!(g_vin.common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_write("[VINPUT] Device rejected features\n");
        return;
    }

    /* Setup event queue (queue 0) */
    g_vin.common_cfg->queue_select = 0;
    uint16_t q_size = g_vin.common_cfg->queue_size;
    if (q_size == 0) q_size = 64;
    if ((q_size & (q_size - 1)) != 0) {
        /* round down to power of two */
        uint16_t p = 1;
        while (p * 2 <= q_size) p *= 2;
        q_size = p;
        if (q_size == 0) q_size = 16;
    }

    if (virtqueue_setup(&g_vin.vq, q_size) != 0) {
        serial_write("[VINPUT] Failed to setup virtqueue\n");
        return;
    }

    g_vin.num_events = q_size;
    g_vin.events = (virtio_input_event_t *)kalloc((size_t)q_size * sizeof(virtio_input_event_t));
    if (!g_vin.events) {
        serial_write("[VINPUT] Failed to allocate event buffers\n");
        return;
    }
    memset(g_vin.events, 0, (size_t)q_size * sizeof(virtio_input_event_t));

    g_vin.key_head = 0;
    g_vin.key_tail = 0;

    /* Submit all buffers to the device. */
    for (uint16_t i = 0; i < q_size; i++) {
        g_vin.vq.desc[i].addr = virtio_get_phys(&g_vin.events[i]);
        g_vin.vq.desc[i].len = sizeof(virtio_input_event_t);
        g_vin.vq.desc[i].flags = VIRTQ_DESC_F_WRITE;

        g_vin.vq.avail->ring[g_vin.vq.avail->idx % g_vin.vq.num] = i;
        __asm__ volatile("" ::: "memory");
        g_vin.vq.avail->idx++;
        __asm__ volatile("" ::: "memory");
    }

    g_vin.common_cfg->queue_desc = virtio_get_phys(g_vin.vq.desc);
    g_vin.common_cfg->queue_avail = virtio_get_phys(g_vin.vq.avail);
    g_vin.common_cfg->queue_used = virtio_get_phys(g_vin.vq.used);

    g_vin.notify_off = g_vin.common_cfg->queue_notify_off;
    g_vin.common_cfg->queue_enable = 1;

    /* Notify device that buffers are available. */
    vinput_notify();

    g_vin.common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;

    g_vin_present = 1;
    g_vin.present = 1;

    serial_write("[VINPUT] Virtio-Input initialized\n");
}
