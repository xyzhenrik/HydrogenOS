#include "iconset.h"

static const IconBitmap *icon_find(IconId id, int size) {
    for (int i = 0; i < icon_bitmap_count; ++i) {
        if (icon_bitmaps[i].id == id && icon_bitmaps[i].size == size)
            return &icon_bitmaps[i];
    }
    return 0;
}

static uint32_t div255(uint32_t value) {
    return (value + 1 + (value >> 8)) >> 8;
}

/* Source over destination, the source premultiplied. A canvas without alpha
   keeps whatever alpha byte it has, as everywhere else in the toolkit. */
static uint32_t blend_premultiplied(uint32_t dst, uint32_t src, int has_alpha) {
    uint32_t src_alpha = src >> 24;
    uint32_t inverse = 255 - src_alpha;

    if (src_alpha == 0)
        return dst;
    if (!has_alpha) {
        uint32_t red = ((src >> 16) & 0xFF) + div255(((dst >> 16) & 0xFF) * inverse);
        uint32_t green = ((src >> 8) & 0xFF) + div255(((dst >> 8) & 0xFF) * inverse);
        uint32_t blue = (src & 0xFF) + div255((dst & 0xFF) * inverse);
        return (dst & 0xFF000000u) | (red << 16) | (green << 8) | blue;
    }

    uint32_t dst_alpha = dst >> 24;
    uint32_t out_alpha = src_alpha + div255(dst_alpha * inverse);
    uint32_t out = out_alpha << 24;

    for (int shift = 16; shift >= 0; shift -= 8) {
        uint32_t dst_part = div255(((dst >> shift) & 0xFF) * dst_alpha);
        uint32_t part = ((src >> shift) & 0xFF) + div255(dst_part * inverse);
        uint32_t straight = (part * 255 + out_alpha / 2) / out_alpha;
        out |= (straight > 255 ? 255 : straight) << shift;
    }
    return out;
}

static uint32_t scale_pixel(uint32_t pixel, unsigned int opacity) {
    if (opacity >= 255)
        return pixel;
    return (div255((pixel >> 24) * opacity) << 24) | (div255(((pixel >> 16) & 0xFF) * opacity) << 16) |
           (div255(((pixel >> 8) & 0xFF) * opacity) << 8) | div255((pixel & 0xFF) * opacity);
}

static uint32_t tint_pixel(uint32_t coverage_pixel, uint32_t rgb, unsigned int opacity) {
    uint32_t alpha = div255((coverage_pixel >> 24) * opacity);

    return (alpha << 24) | (div255(((rgb >> 16) & 0xFF) * alpha) << 16) |
           (div255(((rgb >> 8) & 0xFF) * alpha) << 8) | div255((rgb & 0xFF) * alpha);
}

static void blit(const GfxCanvas *canvas, const IconBitmap *bitmap, const uint32_t *pixels,
                 int x, int y, uint32_t rgb, unsigned int opacity) {
    int size = bitmap->size;

    for (int row = 0; row < size; ++row) {
        int dy = y + row;
        if (dy < 0 || dy >= canvas->height)
            continue;
        uint32_t *line = canvas->pixels + (long)dy * canvas->stride;
        for (int column = 0; column < size; ++column) {
            int dx = x + column;
            uint32_t pixel = pixels[row * size + column];

            if (dx < 0 || dx >= canvas->width || (pixel >> 24) == 0)
                continue;
            pixel = bitmap->mask ? tint_pixel(pixel, rgb, opacity) : scale_pixel(pixel, opacity);
            line[dx] = blend_premultiplied(line[dx], pixel, canvas->has_alpha);
        }
    }
}

void icon_draw(const GfxCanvas *canvas, IconId id, int size, int x, int y,
               int light, unsigned int opacity) {
    const IconBitmap *bitmap = icon_find(id, size);

    if (!canvas || !bitmap || bitmap->mask)
        return;
    blit(canvas, bitmap, light ? bitmap->light : bitmap->dark, x, y, 0, opacity);
}

void icon_draw_glyph(const GfxCanvas *canvas, IconId id, int size, int x, int y,
                     uint32_t rgb, unsigned int opacity) {
    const IconBitmap *bitmap = icon_find(id, size);

    if (!canvas || !bitmap || !bitmap->mask)
        return;
    blit(canvas, bitmap, bitmap->dark, x, y, rgb, opacity);
}

int icon_available(IconId id, int size, int glyph) {
    const IconBitmap *bitmap = icon_find(id, size);

    return bitmap && (bitmap->mask != 0) == (glyph != 0);
}
