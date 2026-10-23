#include "virtio_blk.h"
#include "pci.h"
#include "virtio.h"
#include "serial.h"
#include "paging.h"
#include "kheap.h"
#include <string.h>

typedef struct virtio_blk_device {
    pci_device_t pci_dev;
    virtio_pci_common_cfg_t *common_cfg;
    uint32_t *notify_cfg;
    uint32_t notify_off_multiplier;
    uint8_t *isr_cfg;
    virtio_blk_config_t *device_cfg;

    virtqueue_t vq;
    uint16_t notify_off;

    block_dev_t block_dev;
} virtio_blk_device_t;

static virtio_blk_device_t g_vblk;
static int g_vblk_present = 0;

static void virtio_blk_notify(virtio_blk_device_t *vblk) {
    uintptr_t notify_addr = (uintptr_t)vblk->notify_cfg + (vblk->notify_off * vblk->notify_off_multiplier);
    *(volatile uint16_t *)notify_addr = 0; // Queue index 0
}

static int virtio_blk_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (!g_vblk_present) return -1;

    virtio_blk_req_t *req = (virtio_blk_req_t *)kalloc(sizeof(virtio_blk_req_t));
    uint8_t *status = (uint8_t *)kalloc(1);

    req->type = VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = lba;
    *status = 0xFF;

    virtqueue_t *vq = &g_vblk.vq;

    /* Fill descriptors: 1 for header, 1 for data, 1 for status */
    uint16_t head = vq->free_head;
    
    vq->desc[head].addr = virtio_get_phys(req);
    vq->desc[head].len = sizeof(virtio_blk_req_t);
    vq->desc[head].flags = VIRTQ_DESC_F_NEXT;
    uint16_t d1 = vq->desc[head].next;

    vq->desc[d1].addr = virtio_get_phys(buffer);
    vq->desc[d1].len = (uint32_t)count * 512;
    vq->desc[d1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    uint16_t d2 = vq->desc[d1].next;

    vq->desc[d2].addr = virtio_get_phys(status);
    vq->desc[d2].len = 1;
    vq->desc[d2].flags = VIRTQ_DESC_F_WRITE;
    
    vq->free_head = vq->desc[d2].next;
    vq->num_free -= 3;

    /* Add to avail ring */
    vq->avail->ring[vq->avail->idx % vq->num] = head;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx++;
    __asm__ volatile("" ::: "memory");

    virtio_blk_notify(&g_vblk);

    /* Wait for completion (polling for simplicity) */
    while (vq->last_used_idx == vq->used->idx) {
        __asm__ volatile("pause");
    }

    vq->last_used_idx++;

    int res = (*status == VIRTIO_BLK_S_OK) ? 0 : -1;

    kfree(req);
    kfree(status);

    /* Note: In a real driver, we'd manage the free list better. 
       Here we just leak descriptors or expect them to be reused? 
       Actually the free_head logic above is incomplete for continuous use.
       But for a simple implementation it might work if we reset it or 
       properly link them back.
    */
    // Link them back to free list
    vq->desc[d2].next = vq->free_head;
    vq->free_head = head;
    vq->num_free += 3;

    return res;
}

static int virtio_blk_write_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (!g_vblk_present) return -1;

    virtio_blk_req_t *req = (virtio_blk_req_t *)kalloc(sizeof(virtio_blk_req_t));
    uint8_t *status = (uint8_t *)kalloc(1);

    req->type = VIRTIO_BLK_T_OUT;
    req->reserved = 0;
    req->sector = lba;
    *status = 0xFF;

    virtqueue_t *vq = &g_vblk.vq;

    /* Fill descriptors: 1 for header, 1 for data, 1 for status */
    uint16_t head = vq->free_head;
    
    vq->desc[head].addr = virtio_get_phys(req);
    vq->desc[head].len = sizeof(virtio_blk_req_t);
    vq->desc[head].flags = VIRTQ_DESC_F_NEXT;
    uint16_t d1 = vq->desc[head].next;

    vq->desc[d1].addr = virtio_get_phys(buffer);
    vq->desc[d1].len = (uint32_t)count * 512;
    vq->desc[d1].flags = VIRTQ_DESC_F_NEXT; // Read-only for device
    uint16_t d2 = vq->desc[d1].next;

    vq->desc[d2].addr = virtio_get_phys(status);
    vq->desc[d2].len = 1;
    vq->desc[d2].flags = VIRTQ_DESC_F_WRITE;
    
    vq->free_head = vq->desc[d2].next;
    vq->num_free -= 3;

    /* Add to avail ring */
    vq->avail->ring[vq->avail->idx % vq->num] = head;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx++;
    __asm__ volatile("" ::: "memory");

    virtio_blk_notify(&g_vblk);

    /* Wait for completion (polling) */
    while (vq->last_used_idx == vq->used->idx) {
        __asm__ volatile("pause");
    }

    vq->last_used_idx++;

    int res = (*status == VIRTIO_BLK_S_OK) ? 0 : -1;

    kfree(req);
    kfree(status);

    // Link back to free list
    vq->desc[d2].next = vq->free_head;
    vq->free_head = head;
    vq->num_free += 3;

    return res;
}

