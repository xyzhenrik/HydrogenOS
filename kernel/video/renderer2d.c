#include "renderer2d.h"
#include <stddef.h>
#include <stdint.h>

static uint32_t *r2d_buf = NULL;
static uint32_t r2d_width = 0;
static uint32_t r2d_height = 0;
static uint32_t r2d_pitch = 0;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} r2d_clip_t;

static r2d_clip_t r2d_clip;
static r2d_clip_t r2d_clip_stack[16];
static int r2d_clip_depth = 0;

static int r2d_in_clip(int x, int y) {
    if (x < r2d_clip.x || y < r2d_clip.y) return 0;
    if (x >= r2d_clip.x + r2d_clip.w) return 0;
    if (y >= r2d_clip.y + r2d_clip.h) return 0;
    return 1;
}

static void r2d_put_pixel(int x, int y, uint32_t color) {
    if (!r2d_buf) return;
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= r2d_width || (uint32_t)y >= r2d_height) return;
    if (!r2d_in_clip(x, y)) return;
    uint32_t *row = (uint32_t *)((uint8_t *)r2d_buf + (uint32_t)y * r2d_pitch);
    row[x] = color;
}

static uint32_t r2d_blend_over(uint32_t src, uint32_t dst) {
    uint32_t sa = (src >> 24) & 0xFFu;
    uint32_t da = (dst >> 24) & 0xFFu;
    if (sa == 0) return dst;
    if (sa == 255 && da == 0) return src;
    uint32_t oa = sa + (da * (255u - sa)) / 255u;
    if (oa == 0) return 0;
    uint32_t out_r, out_g, out_b;
    {
        uint32_t sc = (src >> 16) & 0xFFu;
        uint32_t dc = (dst >> 16) & 0xFFu;
        uint32_t num = sc * sa * 255u + dc * da * (255u - sa);
        uint32_t pm = num / (255u * 255u);
        out_r = pm * 255u / oa;
    }
    {
        uint32_t sc = (src >> 8) & 0xFFu;
        uint32_t dc = (dst >> 8) & 0xFFu;
        uint32_t num = sc * sa * 255u + dc * da * (255u - sa);
        uint32_t pm = num / (255u * 255u);
        out_g = pm * 255u / oa;
    }
    {
        uint32_t sc = src & 0xFFu;
        uint32_t dc = dst & 0xFFu;
        uint32_t num = sc * sa * 255u + dc * da * (255u - sa);
        uint32_t pm = num / (255u * 255u);
        out_b = pm * 255u / oa;
    }
    return (oa << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

void r2d_init(const fb_t *fb) {
    if (!fb || !fb->addr || fb->bpp != 32) {
        r2d_buf = NULL;
        r2d_width = 0;
        r2d_height = 0;
        r2d_pitch = 0;
        r2d_clip.x = 0;
        r2d_clip.y = 0;
        r2d_clip.w = 0;
        r2d_clip.h = 0;
        r2d_clip_depth = 0;
        return;
    }
    r2d_buf = (uint32_t *)fb->addr;
    r2d_width = fb->width;
    r2d_height = fb->height;
    r2d_pitch = fb->pitch;
    r2d_clip.x = 0;
    r2d_clip.y = 0;
    r2d_clip.w = (int)r2d_width;
    r2d_clip.h = (int)r2d_height;
    r2d_clip_depth = 0;
}

void r2d_clear(uint32_t color) {
    if (!r2d_buf) return;
    size_t total = (size_t)r2d_pitch * (size_t)r2d_height;
    size_t words = total / 4;
    for (size_t i = 0; i < words; ++i) r2d_buf[i] = color;
}

void r2d_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; ++i) {
        r2d_put_pixel(x + i, y, color);
        r2d_put_pixel(x + i, y + h - 1, color);
    }
    for (int i = 0; i < h; ++i) {
        r2d_put_pixel(x, y + i, color);
        r2d_put_pixel(x + w - 1, y + i, color);
    }
}

void r2d_rect_filled(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            r2d_put_pixel(x + xx, y + yy, color);
        }
    }
}

void r2d_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        r2d_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void r2d_circle(int cx, int cy, int r, uint32_t color) {
    if (r < 0) return;
    if (r == 0) { r2d_put_pixel(cx, cy, color); return; }
    int x = r;
    int y = 0;
    int err = 1 - r;
    while (x >= y) {
        r2d_put_pixel(cx + x, cy + y, color);
        r2d_put_pixel(cx + y, cy + x, color);
        r2d_put_pixel(cx - y, cy + x, color);
        r2d_put_pixel(cx - x, cy + y, color);
        r2d_put_pixel(cx - x, cy - y, color);
        r2d_put_pixel(cx - y, cy - x, color);
        r2d_put_pixel(cx + y, cy - x, color);
        r2d_put_pixel(cx + x, cy - y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void r2d_circle_filled(int cx, int cy, int r, uint32_t color) {
    if (r < 0) return;
    if (r == 0) { r2d_put_pixel(cx, cy, color); return; }
    for (int dy = -r; dy <= r; ++dy) {
        int rem = r * r - dy * dy;
        if (rem < 0) rem = 0;
        int lo = 0, hi = r;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (mid * mid <= rem) lo = mid;
            else hi = mid - 1;
        }
        int dx = lo;
        for (int x = cx - dx; x <= cx + dx; ++x) {
            r2d_put_pixel(x, cy + dy, color);
        }
    }
}

void r2d_blit_alpha(const uint32_t *src, int src_w, int src_h, int dst_x, int dst_y) {
    if (!src || src_w <= 0 || src_h <= 0) return;
    for (int sy = 0; sy < src_h; ++sy) {
        for (int sx = 0; sx < src_w; ++sx) {
            int dx = dst_x + sx;
            int dy = dst_y + sy;
            if (dx < 0 || dy < 0) continue;
            if ((uint32_t)dx >= r2d_width || (uint32_t)dy >= r2d_height) continue;
            if (!r2d_in_clip(dx, dy)) continue;
            uint32_t spx = src[sy * src_w + sx];
            uint32_t *row = (uint32_t *)((uint8_t *)r2d_buf + (uint32_t)dy * r2d_pitch);
            uint32_t dstpx = row[dx];
            row[dx] = r2d_blend_over(spx, dstpx);
        }
    }
}

void r2d_clip_set(int x, int y, int w, int h) {
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (r2d_clip_depth < 16) {
        r2d_clip_stack[r2d_clip_depth++] = r2d_clip;
    }
    r2d_clip.x = x;
    r2d_clip.y = y;
    r2d_clip.w = w;
    r2d_clip.h = h;
}

void r2d_clip_restore(void) {
    if (r2d_clip_depth > 0) {
        r2d_clip = r2d_clip_stack[--r2d_clip_depth];
    } else {
        r2d_clip.x = 0;
        r2d_clip.y = 0;
        r2d_clip.w = (int)r2d_width;
        r2d_clip.h = (int)r2d_height;
    }
}