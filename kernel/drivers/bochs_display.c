#include "bochs_display.h"
#include "pci.h"
#include "io.h"
#include "serial.h"
#include "errno.h"

#include <stddef.h>

#ifndef GFX_WIDTH
#define GFX_WIDTH 1024
#endif
#ifndef GFX_HEIGHT
#define GFX_HEIGHT 768
#endif
#ifndef GFX_DEPTH
#define GFX_DEPTH 32
#endif

#define BGA_FALLBACK_WIDTH  1024
#define BGA_FALLBACK_HEIGHT 768
#define BGA_DEPTH           32

#define BGA_PCI_VENDOR_QEMU 0x1234
#define BGA_PCI_DEVICE_QEMU 0x1111
#define BGA_PCI_VENDOR_VBOX 0x80EE
#define BGA_PCI_DEVICE_VBOX 0xBEEF

#define BGA_IOPORT_INDEX 0x01CE
#define BGA_IOPORT_DATA  0x01CF

#define BGA_INDEX_ID          0x0
#define BGA_INDEX_XRES        0x1
#define BGA_INDEX_YRES        0x2
#define BGA_INDEX_BPP         0x3
#define BGA_INDEX_ENABLE      0x4
#define BGA_INDEX_BANK        0x5
#define BGA_INDEX_VIRT_WIDTH  0x6
#define BGA_INDEX_VIRT_HEIGHT 0x7
#define BGA_INDEX_X_OFFSET    0x8
#define BGA_INDEX_Y_OFFSET    0x9

/* 0xB0C2 is the first interface revision with 32 bpp support. */
#define BGA_ID_MIN 0xB0C2
#define BGA_ID_MAX 0xB0C5

#define BGA_ENABLE_DISABLED 0x00
#define BGA_ENABLE_ENABLED  0x01
#define BGA_ENABLE_GETCAPS  0x02
#define BGA_ENABLE_LFB      0x40

#define BGA_LFB_BAR 0

typedef struct BgaCaps {
    uint32_t max_width;  /* 0 when the adapter cannot report capabilities */
    uint32_t max_height;
    uint32_t max_bpp;
} BgaCaps;

static pci_device_t bga_pci;

static void bga_write(uint16_t index, uint16_t value) {
    outw(BGA_IOPORT_INDEX, index);
    outw(BGA_IOPORT_DATA, value);
}

static uint16_t bga_read(uint16_t index) {
    outw(BGA_IOPORT_INDEX, index);
    return inw(BGA_IOPORT_DATA);
}

/* The adapter latches the highest interface revision it accepts; probing
 * downwards finds the newest one both sides support. */
static int bga_negotiate_id(void) {
    for (uint16_t id = BGA_ID_MAX; id >= BGA_ID_MIN; --id) {
        bga_write(BGA_INDEX_ID, id);
        if (bga_read(BGA_INDEX_ID) == id)
            return 0;
    }
    return -EIO;
}

static void bga_read_caps(BgaCaps *caps) {
    uint16_t saved_enable = bga_read(BGA_INDEX_ENABLE);
    bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_GETCAPS);
    caps->max_width = bga_read(BGA_INDEX_XRES);
    caps->max_height = bga_read(BGA_INDEX_YRES);
    caps->max_bpp = bga_read(BGA_INDEX_BPP);
    bga_write(BGA_INDEX_ENABLE, saved_enable);
}

static int mode_within_caps(const BgaCaps *caps, uint32_t width,
                            uint32_t height, uint32_t bpp) {
    if (caps->max_width == 0 || caps->max_height == 0 || caps->max_bpp == 0)
        return 1;
    return width <= caps->max_width && height <= caps->max_height &&
           bpp <= caps->max_bpp;
}

/* Programs the mode and reads it back; the adapter silently clamps or
 * ignores values it cannot honour, so only the read-back is authoritative. */
