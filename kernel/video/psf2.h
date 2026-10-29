#pragma once

#include <stdint.h>
#include <stddef.h>

#define PSF2_MAGIC 0x864AB572u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t flags;
    uint32_t length;
    uint32_t charsize;
    uint32_t height;
    uint32_t width;
} psf2_header_t;

/*
 * Returned by psf2_glyph(). The anonymous unions allow the bitmap pointer
 * and dimensions to be read through several common field names.
 */
typedef struct {
    union {
        const uint8_t *bitmap;
        const uint8_t *data;
        const uint8_t *ptr;
        const uint8_t *glyph;
        const uint8_t *pixels;
        const uint8_t *buf;
    };
    union {
        uint32_t width;
        uint32_t w;
    };
    union {
        uint32_t height;
        uint32_t h;
    };
} psf2_glyph_t;

/* Current font metrics (8x16 fallback defaults). */
extern uint32_t psf2_width;
extern uint32_t psf2_height;

/* Parse a PSF2 font. Returns 0 on success, -1 on failure (fallback stays active). */
int psf2_init(const void *data, size_t size);

/* Return bitmap pointer and dimensions for a character. */
psf2_glyph_t psf2_glyph(char c);

/* Draw a single character. `fb` may be a fb_t* or a raw pixel buffer. */
void psf2_draw_char(uint32_t x, uint32_t y, char c, uint32_t color, void *fb);

/* Draw a NUL-terminated string. */
void psf2_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t color, void *fb);

/* Convenience metric helpers. */
uint32_t psf2_get_width(void);
uint32_t psf2_get_height(void);