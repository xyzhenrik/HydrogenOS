#include "psf2.h"
#include "font8x16.h"
#include "framebuffer.h"

#include <stddef.h>

uint32_t psf2_width = 8;
uint32_t psf2_height = 16;

static int psf2_loaded = 0;
static const uint8_t *psf2_glyphs = NULL;
static uint32_t psf2_num_glyphs = 0;
static uint32_t psf2_bytes_per_glyph = 0;
static uint32_t psf2_header_size = 0;

static uint32_t read32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int psf2_init(const void *data, size_t size) {
    psf2_loaded = 0;
    psf2_glyphs = NULL;
    psf2_num_glyphs = 0;
    psf2_bytes_per_glyph = 0;
    psf2_header_size = 0;
    psf2_width = 8;
    psf2_height = 16;

    if (!data || size < 32) {
        return -1;
    }

    const uint8_t *d = (const uint8_t *)data;

    uint32_t magic = read32_le(d);
    if (magic != PSF2_MAGIC) {
        return -1;
    }

    uint32_t version = read32_le(d + 4);
    uint32_t header_size = read32_le(d + 8);
    uint32_t flags = read32_le(d + 12);
    uint32_t num_glyphs = read32_le(d + 16);
    uint32_t bytes_per_glyph = read32_le(d + 20);
    uint32_t height = read32_le(d + 24);
    uint32_t width = read32_le(d + 28);

    (void)version;
    (void)flags;

    if (header_size < 32 || header_size > size) {
        return -1;
    }
    if (num_glyphs == 0 || width == 0 || height == 0 || bytes_per_glyph == 0) {
        return -1;
    }

    uint32_t min_bytes = height * ((width + 7u) / 8u);
    if (bytes_per_glyph < min_bytes) {
        return -1;
    }

    size_t glyph_bytes = (size_t)num_glyphs * (size_t)bytes_per_glyph;
    if ((size_t)header_size + glyph_bytes > size) {
        return -1;
    }

    psf2_header_size = header_size;
    psf2_num_glyphs = num_glyphs;
    psf2_bytes_per_glyph = bytes_per_glyph;
    psf2_width = width;
    psf2_height = height;
    psf2_glyphs = d + header_size;
    psf2_loaded = 1;

    return 0;
}

psf2_glyph_t psf2_glyph(char c) {
    psf2_glyph_t g;
    if (psf2_loaded && psf2_glyphs) {
        unsigned char uc = (unsigned char)c;
        if (uc >= psf2_num_glyphs) {
            uc = 0;
        }
        g.bitmap = psf2_glyphs + (size_t)uc * (size_t)psf2_bytes_per_glyph;
        g.width = psf2_width;
        g.height = psf2_height;
    } else {
        g.bitmap = font8x16[(unsigned char)c];
        g.width = 8;
        g.height = 16;
    }
    return g;
}

static void resolve_fb(void *fb, uint8_t **out_addr, uint32_t *out_w,
                       uint32_t *out_h, uint32_t *out_pitch) {
    if (fb) {
        fb_t *f = (fb_t *)fb;
        if (f->addr && f->width > 0 && f->height > 0 &&
            f->pitch >= f->width * 4u) {
            *out_addr = f->addr;
            *out_w = f->width;
            *out_h = f->height;
            *out_pitch = f->pitch;
            return;
        }
        /* Treat as a raw pixel buffer; borrow geometry from the global fb if possible. */
        *out_addr = (uint8_t *)fb;
        if (framebuffer.width > 0 && framebuffer.height > 0 &&
            framebuffer.pitch >= framebuffer.width * 4u) {
            *out_w = framebuffer.width;
            *out_h = framebuffer.height;
            *out_pitch = framebuffer.pitch;
        } else {
            *out_w = 800;
            *out_h = 600;
            *out_pitch = 800 * 4u;
        }
        return;
    }

    *out_addr = framebuffer.addr;
    *out_w = framebuffer.width;
    *out_h = framebuffer.height;
    *out_pitch = framebuffer.pitch;
    if (*out_pitch == 0 && *out_w > 0) {
        *out_pitch = *out_w * 4u;
    }
}

void psf2_draw_char(uint32_t x, uint32_t y, char c, uint32_t color, void *fb) {
    psf2_glyph_t g = psf2_glyph(c);

    uint8_t *addr;
    uint32_t fw, fh, pitch;
    resolve_fb(fb, &addr, &fw, &fh, &pitch);
    if (!addr || fw == 0 || fh == 0 || pitch == 0) {
        return;
    }

    uint32_t bytes_per_row = (g.width + 7u) / 8u;
    if (bytes_per_row == 0) {
        bytes_per_row = 1;
    }

    for (uint32_t row = 0; row < g.height; ++row) {
        uint32_t py = y + row;
        if (py >= fh) {
            break;
        }
        for (uint32_t col = 0; col < g.width; ++col) {
            uint32_t px = x + col;
            if (px >= fw) {
                break;
            }
            uint8_t byte = g.bitmap[row * bytes_per_row + (col / 8u)];
            uint8_t bit = (byte >> (7u - (col % 8u))) & 1u;
            if (bit) {
                uint32_t *pixel = (uint32_t *)(addr + py * pitch + px * 4u);
                *pixel = color;
            }
        }
    }
}

void psf2_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t color, void *fb) {
    if (!s) {
        return;
    }
    uint32_t cx = x;
    uint32_t cy = y;
    uint32_t w = psf2_loaded ? psf2_width : 8;
    uint32_t h = psf2_loaded ? psf2_height : 16;

    /* Resolve once to get dimensions for newline handling; drawing uses resolve per char. */
    uint8_t *addr;
    uint32_t fw, fh, pitch;
    resolve_fb(fb, &addr, &fw, &fh, &pitch);
    (void)addr;
    (void)fw;
    (void)fh;
    (void)pitch;

    while (*s) {
        if (*s == '\n') {
            cx = x;
            cy += h;
        } else if (*s == '\r') {
            cx = x;
        } else if (*s == '\t') {
            cx += w * 4u;
        } else {
            psf2_draw_char(cx, cy, *s, color, fb);
            cx += w;
        }
        ++s;
    }
}

uint32_t psf2_get_width(void) {
    return psf2_loaded ? psf2_width : 8u;
}

uint32_t psf2_get_height(void) {
    return psf2_loaded ? psf2_height : 16u;
}