static int bga_set_mode(uint32_t width, uint32_t height, uint32_t bpp,
                        uint32_t vram_size, uint32_t *pitch_out) {
    if (width == 0 || height == 0 || width > 0xFFFF || height > 0xFFFF)
        return -EINVAL;
    if ((uint64_t)width * height * (bpp / 8) > vram_size)
        return -ENOSPC;

    bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
    bga_write(BGA_INDEX_XRES, (uint16_t)width);
    bga_write(BGA_INDEX_YRES, (uint16_t)height);
    bga_write(BGA_INDEX_BPP, (uint16_t)bpp);
    bga_write(BGA_INDEX_VIRT_WIDTH, (uint16_t)width);
    bga_write(BGA_INDEX_X_OFFSET, 0);
    bga_write(BGA_INDEX_Y_OFFSET, 0);
    bga_write(BGA_INDEX_BANK, 0);
    bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_ENABLED | BGA_ENABLE_LFB);

    if (bga_read(BGA_INDEX_XRES) != width ||
        bga_read(BGA_INDEX_YRES) != height ||
        bga_read(BGA_INDEX_BPP) != bpp ||
        bga_read(BGA_INDEX_X_OFFSET) != 0 ||
        bga_read(BGA_INDEX_Y_OFFSET) != 0 ||
        bga_read(BGA_INDEX_VIRT_HEIGHT) < height) {
        bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
        return -EINVAL;
    }

    uint32_t virt_width = bga_read(BGA_INDEX_VIRT_WIDTH);
    if (virt_width < width) {
        bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
        return -EINVAL;
    }
    uint32_t pitch = virt_width * (bpp / 8);
    if ((uint64_t)pitch * height > vram_size) {
        bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
        return -ENOSPC;
    }
    *pitch_out = pitch;
    return 0;
}

static int try_mode(const BgaCaps *caps, uint32_t width, uint32_t height,
                    uint32_t bpp, uint32_t vram_size, uint32_t *pitch_out) {
    if (bpp != BGA_DEPTH || !mode_within_caps(caps, width, height, bpp))
        return -EINVAL;
    return bga_set_mode(width, height, bpp, vram_size, pitch_out);
}

static int bga_find_device(pci_device_t *out) {
    if (pci_find_device(BGA_PCI_VENDOR_QEMU, BGA_PCI_DEVICE_QEMU, out) == 0)
        return 0;
    if (pci_find_device(BGA_PCI_VENDOR_VBOX, BGA_PCI_DEVICE_VBOX, out) == 0)
        return 0;
    return -ENOENT;
}

static void serial_write_dec(uint32_t value) {
    char buf[11];
    size_t i = sizeof(buf);
    buf[--i] = '\0';
    do {
        buf[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    serial_write(&buf[i]);
}

int bochs_display_init(BochsDisplayMode *out) {
    if (!out)
        return -EINVAL;

    if (bga_find_device(&bga_pci) != 0)
        return -ENOENT;
    if (bga_pci.bar_is_io[BGA_LFB_BAR] || bga_pci.bar_size[BGA_LFB_BAR] == 0) {
        serial_write("[BGA] adapter has no usable LFB BAR\n");
        return -EIO;
    }
    if (bga_negotiate_id() != 0) {
        serial_write("[BGA] DISPI interface not available\n");
        return -EIO;
    }

    pci_enable_mmio(&bga_pci);

    BgaCaps caps;
    bga_read_caps(&caps);

    uint32_t vram_size = bga_pci.bar_size[BGA_LFB_BAR];
    uint32_t width = GFX_WIDTH;
    uint32_t height = GFX_HEIGHT;
    uint32_t pitch = 0;
    int rc = try_mode(&caps, width, height, GFX_DEPTH, vram_size, &pitch);
    if (rc != 0) {
        width = BGA_FALLBACK_WIDTH;
        height = BGA_FALLBACK_HEIGHT;
        rc = try_mode(&caps, width, height, BGA_DEPTH, vram_size, &pitch);
    }
    if (rc != 0) {
        serial_write("[BGA] no supported display mode\n");
        return rc;
    }

    uint8_t *base = (uint8_t *)pci_map_bar(&bga_pci, BGA_LFB_BAR);
    if (!base) {
        bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
        return -ENOMEM;
    }

    out->base = base;
    out->width = width;
    out->height = height;
    out->pitch = pitch;
    out->bpp = BGA_DEPTH;

    serial_write("[BGA] mode ");
    serial_write_dec(width);
    serial_write("x");
    serial_write_dec(height);
    serial_write("x");
    serial_write_dec(BGA_DEPTH);
    serial_write("\n");
    return 0;
}