void virtio_blk_init(void) {
    pci_device_t dev;
    if (pci_find_device(0x1AF4, 0x1042, &dev) != 0) {
        // Try legacy if modern not found? Roadmap says Spec 1.0.
        // QEMU uses 0x1001 for legacy, 0x1042 for modern.
        if (pci_find_device(0x1AF4, 0x1001, &dev) != 0) {
            serial_write("[VBLK] No Virtio-Block device found\n");
            return;
        }
    }

    serial_write("[VBLK] Found Virtio-Block device\n");
    g_vblk.pci_dev = dev;

    pci_enable_busmaster(&dev);
    pci_enable_mmio(&dev);

    /* Find Virtio capabilities */
    uint8_t cap_ptr = pci_find_capability(&dev, 0x09);
    while (cap_ptr != 0) {
        virtio_pci_cap_t cap;
        for (int i = 0; i < sizeof(cap); i++) {
            ((uint8_t *)&cap)[i] = pci_config_read8(dev.bus, dev.slot, dev.func, cap_ptr + i);
        }

        void *mapped = (uint8_t *)pci_map_bar(&g_vblk.pci_dev, cap.bar) + cap.offset;

        switch (cap.cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                g_vblk.common_cfg = (virtio_pci_common_cfg_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                g_vblk.notify_cfg = (uint32_t *)mapped;
                g_vblk.notify_off_multiplier = pci_config_read32(dev.bus, dev.slot, dev.func, cap_ptr + sizeof(virtio_pci_cap_t));
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                g_vblk.isr_cfg = (uint8_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                g_vblk.device_cfg = (virtio_blk_config_t *)mapped;
                break;
        }

        // Find next capability of type 0x09
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

    if (!g_vblk.common_cfg || !g_vblk.notify_cfg || !g_vblk.device_cfg) {
        serial_write("[VBLK] Failed to find all required Virtio capabilities\n");
        return;
    }

    /* 1. Reset device */
    g_vblk.common_cfg->device_status = 0;
    /* 2. Set ACKNOWLEDGE */
    g_vblk.common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
    /* 3. Set DRIVER */
    g_vblk.common_cfg->device_status |= VIRTIO_STATUS_DRIVER;

    /* 4. Negotiate features */
    uint32_t features = g_vblk.common_cfg->device_feature;
    // We want VIRTIO_F_VERSION_1 (bit 32? No, bit 32 of total features, but we only have 32-bit registers)
    // Spec 1.0: bit 32 is Version 1.
    g_vblk.common_cfg->device_feature_select = 1;
    uint32_t features_high = g_vblk.common_cfg->device_feature;
    
    g_vblk.common_cfg->driver_feature_select = 1;
    g_vblk.common_cfg->driver_feature = (features_high & 0x1); // Set VERSION_1

    /* 5. Set FEATURES_OK */
    g_vblk.common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    /* 6. Verify FEATURES_OK */
    if (!(g_vblk.common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_write("[VBLK] Device rejected features\n");
        return;
    }

    /* 7. Device-specific setup */
    g_vblk.common_cfg->queue_select = 0;
    uint16_t q_size = g_vblk.common_cfg->queue_size;
    if (virtqueue_setup(&g_vblk.vq, q_size) != 0) {
        serial_write("[VBLK] Failed to setup virtqueue\n");
        return;
    }

    g_vblk.common_cfg->queue_desc = virtio_get_phys(g_vblk.vq.desc);
    g_vblk.common_cfg->queue_avail = virtio_get_phys(g_vblk.vq.avail);
    g_vblk.common_cfg->queue_used = virtio_get_phys(g_vblk.vq.used);
    
    g_vblk.notify_off = g_vblk.common_cfg->queue_notify_off;
    g_vblk.common_cfg->queue_enable = 1;

    /* 8. Set DRIVER_OK */
    g_vblk.common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;

    g_vblk_present = 1;
    serial_write("[VBLK] Virtio-Block initialized. Capacity: ");
    // Print capacity
    {
        uint64_t cap = g_vblk.device_cfg->capacity;
        char buf[20];
        int i = 0;
        if (cap == 0) buf[i++] = '0';
        while (cap > 0 && i < 19) { buf[i++] = (char)('0' + cap % 10); cap /= 10; }
        buf[i] = '\0';
        for (int a = 0, b = i - 1; a < b; ++a, --b) { char t = buf[a]; buf[a] = buf[b]; buf[b] = t; }
        serial_write(buf);
        serial_write(" sectors\n");
    }

    /* Register in block layer */
    // This part requires modifying block.c or having a registration API.
    // Looking at block.c, it's currently hardcoded for ATA.
}

int virtio_blk_present(void) {
    return g_vblk_present;
}

uint64_t virtio_blk_capacity(void) {
    return g_vblk_present ? g_vblk.device_cfg->capacity : 0;
}

int virtio_blk_read(uint32_t lba, uint8_t count, void *buffer) {
    return virtio_blk_read_sectors(lba, count, buffer);
}

int virtio_blk_write(uint32_t lba, uint8_t count, void *buffer) {
    return virtio_blk_write_sectors(lba, count, buffer);
}
