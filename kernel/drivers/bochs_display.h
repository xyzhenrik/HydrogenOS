#pragma once

#include <stdint.h>

/* Bochs Graphics Adapter (BGA / "DISPI"): QEMU std VGA, Bochs, VirtualBox VGA. */

typedef struct BochsDisplayMode {
    uint8_t *base;     /* kernel virtual address of the linear framebuffer */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;    /* bytes per scanline */
    uint32_t bpp;
} BochsDisplayMode;

/* Probes the PCI registry for a BGA-compatible adapter, programs the build-time
 * GFX_WIDTH x GFX_HEIGHT x GFX_DEPTH mode (falling back to 1024x768x32), and
 * maps the linear framebuffer. Requires pci_init() to have run.
 * Returns 0 and fills *out on success, or a negative errno value. */
int bochs_display_init(BochsDisplayMode *out);
