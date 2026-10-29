#include "gfx_effects.h"

void *malloc(size_t size);
void free(void *ptr);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

#define GFX_CORNER_SUBSAMPLES 8
#define GFX_SHADOW_CACHE_SLOTS 8

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int gfx_rect_intersect(const GfxRect *a, const GfxRect *b, GfxRect *out) {
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    if (a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0 || x1 <= x0 || y1 <= y0) {
        out->x = out->y = out->w = out->h = 0;
        return 0;
    }
    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
    return 1;
}

uint32_t gfx_over(uint32_t dst, uint32_t src, unsigned int alpha) {
    if (alpha > 255) alpha = 255;
    uint32_t sa = (((src >> 24) & 0xFFu) * alpha + 127u) / 255u;
    if (sa == 0) return dst;
    uint32_t da = (dst >> 24) & 0xFFu;
    if (sa == 255 || da == 0) return (sa << 24) | (src & 0x00FFFFFFu);
    uint32_t dw = da * (255u - sa);              /* dst weight, scaled by 255 */
    uint32_t out_a255 = sa * 255u + dw;          /* out alpha, scaled by 255 */
    uint32_t half = out_a255 / 2u;
    uint32_t r = (((src >> 16) & 0xFFu) * sa * 255u + ((dst >> 16) & 0xFFu) * dw + half) / out_a255;
    uint32_t g = (((src >> 8) & 0xFFu) * sa * 255u + ((dst >> 8) & 0xFFu) * dw + half) / out_a255;
    uint32_t b = ((src & 0xFFu) * sa * 255u + (dst & 0xFFu) * dw + half) / out_a255;
    uint32_t a = (out_a255 + 127u) / 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void gfx_blend_pixel(const GfxCanvas *canvas, int x, int y, uint32_t rgb, unsigned int alpha) {
    if (alpha == 0 || x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) return;
    uint32_t *p = &canvas->pixels[(size_t)y * (size_t)canvas->stride + (size_t)x];
    if (canvas->has_alpha)
        *p = gfx_over(*p, 0xFF000000u | rgb, alpha);
    else
        *p = gfx_mix(*p, rgb, alpha);
}

/* ---------------------------------------------------------------- blur */

int gfx_blur_extent(int radius) {
    return clamp_int(radius, 0, GFX_MAX_BLUR_RADIUS) * GFX_BLUR_PASSES;
}

/* One box pass over n samples read from src and written with dst_step.
   Running sums keep the cost independent of the radius; the reciprocal turns
   the division into a multiply (sum * inv >> 16 never exceeds 255 because
   inv <= 65536 / (2r + 1)). */
static void blur_line_argb(uint32_t *dst, size_t dst_step, const uint32_t *src, int n, int radius) {
    uint32_t inv = 65536u / (uint32_t)(2 * radius + 1);
    uint32_t sa = 0, sr = 0, sg = 0, sb = 0;
    for (int k = -radius; k <= radius; ++k) {
        uint32_t p = src[clamp_int(k, 0, n - 1)];
        sa += p >> 24;
        sr += (p >> 16) & 0xFFu;
        sg += (p >> 8) & 0xFFu;
        sb += p & 0xFFu;
    }
    for (int i = 0; i < n; ++i) {
        dst[(size_t)i * dst_step] = (((sa * inv + 32768u) >> 16) << 24) |
                                    (((sr * inv + 32768u) >> 16) << 16) |
                                    (((sg * inv + 32768u) >> 16) << 8) |
                                    ((sb * inv + 32768u) >> 16);
        uint32_t in = src[i + radius + 1 < n ? i + radius + 1 : n - 1];
        uint32_t out = src[i - radius > 0 ? i - radius : 0];
        sa += in >> 24;
        sr += (in >> 16) & 0xFFu;
        sg += (in >> 8) & 0xFFu;
        sb += in & 0xFFu;
        sa -= out >> 24;
        sr -= (out >> 16) & 0xFFu;
        sg -= (out >> 8) & 0xFFu;
        sb -= out & 0xFFu;
    }
}

static void blur_line_a8(uint8_t *dst, size_t dst_step, const uint8_t *src, int n, int radius) {
    uint32_t inv = 65536u / (uint32_t)(2 * radius + 1);
    uint32_t sum = 0;
    for (int k = -radius; k <= radius; ++k) sum += src[clamp_int(k, 0, n - 1)];
    for (int i = 0; i < n; ++i) {
        dst[(size_t)i * dst_step] = (uint8_t)((sum * inv + 32768u) >> 16);
        sum += src[i + radius + 1 < n ? i + radius + 1 : n - 1];
        sum -= src[i - radius > 0 ? i - radius : 0];
    }
}

void gfx_blur(uint32_t *pixels, int w, int h, int stride, int radius, uint32_t *line) {
    if (!pixels || !line || w <= 0 || h <= 0 || stride < w || radius <= 0) return;
    radius = clamp_int(radius, 1, GFX_MAX_BLUR_RADIUS);
    for (int pass = 0; pass < GFX_BLUR_PASSES; ++pass) {
        for (int y = 0; y < h; ++y) {
            uint32_t *row = pixels + (size_t)y * (size_t)stride;
            memcpy(line, row, (size_t)w * sizeof(uint32_t));
            blur_line_argb(row, 1, line, w, radius);
        }
        for (int x = 0; x < w; ++x) {
            uint32_t *col = pixels + x;
            for (int y = 0; y < h; ++y) line[y] = col[(size_t)y * (size_t)stride];
            blur_line_argb(col, (size_t)stride, line, h, radius);
        }
    }
}

void gfx_blur_a8(uint8_t *mask, int w, int h, int stride, int radius, uint8_t *line) {
    if (!mask || !line || w <= 0 || h <= 0 || stride < w || radius <= 0) return;
    radius = clamp_int(radius, 1, GFX_MAX_BLUR_RADIUS);
    for (int pass = 0; pass < GFX_BLUR_PASSES; ++pass) {
        for (int y = 0; y < h; ++y) {
            uint8_t *row = mask + (size_t)y * (size_t)stride;
            memcpy(line, row, (size_t)w);
            blur_line_a8(row, 1, line, w, radius);
        }
        for (int x = 0; x < w; ++x) {
            uint8_t *col = mask + x;
            for (int y = 0; y < h; ++y) line[y] = col[(size_t)y * (size_t)stride];
            blur_line_a8(col, (size_t)stride, line, h, radius);
        }
    }
}

/* ------------------------------------------------------ rounded corners */

/* Corner masks for every radius live side by side: radius r starts at
   sum(k^2, k < r) = (r-1) r (2r-1) / 6. Built lazily, never evicted. */
#define GFX_CORNER_STORAGE \
    ((GFX_MAX_CORNER_RADIUS * (GFX_MAX_CORNER_RADIUS + 1) * (2 * GFX_MAX_CORNER_RADIUS + 1)) / 6)

static uint8_t gfx_corner_storage[GFX_CORNER_STORAGE];
static uint8_t gfx_corner_ready[GFX_MAX_CORNER_RADIUS + 1];

static const uint8_t *corner_mask(int radius) {
    if (radius <= 0 || radius > GFX_MAX_CORNER_RADIUS) return 0;
    uint8_t *mask = gfx_corner_storage + ((radius - 1) * radius * (2 * radius - 1)) / 6;
    if (gfx_corner_ready[radius]) return mask;

    /* The arc is centred on the inner corner (radius, radius); samples sit at
       subpixel centres, all in units of 1 / (2 * GFX_CORNER_SUBSAMPLES). */
    const int scale = 2 * GFX_CORNER_SUBSAMPLES;
    const int64_t center = (int64_t)scale * radius;
    const int64_t limit = center * center;
    for (int py = 0; py < radius; ++py) {
        for (int px = 0; px < radius; ++px) {
            int inside = 0;
            for (int sy = 0; sy < GFX_CORNER_SUBSAMPLES; ++sy) {
                int64_t dy = center - (int64_t)(scale * py + 2 * sy + 1);
                for (int sx = 0; sx < GFX_CORNER_SUBSAMPLES; ++sx) {
                    int64_t dx = center - (int64_t)(scale * px + 2 * sx + 1);
                    if (dx * dx + dy * dy <= limit) ++inside;
                }
            }
            const int total = GFX_CORNER_SUBSAMPLES * GFX_CORNER_SUBSAMPLES;
            mask[py * radius + px] = (uint8_t)((inside * 255 + total / 2) / total);
        }
    }
    gfx_corner_ready[radius] = 1;
    return mask;
}

void gfx_round_rect_init(GfxRoundRect *rr, int w, int h, int radius) {
    rr->w = w > 0 ? w : 0;
    rr->h = h > 0 ? h : 0;
    int max_radius = (rr->w < rr->h ? rr->w : rr->h) / 2;
    if (radius > max_radius) radius = max_radius;
    if (radius > GFX_MAX_CORNER_RADIUS) radius = GFX_MAX_CORNER_RADIUS;
    if (radius < 0) radius = 0;
    rr->radius = radius;
    rr->corner = corner_mask(radius);
    if (!rr->corner) rr->radius = 0;
}

unsigned int gfx_round_rect_coverage(const GfxRoundRect *rr, int x, int y) {
    if (x < 0 || y < 0 || x >= rr->w || y >= rr->h) return 0;
    int r = rr->radius;
    if (r == 0) return 255;
    int cx = x < r ? x : (x >= rr->w - r ? rr->w - 1 - x : -1);
    int cy = y < r ? y : (y >= rr->h - r ? rr->h - 1 - y : -1);
    if (cx < 0 || cy < 0) return 255;
    return rr->corner[cy * r + cx];
}

unsigned int gfx_round_rect_edge(const GfxRoundRect *rr, int x, int y) {
    int r = rr->radius;
    int on_border = x == 0 || y == 0 || x == rr->w - 1 || y == rr->h - 1;
    int in_corner = (x <= r || x >= rr->w - 1 - r) && (y <= r || y >= rr->h - 1 - r);
    if (!on_border && !in_corner) return 0;
    unsigned int outer = gfx_round_rect_coverage(rr, x, y);
    if (outer == 0) return 0;
    GfxRoundRect inner;
    gfx_round_rect_init(&inner, rr->w - 2, rr->h - 2, rr->radius - 1);
    unsigned int in = gfx_round_rect_coverage(&inner, x - 1, y - 1);
    return outer > in ? outer - in : 0;
}

static int canvas_clip(const GfxCanvas *canvas, int x, int y, int w, int h, GfxRect *out) {
    GfxRect shape = { x, y, w, h };
    GfxRect bounds = { 0, 0, canvas->width, canvas->height };
    if (!canvas->pixels || canvas->stride < canvas->width) return 0;
    return gfx_rect_intersect(&shape, &bounds, out);
}

void gfx_fill_round_rect(const GfxCanvas *canvas, int x, int y, int w, int h,
                         int radius, uint32_t rgb, unsigned int opacity) {
    GfxRect clip;
    if (!canvas || opacity == 0 || !canvas_clip(canvas, x, y, w, h, &clip)) return;
    if (opacity > 255) opacity = 255;
    GfxRoundRect rr;
    gfx_round_rect_init(&rr, w, h, radius);
    for (int py = clip.y; py < clip.y + clip.h; ++py) {
        for (int px = clip.x; px < clip.x + clip.w; ++px) {
            unsigned int cov = gfx_round_rect_coverage(&rr, px - x, py - y);
            gfx_blend_pixel(canvas, px, py, rgb, (cov * opacity + 127u) / 255u);
        }
    }
}

void gfx_stroke_round_rect(const GfxCanvas *canvas, int x, int y, int w, int h,
                           int radius, uint32_t rgb, unsigned int opacity) {
    GfxRect clip;
    if (!canvas || opacity == 0 || !canvas_clip(canvas, x, y, w, h, &clip)) return;
    if (opacity > 255) opacity = 255;
    GfxRoundRect rr;
    gfx_round_rect_init(&rr, w, h, radius);
    for (int py = clip.y; py < clip.y + clip.h; ++py) {
        for (int px = clip.x; px < clip.x + clip.w; ++px) {
            unsigned int cov = gfx_round_rect_edge(&rr, px - x, py - y);
            gfx_blend_pixel(canvas, px, py, rgb, (cov * opacity + 127u) / 255u);
        }
    }
}

/* -------------------------------------------------------------- shadows */

static GfxShadow gfx_shadow_cache[GFX_SHADOW_CACHE_SLOTS];
static int gfx_shadow_count = 0;

/* The template is the blurred mask of a rounded square whose straight edge
   spans the middle row/column. Every source sample the blur reaches from
   that row lies on the straight edge, so repeating it yields exactly the
   shadow of a larger rectangle: a nine-slice that stretches losslessly. */
static int shadow_build(GfxShadow *s, int corner_radius, int blur_radius) {
    int extent = gfx_blur_extent(blur_radius);
    int center = corner_radius + 2 * extent;
    int size = 2 * center + 1;
    uint8_t *mask = (uint8_t *)malloc((size_t)size * (size_t)size + (size_t)size);
    if (!mask) return -1;
    uint8_t *line = mask + (size_t)size * (size_t)size;
    memset(mask, 0, (size_t)size * (size_t)size);

    GfxRoundRect rr;
    int shape = size - 2 * extent;
    gfx_round_rect_init(&rr, shape, shape, corner_radius);
    for (int y = 0; y < shape; ++y)
        for (int x = 0; x < shape; ++x)
            mask[(size_t)(y + extent) * (size_t)size + (size_t)(x + extent)] =
                (uint8_t)gfx_round_rect_coverage(&rr, x, y);
    gfx_blur_a8(mask, size, size, size, blur_radius, line);

    s->corner_radius = corner_radius;
    s->blur_radius = blur_radius;
    s->extent = extent;
    s->size = size;
    s->center = center;
    s->mask = mask;
    return 0;
}

const GfxShadow *gfx_shadow_get(int corner_radius, int blur_radius) {
    if (corner_radius < 0 || corner_radius > GFX_MAX_CORNER_RADIUS ||
        blur_radius <= 0 || blur_radius > GFX_MAX_BLUR_RADIUS)
        return 0;
    for (int i = 0; i < gfx_shadow_count; ++i) {
        if (gfx_shadow_cache[i].corner_radius == corner_radius &&
            gfx_shadow_cache[i].blur_radius == blur_radius)
            return &gfx_shadow_cache[i];
    }
    if (gfx_shadow_count >= GFX_SHADOW_CACHE_SLOTS) return 0;
    if (shadow_build(&gfx_shadow_cache[gfx_shadow_count], corner_radius, blur_radius) != 0)
        return 0;
    return &gfx_shadow_cache[gfx_shadow_count++];
}

/* Maps a coordinate of a span of length n onto the template; spans shorter
   than the template fold symmetrically around their middle. */
static int shadow_axis(const GfxShadow *s, int n, int i) {
    if (2 * i < n) return i < s->center ? i : s->center;
    int from_end = n - 1 - i;
    return s->size - 1 - (from_end < s->center ? from_end : s->center);
}

unsigned int gfx_shadow_sample(const GfxShadow *shadow, int w, int h, int u, int v) {
    int bw = w + 2 * shadow->extent;
    int bh = h + 2 * shadow->extent;
    if (u < 0 || v < 0 || u >= bw || v >= bh) return 0;
    int tu = shadow_axis(shadow, bw, u);
    int tv = shadow_axis(shadow, bh, v);
    return shadow->mask[(size_t)tv * (size_t)shadow->size + (size_t)tu];
}

void gfx_draw_shadow(const GfxCanvas *canvas, const GfxShadow *shadow,
                     int x, int y, int w, int h, int offset_x, int offset_y,
                     uint32_t rgb, unsigned int opacity, const GfxRect *clip) {
    if (!canvas || !shadow || opacity == 0 || w <= 0 || h <= 0) return;
    if (opacity > 255) opacity = 255;
    int e = shadow->extent;
    GfxRect box = { x + offset_x - e, y + offset_y - e, w + 2 * e, h + 2 * e };
    GfxRect area;
    if (!canvas_clip(canvas, box.x, box.y, box.w, box.h, &area)) return;
    if (clip && !gfx_rect_intersect(&area, clip, &area)) return;

    GfxRoundRect knockout;
    gfx_round_rect_init(&knockout, w, h, shadow->corner_radius);
    for (int py = area.y; py < area.y + area.h; ++py) {
        int ky = py - y;
        int row_inside = ky >= knockout.radius && ky < h - knockout.radius;
        for (int px = area.x; px < area.x + area.w; ++px) {
            int kx = px - x;
            if (row_inside && kx >= 0 && kx < w) {
                px = x + w - 1;  /* fully covered span: skip to its end */
                continue;
            }
            unsigned int s = gfx_shadow_sample(shadow, w, h, px - box.x, py - box.y);
            if (s == 0) continue;
            unsigned int cover = gfx_round_rect_coverage(&knockout, kx, ky);
            unsigned int a = (s * opacity + 127u) / 255u;
            a = (a * (255u - cover) + 127u) / 255u;
            gfx_blend_pixel(canvas, px, py, rgb, a);
        }
    }
}

/* ---------------------------------------------------------------- glass */

static uint32_t *gfx_glass_scratch = 0;
static size_t gfx_glass_scratch_cap = 0;

/* The allocator hands out whole pages and cannot reuse freed memory yet, so
   the scratch area only ever grows. */
static uint32_t *glass_scratch(size_t count) {
    if (count <= gfx_glass_scratch_cap) return gfx_glass_scratch;
    uint32_t *fresh = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!fresh) return 0;
    if (gfx_glass_scratch) free(gfx_glass_scratch);
    gfx_glass_scratch = fresh;
    gfx_glass_scratch_cap = count;
    return fresh;
}

