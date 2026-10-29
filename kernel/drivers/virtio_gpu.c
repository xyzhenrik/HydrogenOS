#include "virtio_gpu.h"
#include "pci.h"
#include "msi.h"
#include "virtio.h"
#include "serial.h"
#include "paging.h"
#include "kheap.h"
#include <string.h>

typedef struct virtio_gpu_device {
    pci_device_t pci_dev;
    virtio_pci_common_cfg_t *common_cfg;
    uint32_t *notify_cfg;
    uint32_t notify_off_multiplier;
    uint8_t *isr_cfg;
    virtqueue_t vq;
    uint16_t notify_off;

    uint32_t width;
    uint32_t height;
    uint32_t stride;
    void *framebuffer;
    uint32_t resource_id;
    int present;
} virtio_gpu_device_t;

static virtio_gpu_device_t g_vgpu;
static int g_vgpu_present = 0;

static void vgpu_serial_uint(uint32_t v) {
    char buf[16];
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v > 0 && i < 15) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    buf[i] = '\0';
    for (int a = 0, b = i - 1; a < b; ++a, --b) { char t = buf[a]; buf[a] = buf[b]; buf[b] = t; }
    serial_write(buf);
}

static int vgpu_wait_used(virtqueue_t *vq) {
    uint64_t spins = 0;
    while (vq->last_used_idx == vq->used->idx) {
        __asm__ volatile("pause");
        if (++spins > 20000000ULL) return -1;
    }
    return 0;
}

static int vgpu_ctrl(void *req, size_t req_len, void *resp, size_t resp_len) {
    if (!g_vgpu_present) return -1;
    virtqueue_t *vq = &g_vgpu.vq;
    if (vq->num_free < 2) return -1;

    uint16_t head = vq->free_head;
    uint16_t d0 = head;
    uint16_t d1 = vq->desc[head].next;

    vq->desc[d0].addr = virtio_get_phys(req);
    vq->desc[d0].len = (uint32_t)req_len;
    vq->desc[d0].flags = VIRTQ_DESC_F_NEXT;

    vq->desc[d1].addr = virtio_get_phys(resp);
    vq->desc[d1].len = (uint32_t)resp_len;
    vq->desc[d1].flags = VIRTQ_DESC_F_WRITE;
    uint16_t next_free = vq->desc[d1].next;

    vq->free_head = next_free;
    vq->num_free -= 2;

    vq->avail->ring[vq->avail->idx % vq->num] = head;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx++;
    __asm__ volatile("" ::: "memory");

    uintptr_t notify_addr = (uintptr_t)g_vgpu.notify_cfg + ((uintptr_t)g_vgpu.notify_off * g_vgpu.notify_off_multiplier);
    *(volatile uint16_t *)notify_addr = 0;

    if (vgpu_wait_used(vq) != 0) {
        vq->desc[d1].next = vq->free_head;
        vq->free_head = head;
        vq->num_free += 2;
        return -1;
    }
    vq->last_used_idx++;

    vq->desc[d1].next = vq->free_head;
    vq->free_head = head;
    vq->num_free += 2;

    virtio_gpu_ctrl_hdr_t *rhdr = (virtio_gpu_ctrl_hdr_t *)resp;
    if ((rhdr->type & 0xFF00) != 0x1100) return -1;
    return 0;
}

static int vgpu_get_display_info(void) {
    virtio_gpu_ctrl_hdr_t req;
    memset(&req, 0, sizeof(req));
    req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    virtio_gpu_resp_display_info_t resp;
    memset(&resp, 0, sizeof(resp));

    if (vgpu_ctrl(&req, sizeof(req), &resp, sizeof(resp)) != 0) {
        serial_write("[VGPU] GET_DISPLAY_INFO failed\n");
        return -1;
    }

    for (int i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        if (resp.pmodes[i].enabled) {
            g_vgpu.width = resp.pmodes[i].r.width;
            g_vgpu.height = resp.pmodes[i].r.height;
            serial_write("[VGPU] Display ");
            vgpu_serial_uint(g_vgpu.width);
            serial_write("x");
            vgpu_serial_uint(g_vgpu.height);
            serial_write("\n");
            return 0;
        }
    }
    if (resp.pmodes[0].r.width && resp.pmodes[0].r.height) {
        g_vgpu.width = resp.pmodes[0].r.width;
        g_vgpu.height = resp.pmodes[0].r.height;
        return 0;
    }
    serial_write("[VGPU] No display info\n");
    return -1;
}

