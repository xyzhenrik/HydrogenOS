#include "virtio_input.h"
#include "pci.h"
#include "msi.h"
#include "virtio.h"
#include "serial.h"
#include "paging.h"
#include "kheap.h"
#include "mouse.h"
#include <string.h>

#define VINPUT_KEY_BUF_SIZE 128
#define VINPUT_MAX_DEVICES 4

typedef enum vinput_kind {
    VINPUT_KIND_KEYBOARD,
    VINPUT_KIND_TABLET,
} vinput_kind_t;

typedef struct virtio_input_device {
    pci_device_t pci_dev;
    virtio_pci_common_cfg_t *common_cfg;
    uint32_t *notify_cfg;
    uint32_t notify_off_multiplier;
    uint8_t *isr_cfg;
    volatile virtio_input_config_t *device_cfg;

    virtqueue_t vq;
    uint16_t notify_off;

    virtio_input_event_t *events;
    uint16_t num_events;

    vinput_kind_t kind;

    /* Tablet: events accumulate here and are published on EV_SYN so the
       desktop never observes a half-updated (x, y, buttons) tuple. */
    uint32_t abs_range[2];
    uint32_t abs_pos[2];
    int abs_pos_dirty;
    uint8_t buttons;
    int32_t wheel;
} virtio_input_device_t;

static virtio_input_device_t g_vin[VINPUT_MAX_DEVICES];
static int g_vin_count = 0;

/* Keyboards of all devices share one queue; tablets never feed it. */
static int g_key_buf[VINPUT_KEY_BUF_SIZE];
static int g_key_val_buf[VINPUT_KEY_BUF_SIZE];
static uint8_t g_key_head = 0;
static uint8_t g_key_tail = 0;

virtio_input_event_t virtio_input_last_event;
int virtio_input_last_key_value = 0;

static void vinput_notify(const virtio_input_device_t *d) {
    uintptr_t notify_addr = (uintptr_t)d->notify_cfg + ((uintptr_t)d->notify_off * d->notify_off_multiplier);
    *(volatile uint16_t *)notify_addr = 0;
}

/* Selects a config page and copies out what the device reports; returns the
   reported size (0 = not supported by this device). */
static uint8_t vinput_cfg_query(volatile virtio_input_config_t *cfg, uint8_t select,
                                uint8_t subsel, uint8_t *out, uint8_t capacity) {
    cfg->select = select;
    cfg->subsel = subsel;
    __asm__ volatile("" ::: "memory");
    uint8_t size = cfg->size;
    for (uint8_t i = 0; i < size && i < capacity; i++) out[i] = cfg->data[i];
    return size;
}

static void vinput_queue_key(const virtio_input_event_t *ev) {
    g_key_buf[g_key_head] = (int)ev->code;
    g_key_val_buf[g_key_head] = (int)ev->value;
    g_key_head = (uint8_t)((g_key_head + 1) % VINPUT_KEY_BUF_SIZE);
    if (g_key_head == g_key_tail) {
        g_key_tail = (uint8_t)((g_key_tail + 1) % VINPUT_KEY_BUF_SIZE);
    }
    virtio_input_last_event = *ev;
    virtio_input_last_key_value = (int)ev->value;
}

static uint8_t vinput_button_bit(uint16_t code) {
    switch (code) {
        case VIRTIO_INPUT_BTN_LEFT:   return 0x01;
        case VIRTIO_INPUT_BTN_RIGHT:  return 0x02;
        case VIRTIO_INPUT_BTN_MIDDLE: return 0x04;
        default:                      return 0;
    }
}