int gfx_glass_apply(const GfxCanvas *canvas, int x, int y, int w, int h, const GfxGlass *glass) {
    GfxRect target;
    if (!canvas || !glass || !canvas_clip(canvas, x, y, w, h, &target)) return 0;

    int e = gfx_blur_extent(glass->blur_radius);
    GfxRect area;
    canvas_clip(canvas, target.x - e, target.y - e, target.w + 2 * e, target.h + 2 * e, &area);
    size_t line_len = (size_t)(area.w > area.h ? area.w : area.h);
    uint32_t *work = glass_scratch((size_t)area.w * (size_t)area.h + line_len);
    if (!work) return -1;
    uint32_t *line = work + (size_t)area.w * (size_t)area.h;

    for (int row = 0; row < area.h; ++row)
        memcpy(work + (size_t)row * (size_t)area.w,
               canvas->pixels + (size_t)(area.y + row) * (size_t)canvas->stride + (size_t)area.x,
               (size_t)area.w * sizeof(uint32_t));
    gfx_blur(work, area.w, area.h, area.w, glass->blur_radius, line);

    GfxRoundRect rr;
    gfx_round_rect_init(&rr, w, h, glass->corner_radius);
    for (int py = target.y; py < target.y + target.h; ++py) {
        for (int px = target.x; px < target.x + target.w; ++px) {
            unsigned int cov = gfx_round_rect_coverage(&rr, px - x, py - y);
            if (cov == 0) continue;
            uint32_t *p = &canvas->pixels[(size_t)py * (size_t)canvas->stride + (size_t)px];
            uint32_t blurred = work[(size_t)(py - area.y) * (size_t)area.w + (size_t)(px - area.x)];
            uint32_t material = gfx_mix(blurred, glass->tint, glass->tint_alpha);
            if (2 * (py - y) < h && py - y <= rr.radius) {
                unsigned int edge = gfx_round_rect_edge(&rr, px - x, py - y);
                material = gfx_mix(material, glass->highlight,
                                   (edge * glass->highlight_alpha + 127u) / 255u);
            }
            material = (material & 0x00FFFFFFu) | (*p & 0xFF000000u);
            *p = gfx_mix(*p, material, cov);
        }
    }
    return 0;
}

/* -------------------------------------------------------------- checksum */

uint32_t gfx_checksum(const uint32_t *pixels, int w, int h, int stride) {
    uint32_t hash = 2166136261u;
    for (int y = 0; y < h; ++y) {
        const uint32_t *row = pixels + (size_t)y * (size_t)stride;
        for (int x = 0; x < w; ++x) {
            uint32_t p = row[x];
            for (int b = 0; b < 4; ++b) {
                hash ^= (p >> (8 * b)) & 0xFFu;
                hash *= 16777619u;
            }
        }
    }
    return hash;
}

uint32_t gfx_checksum_a8(const uint8_t *mask, int w, int h, int stride) {
    uint32_t hash = 2166136261u;
    for (int y = 0; y < h; ++y) {
        const uint8_t *row = mask + (size_t)y * (size_t)stride;
        for (int x = 0; x < w; ++x) {
            hash ^= row[x];
            hash *= 16777619u;
        }
    }
    return hash;
}