static int vgpu_setup_fb(void) {
    if (g_vgpu.width == 0 || g_vgpu.height == 0) {
        g_vgpu.width = 1024;
        g_vgpu.height = 768;
    }

    g_vgpu.stride = g_vgpu.width * 4;
    g_vgpu.resource_id = 1;

    virtio_gpu_resource_create_2d_t req_create;
    memset(&req_create, 0, sizeof(req_create));
    req_create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req_create.resource_id = g_vgpu.resource_id;
    req_create.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    req_create.width = g_vgpu.width;
    req_create.height = g_vgpu.height;

    virtio_gpu_ctrl_hdr_t resp;
    memset(&resp, 0, sizeof(resp));
    if (vgpu_ctrl(&req_create, sizeof(req_create), &resp, sizeof(resp)) != 0) {
        serial_write("[VGPU] RESOURCE_CREATE_2D failed\n");
        return -1;
    }

    size_t fb_size = (size_t)g_vgpu.width * g_vgpu.height * 4;
    g_vgpu.framebuffer = kalloc(fb_size);
    if (!g_vgpu.framebuffer) {
        serial_write("[VGPU] framebuffer alloc failed\n");
        return -1;
    }
    memset(g_vgpu.framebuffer, 0, fb_size);

    {
        size_t total = sizeof(virtio_gpu_resource_attach_backing_t) + sizeof(virtio_gpu_mem_entry_t);
        uint8_t *buf = (uint8_t *)kalloc(total);
        memset(buf, 0, total);
        virtio_gpu_resource_attach_backing_t *ab = (virtio_gpu_resource_attach_backing_t *)buf;
        ab->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
        ab->resource_id = g_vgpu.resource_id;
        ab->nr_entries = 1;
        virtio_gpu_mem_entry_t *me = (virtio_gpu_mem_entry_t *)(buf + sizeof(*ab));
        me->addr = virtio_get_phys(g_vgpu.framebuffer);
        me->length = (uint32_t)fb_size;

        virtio_gpu_ctrl_hdr_t resp2;
        memset(&resp2, 0, sizeof(resp2));
        if (vgpu_ctrl(buf, total, &resp2, sizeof(resp2)) != 0) {
            serial_write("[VGPU] ATTACH_BACKING failed\n");
        }
        kfree(buf);
    }

    virtio_gpu_set_scanout_t req_scan;
    memset(&req_scan, 0, sizeof(req_scan));
    req_scan.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    req_scan.r.x = 0;
    req_scan.r.y = 0;
    req_scan.r.width = g_vgpu.width;
    req_scan.r.height = g_vgpu.height;
    req_scan.scanout_id = 0;
    req_scan.resource_id = g_vgpu.resource_id;

    virtio_gpu_ctrl_hdr_t resp3;
    memset(&resp3, 0, sizeof(resp3));
    if (vgpu_ctrl(&req_scan, sizeof(req_scan), &resp3, sizeof(resp3)) != 0) {
        serial_write("[VGPU] SET_SCANOUT failed\n");
        return -1;
    }

    serial_write("[VGPU] Framebuffer ready\n");
    return 0;
}