static void vinput_tablet_event(virtio_input_device_t *d, const virtio_input_event_t *ev) {
    switch (ev->type) {
        case VIRTIO_INPUT_EV_ABS:
            if (ev->code == VIRTIO_INPUT_ABS_X || ev->code == VIRTIO_INPUT_ABS_Y) {
                int axis = ev->code == VIRTIO_INPUT_ABS_Y;
                /* abs_pos is stored relative to the axis minimum. */
                uint32_t pos = ev->value;
                d->abs_pos[axis] = pos > d->abs_range[axis] ? d->abs_range[axis] : pos;
                d->abs_pos_dirty = 1;
            }
            break;
        case VIRTIO_INPUT_EV_KEY: {
            uint8_t bit = vinput_button_bit(ev->code);
            if (bit == 0) break;
            d->buttons = ev->value ? (uint8_t)(d->buttons | bit) : (uint8_t)(d->buttons & ~bit);
            break;
        }
        case VIRTIO_INPUT_EV_REL:
            if (ev->code == VIRTIO_INPUT_REL_WHEEL) d->wheel += (int32_t)ev->value;
            break;
        case VIRTIO_INPUT_EV_SYN:
            if (d->abs_pos_dirty) {
                mouse_set_position_scaled(d->abs_pos[0], d->abs_range[0], d->abs_pos[1], d->abs_range[1]);
                d->abs_pos_dirty = 0;
            }
            mouse_set_buttons(d->buttons);
            if (d->wheel != 0) {
                /* Linux reports wheel-up as positive, the PS/2 path as negative. */
                mouse_add_wheel(-d->wheel);
                d->wheel = 0;
            }
            break;
        default:
            break;
    }
}

