#pragma once

/* Integer-only material primitives shared by the compositor and the toolkit:
   separable box blur, anti-aliased rounded-rect coverage, nine-slice drop
   shadows and the translucent glass material.

   Pixels are 0xAARRGGBB. Most surfaces in the system treat the alpha byte as
   "don't care" and are opaque; GfxCanvas.has_alpha selects straight-alpha
   compositing for surfaces whose alpha byte is meaningful. */

#include <stddef.h>
#include <stdint.h>

#define GFX_MAX_CORNER_RADIUS 32
#define GFX_MAX_BLUR_RADIUS   16
#define GFX_BLUR_PASSES       3

typedef struct GfxRect {
    int x, y, w, h;
} GfxRect;

typedef struct GfxCanvas {
    uint32_t *pixels;
    int width;
    int height;
    int stride;     /* in pixels */
    int has_alpha;  /* nonzero: blend with straight-alpha "over" */
} GfxCanvas;

/* Rounded rectangle geometry with the radius clamped to fit the rectangle. */
typedef struct GfxRoundRect {
    int w, h;
    int radius;
    const uint8_t *corner;  /* radius*radius coverage, top-left quadrant */
} GfxRoundRect;

typedef struct GfxShadow {
    int corner_radius;
    int blur_radius;
    int extent;   /* how far the blurred shadow reaches beyond the shape */
    int size;     /* edge length of the square template */
    int center;   /* index of the stretchable middle row/column */
    uint8_t *mask;
} GfxShadow;

typedef struct GfxGlass {
    int blur_radius;
    int corner_radius;
    uint32_t tint;
    unsigned int tint_alpha;
    uint32_t highlight;
    unsigned int highlight_alpha;
} GfxGlass;

int gfx_rect_intersect(const GfxRect *a, const GfxRect *b, GfxRect *out);

/* Linear interpolation of the colour channels; keeps the alpha byte of dst. */
static inline uint32_t gfx_mix(uint32_t dst, uint32_t src, unsigned int alpha) {
    if (alpha >= 255) return (dst & 0xFF000000u) | (src & 0x00FFFFFFu);
    if (alpha == 0) return dst;
    uint32_t inv = 255u - alpha;
    uint32_t rb = (src & 0x00FF00FFu) * alpha + (dst & 0x00FF00FFu) * inv;
    uint32_t g = (src & 0x0000FF00u) * alpha + (dst & 0x0000FF00u) * inv;
    /* x/255 == (x + 1 + (x >> 8)) >> 8 for every x < 65536, per lane. */
    rb = ((rb + 0x00010001u + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    g = ((g + 0x00000100u + ((g >> 8) & 0x0000FF00u)) >> 8) & 0x0000FF00u;
    return (dst & 0xFF000000u) | rb | g;
}

/* Straight-alpha "over": src (with its own alpha byte scaled by alpha). */
uint32_t gfx_over(uint32_t dst, uint32_t src, unsigned int alpha);

void gfx_blend_pixel(const GfxCanvas *canvas, int x, int y, uint32_t rgb, unsigned int alpha);

/* GFX_BLUR_PASSES box passes in each direction approximate a gaussian.
   Edges are clamped. line must hold at least max(w, h) elements. */
void gfx_blur(uint32_t *pixels, int w, int h, int stride, int radius, uint32_t *line);
void gfx_blur_a8(uint8_t *mask, int w, int h, int stride, int radius, uint8_t *line);
int gfx_blur_extent(int radius);

void gfx_round_rect_init(GfxRoundRect *rr, int w, int h, int radius);
/* Coverage 0..255 of pixel (x, y) relative to the rectangle origin. */
unsigned int gfx_round_rect_coverage(const GfxRoundRect *rr, int x, int y);
/* Coverage of the 1px inner border of the rounded rectangle. */
unsigned int gfx_round_rect_edge(const GfxRoundRect *rr, int x, int y);

void gfx_fill_round_rect(const GfxCanvas *canvas, int x, int y, int w, int h,
                         int radius, uint32_t rgb, unsigned int opacity);
void gfx_stroke_round_rect(const GfxCanvas *canvas, int x, int y, int w, int h,
                           int radius, uint32_t rgb, unsigned int opacity);

/* Templates are built once per (corner radius, blur radius) and never
   evicted. Returns 0 for invalid parameters or when memory is exhausted. */
const GfxShadow *gfx_shadow_get(int corner_radius, int blur_radius);
/* Shadow alpha at (u, v) of the (w + 2e) x (h + 2e) box around a w x h shape. */
unsigned int gfx_shadow_sample(const GfxShadow *shadow, int w, int h, int u, int v);
/* Casts the shadow of the rounded shape (x, y, w, h) displaced by
   (offset_x, offset_y). The shape itself is knocked out so the shadow only
   shows outside it; nothing outside clip (canvas coordinates) is touched. */
void gfx_draw_shadow(const GfxCanvas *canvas, const GfxShadow *shadow,
                     int x, int y, int w, int h, int offset_x, int offset_y,
                     uint32_t rgb, unsigned int opacity, const GfxRect *clip);

/* Turns the rectangle into glass: what the canvas already holds around it is
   blurred, tinted and given a 1px highlight along its upper edge. Returns -1
   when scratch memory cannot be obtained; the canvas is then left as is. */
int gfx_glass_apply(const GfxCanvas *canvas, int x, int y, int w, int h, const GfxGlass *glass);

/* FNV-1a over the pixel rows, for deterministic self-tests. */
uint32_t gfx_checksum(const uint32_t *pixels, int w, int h, int stride);
uint32_t gfx_checksum_a8(const uint8_t *mask, int w, int h, int stride);