void virtio_gpu_init(void) {
    pci_device_t dev;
    int found = 0;
    if (pci_find_device(VIRTIO_GPU_VENDOR_ID, VIRTIO_GPU_DEVICE_MODERN, &dev) == 0) {
        found = 1;
    } else if (pci_find_device(VIRTIO_GPU_VENDOR_ID, VIRTIO_GPU_DEVICE_LEGACY, &dev) == 0) {
        found = 1;
    }

    if (!found) {
        serial_write("[VGPU] No Virtio-GPU device found\n");
        return;
    }

    serial_write("[VGPU] Found Virtio-GPU device\n");
    g_vgpu.pci_dev = dev;

    pci_enable_busmaster(&dev);
    pci_enable_mmio(&dev);

    if (pci_find_capability(&dev, PCI_CAP_ID_MSI) != 0) {
        msi_enable(dev);
    }

    uint8_t cap_ptr = pci_find_capability(&dev, 0x09);
    while (cap_ptr != 0) {
        virtio_pci_cap_t cap;
        for (size_t i = 0; i < sizeof(cap); i++) {
            ((uint8_t *)&cap)[i] = pci_config_read8(dev.bus, dev.slot, dev.func, cap_ptr + i);
        }

        void *mapped = (uint8_t *)pci_map_bar(&g_vgpu.pci_dev, cap.bar) + cap.offset;

        switch (cap.cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                g_vgpu.common_cfg = (virtio_pci_common_cfg_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                g_vgpu.notify_cfg = (uint32_t *)mapped;
                g_vgpu.notify_off_multiplier = pci_config_read32(dev.bus, dev.slot, dev.func, cap_ptr + sizeof(virtio_pci_cap_t));
                if (g_vgpu.notify_off_multiplier == 0) g_vgpu.notify_off_multiplier = 1;
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                g_vgpu.isr_cfg = (uint8_t *)mapped;
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

    if (!g_vgpu.common_cfg || !g_vgpu.notify_cfg) {
        serial_write("[VGPU] Failed to find required Virtio capabilities\n");
        return;
    }

    g_vgpu.common_cfg->device_status = 0;
    g_vgpu.common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
    g_vgpu.common_cfg->device_status |= VIRTIO_STATUS_DRIVER;

    g_vgpu.common_cfg->device_feature_select = 0;
    (void)g_vgpu.common_cfg->device_feature;

    g_vgpu.common_cfg->device_feature_select = 1;
    uint32_t features_high = g_vgpu.common_cfg->device_feature;

    g_vgpu.common_cfg->driver_feature_select = 0;
    g_vgpu.common_cfg->driver_feature = 0;
    g_vgpu.common_cfg->driver_feature_select = 1;
    g_vgpu.common_cfg->driver_feature = (features_high & 0x1);

    g_vgpu.common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if (!(g_vgpu.common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_write("[VGPU] Device rejected features\n");
        return;
    }

    g_vgpu.common_cfg->queue_select = 0;
    uint16_t q_size = g_vgpu.common_cfg->queue_size;
    if (q_size == 0) q_size = 64;
    if (virtqueue_setup(&g_vgpu.vq, q_size) != 0) {
        serial_write("[VGPU] Failed to setup virtqueue\n");
        return;
    }

    g_vgpu.common_cfg->queue_desc = virtio_get_phys(g_vgpu.vq.desc);
    g_vgpu.common_cfg->queue_avail = virtio_get_phys(g_vgpu.vq.avail);
    g_vgpu.common_cfg->queue_used = virtio_get_phys(g_vgpu.vq.used);
    g_vgpu.notify_off = g_vgpu.common_cfg->queue_notify_off;
    g_vgpu.common_cfg->queue_enable = 1;

    g_vgpu.common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;

    g_vgpu_present = 1;
    g_vgpu.present = 1;

    serial_write("[VGPU] Virtio-GPU initialized\n");

    if (vgpu_get_display_info() == 0) {
        vgpu_setup_fb();
    }
}

void virtio_gpu_present(void) {
    if (!g_vgpu_present || !g_vgpu.framebuffer) return;

    virtio_gpu_transfer_to_host_2d_t req_tr;
    memset(&req_tr, 0, sizeof(req_tr));
    req_tr.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    req_tr.r.x = 0;
    req_tr.r.y = 0;
    req_tr.r.width = g_vgpu.width;
    req_tr.r.height = g_vgpu.height;
    req_tr.offset = 0;
    req_tr.resource_id = g_vgpu.resource_id;

    virtio_gpu_ctrl_hdr_t resp;
    memset(&resp, 0, sizeof(resp));
    if (vgpu_ctrl(&req_tr, sizeof(req_tr), &resp, sizeof(resp)) != 0) {
        serial_write("[VGPU] TRANSFER failed\n");
    }

    virtio_gpu_resource_flush_t req_fl;
    memset(&req_fl, 0, sizeof(req_fl));
    req_fl.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    req_fl.r.x = 0;
    req_fl.r.y = 0;
    req_fl.r.width = g_vgpu.width;
    req_fl.r.height = g_vgpu.height;
    req_fl.resource_id = g_vgpu.resource_id;

    virtio_gpu_ctrl_hdr_t resp2;
    memset(&resp2, 0, sizeof(resp2));
    if (vgpu_ctrl(&req_fl, sizeof(req_fl), &resp2, sizeof(resp2)) != 0) {
        serial_write("[VGPU] FLUSH failed\n");
    }
}

uint32_t virtio_gpu_width(void) {
    return g_vgpu.width;
}

uint32_t virtio_gpu_height(void) {
    return g_vgpu.height;
}

uint32_t virtio_gpu_stride(void) {
    return g_vgpu.stride;
}

void *virtio_gpu_framebuffer(void) {
    return g_vgpu.framebuffer;
}