static void vinput_poll_device(virtio_input_device_t *d) {
    virtqueue_t *vq = &d->vq;
    int notified = 0;

    while (vq->last_used_idx != vq->used->idx) {
        virtq_used_item_t *used = &vq->used->ring[vq->last_used_idx % vq->num];
        uint16_t id = (uint16_t)used->id;

        if (id < d->num_events) {
            virtio_input_event_t ev = d->events[id];

            if (d->kind == VINPUT_KIND_TABLET) {
                vinput_tablet_event(d, &ev);
            } else if (ev.type == VIRTIO_INPUT_EV_KEY) {
                vinput_queue_key(&ev);
            }

            /* Re-submit the buffer to the device. */
            vq->desc[id].addr = virtio_get_phys(&d->events[id]);
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
        vinput_notify(d);
    }
}

void virtio_input_poll(void) {
    for (int i = 0; i < g_vin_count; i++) vinput_poll_device(&g_vin[i]);
}

int virtio_input_get_key_event(void) {
    virtio_input_poll();

    if (g_key_head == g_key_tail) {
        return -1;
    }

    int code = g_key_buf[g_key_tail];
    virtio_input_last_key_value = g_key_val_buf[g_key_tail];
    g_key_tail = (uint8_t)((g_key_tail + 1) % VINPUT_KEY_BUF_SIZE);
    return code;
}

int virtio_input_present(void) {
    return g_vin_count > 0;
}

/* Fills in d->kind and the tablet axis ranges. Returns -1 for devices this
   driver does not handle (e.g. relative virtio mice). */
static int vinput_classify(virtio_input_device_t *d) {
    uint8_t bits[8];

    if (vinput_cfg_query(d->device_cfg, VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_ABS, bits, sizeof(bits)) > 0
        && (bits[0] & ((1u << VIRTIO_INPUT_ABS_X) | (1u << VIRTIO_INPUT_ABS_Y))) ==
               ((1u << VIRTIO_INPUT_ABS_X) | (1u << VIRTIO_INPUT_ABS_Y))) {
        for (int axis = 0; axis < 2; axis++) {
            virtio_input_absinfo_t info;
            uint8_t size = vinput_cfg_query(d->device_cfg, VIRTIO_INPUT_CFG_ABS_INFO,
                                            (uint8_t)axis, (uint8_t *)&info, sizeof(info));
            if (size < 2 * sizeof(uint32_t) || info.max <= info.min) return -1;
            d->abs_range[axis] = info.max - info.min;
            d->abs_pos[axis] = d->abs_range[axis] / 2;
        }
        d->kind = VINPUT_KIND_TABLET;
        return 0;
    }

    if (vinput_cfg_query(d->device_cfg, VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_REL, bits, sizeof(bits)) > 0)
        return -1;
    if (vinput_cfg_query(d->device_cfg, VIRTIO_INPUT_CFG_EV_BITS, VIRTIO_INPUT_EV_KEY, bits, sizeof(bits)) == 0)
        return -1;
    d->kind = VINPUT_KIND_KEYBOARD;
    return 0;
}

static int virtio_input_init_device(const pci_device_t *found) {
    virtio_input_device_t *d = &g_vin[g_vin_count];
    memset(d, 0, sizeof(*d));
    d->pci_dev = *found;
    pci_device_t dev = *found;

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

        void *mapped = (uint8_t *)pci_map_bar(&d->pci_dev, cap.bar) + cap.offset;

        switch (cap.cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                d->common_cfg = (virtio_pci_common_cfg_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                d->notify_cfg = (uint32_t *)mapped;
                d->notify_off_multiplier = pci_config_read32(dev.bus, dev.slot, dev.func, cap_ptr + sizeof(virtio_pci_cap_t));
                if (d->notify_off_multiplier == 0) d->notify_off_multiplier = 1;
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                d->isr_cfg = (uint8_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                d->device_cfg = (volatile virtio_input_config_t *)mapped;
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

    if (!d->common_cfg || !d->notify_cfg || !d->device_cfg) {
        serial_write("[VINPUT] Failed to find required Virtio capabilities\n");
        return -1;
    }

    if (vinput_classify(d) != 0) {
        serial_write("[VINPUT] Unsupported input device, skipped\n");
        return -1;
    }

    /* Reset device */
    d->common_cfg->device_status = 0;
    d->common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
    d->common_cfg->device_status |= VIRTIO_STATUS_DRIVER;

    /* Negotiate features */
    d->common_cfg->device_feature_select = 0;
    (void)d->common_cfg->device_feature;

    d->common_cfg->device_feature_select = 1;
    uint32_t features_high = d->common_cfg->device_feature;

    d->common_cfg->driver_feature_select = 0;
    d->common_cfg->driver_feature = 0;
    d->common_cfg->driver_feature_select = 1;
    d->common_cfg->driver_feature = (features_high & 0x1); /* VERSION_1 */

    d->common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if (!(d->common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_write("[VINPUT] Device rejected features\n");
        return -1;
    }

    /* Setup event queue (queue 0) */
    d->common_cfg->queue_select = 0;
    uint16_t q_size = d->common_cfg->queue_size;
    if (q_size == 0) q_size = 64;
    if ((q_size & (q_size - 1)) != 0) {
        /* round down to power of two */
        uint16_t p = 1;
        while (p * 2 <= q_size) p *= 2;
        q_size = p;
        if (q_size == 0) q_size = 16;
    }

    if (virtqueue_setup(&d->vq, q_size) != 0) {
        serial_write("[VINPUT] Failed to setup virtqueue\n");
        return -1;
    }

    d->num_events = q_size;
    d->events = (virtio_input_event_t *)kalloc((size_t)q_size * sizeof(virtio_input_event_t));
    if (!d->events) {
        serial_write("[VINPUT] Failed to allocate event buffers\n");
        return -1;
    }
    memset(d->events, 0, (size_t)q_size * sizeof(virtio_input_event_t));

    /* Submit all buffers to the device. */
    for (uint16_t i = 0; i < q_size; i++) {
        d->vq.desc[i].addr = virtio_get_phys(&d->events[i]);
        d->vq.desc[i].len = sizeof(virtio_input_event_t);
        d->vq.desc[i].flags = VIRTQ_DESC_F_WRITE;

        d->vq.avail->ring[d->vq.avail->idx % d->vq.num] = i;
        __asm__ volatile("" ::: "memory");
        d->vq.avail->idx++;
        __asm__ volatile("" ::: "memory");
    }

    d->common_cfg->queue_desc = virtio_get_phys(d->vq.desc);
    d->common_cfg->queue_avail = virtio_get_phys(d->vq.avail);
    d->common_cfg->queue_used = virtio_get_phys(d->vq.used);

    d->notify_off = d->common_cfg->queue_notify_off;
    d->common_cfg->queue_enable = 1;

    /* Notify device that buffers are available. */
    vinput_notify(d);

    d->common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
    return 0;
}

void virtio_input_init(void) {
    for (int i = 0; i < pci_device_count && g_vin_count < VINPUT_MAX_DEVICES; i++) {
        const pci_device_t *dev = &pci_devices[i];
        if (dev->vendor_id != VIRTIO_INPUT_VENDOR_ID) continue;
        if (dev->device_id != VIRTIO_INPUT_DEVICE_MODERN && dev->device_id != VIRTIO_INPUT_DEVICE_LEGACY) continue;

        if (virtio_input_init_device(dev) != 0) continue;
        serial_write(g_vin[g_vin_count].kind == VINPUT_KIND_TABLET
                         ? "[VINPUT] Virtio tablet initialized\n"
                         : "[VINPUT] Virtio keyboard initialized\n");
        g_vin_count++;
    }

    if (g_vin_count == 0) {
        serial_write("[VINPUT] No Virtio-Input device found\n");
    }
}
