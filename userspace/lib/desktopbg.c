#include "desktopbg.h"

#include <stddef.h>
#include <stdint.h>

static DesktopBg *g_desktopbg = 0;

static int desktopbg_is_surface_ptr(void *p) {
    if (!p) return 0;
    Surface *s = (Surface *)p;
    return s->pixels != 0 && s->width > 0 && s->height > 0;
}

static DesktopBg *desktopbg_as_bg(void *p) {
    if (!p) return 0;
    if (desktopbg_is_surface_ptr(p)) return 0;
    return (DesktopBg *)p;
}

static int desktopbg_surface_width(Surface *s) {
    if (!s) return 0;
    return s->width > 0 ? s->width : s->w;
}

static int desktopbg_surface_height(Surface *s) {
    if (!s) return 0;
    return s->height > 0 ? s->height : s->h;
}

static int desktopbg_surface_stride(Surface *s) {
    if (!s) return 0;
    int w = desktopbg_surface_width(s);
    int stride = s->stride > 0 ? s->stride : (s->pitch > 0 ? s->pitch : w);
    if (stride <= 0) stride = w;
    return stride;
}

DesktopBg *desktopbg_create(void) {
    DesktopBg *bg = (DesktopBg *)malloc(sizeof(DesktopBg));
    if (!bg) return 0;
    memset(bg, 0, sizeof(DesktopBg));
    bg->mode = DESKTOPBG_MODE_GRADIENT;
    bg->solid_color = DESKTOPBG_DEFAULT_SOLID;
    bg->gradient_top = DESKTOPBG_DEFAULT_TOP;
    bg->gradient_bottom = DESKTOPBG_DEFAULT_BOTTOM;
    bg->has_png = 0;
    bg->png_path[0] = '\0';
    return bg;
}

void desktopbg_destroy(DesktopBg *bg) {
    if (!bg) return;
    if (g_desktopbg == bg) g_desktopbg = 0;
    free(bg);
}

DesktopBg *desktopbg_get(void) {
    if (!g_desktopbg) g_desktopbg = desktopbg_create();
    return g_desktopbg;
}

static DesktopBg *desktopbg_ensure(DesktopBg *bg) {
    if (!bg) bg = g_desktopbg;
    if (!bg) {
        g_desktopbg = desktopbg_create();
        bg = g_desktopbg;
    }
    if (g_desktopbg == 0 && bg) g_desktopbg = bg;
    return bg;
}

static uint32_t desktopbg_lerp_channel(uint8_t a, uint8_t b, int i, int n) {
    if (n <= 1) return a;
    int v = (int)a + ((int)b - (int)a) * i / (n - 1);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint32_t)v;
}

static uint32_t desktopbg_lerp_color(uint32_t a, uint32_t b, int i, int n) {
    uint32_t ra = desktopbg_lerp_channel((uint8_t)(a >> 16), (uint8_t)(b >> 16), i, n);
    uint32_t ga = desktopbg_lerp_channel((uint8_t)(a >> 8), (uint8_t)(b >> 8), i, n);
    uint32_t ba = desktopbg_lerp_channel((uint8_t)a, (uint8_t)b, i, n);
    uint32_t aa = desktopbg_lerp_channel((uint8_t)(a >> 24), (uint8_t)(b >> 24), i, n);
    return (aa << 24) | (ra << 16) | (ga << 8) | ba;
}

void desktopbg_set_mode(int mode) {
    DesktopBg *bg = desktopbg_ensure(0);
    if (bg) bg->mode = mode;
}

void desktopbg_set_mode_bg(DesktopBg *bg, int mode) {
    bg = desktopbg_ensure(bg);
    if (bg) bg->mode = mode;
}

void desktopbg_set_color(uint32_t color) {
    DesktopBg *bg = desktopbg_ensure(0);
    if (bg) {
        bg->solid_color = color;
        bg->mode = DESKTOPBG_MODE_SOLID;
    }
}

void desktopbg_set_color_bg(DesktopBg *bg, uint32_t color) {
    bg = desktopbg_ensure(bg);
    if (bg) {
        bg->solid_color = color;
        bg->mode = DESKTOPBG_MODE_SOLID;
    }
}

void desktopbg_set_solid(uint32_t color) {
    desktopbg_set_color(color);
}

void desktopbg_set_solid_color(uint32_t color) {
    desktopbg_set_color(color);
}

void desktopbg_set_solid_bg(DesktopBg *bg, uint32_t color) {
    desktopbg_set_color_bg(bg, color);
}

void desktopbg_set_gradient(uint32_t top, uint32_t bottom) {
    DesktopBg *bg = desktopbg_ensure(0);
    if (bg) {
        bg->gradient_top = top;
        bg->gradient_bottom = bottom;
        bg->mode = DESKTOPBG_MODE_GRADIENT;
    }
}

void desktopbg_set_gradient_colors(uint32_t top, uint32_t bottom) {
    desktopbg_set_gradient(top, bottom);
}

void desktopbg_set_gradient_bg(DesktopBg *bg, uint32_t top, uint32_t bottom) {
    bg = desktopbg_ensure(bg);
    if (bg) {
        bg->gradient_top = top;
        bg->gradient_bottom = bottom;
        bg->mode = DESKTOPBG_MODE_GRADIENT;
    }
}

void desktopbg_set_top_color(uint32_t color) {
    DesktopBg *bg = desktopbg_ensure(0);
    if (bg) bg->gradient_top = color;
}

void desktopbg_set_bottom_color(uint32_t color) {
    DesktopBg *bg = desktopbg_ensure(0);
    if (bg) bg->gradient_bottom = color;
}

int desktopbg_load_png(const char *path) {
    return desktopbg_load_png_bg(0, path);
}

int desktopbg_load_png_bg(DesktopBg *bg, const char *path) {
    bg = desktopbg_ensure(bg);
    if (!bg || !path) return -1;
    size_t i = 0;
    while (path[i] && i + 1 < sizeof(bg->png_path)) {
        bg->png_path[i] = path[i];
        ++i;
    }
    bg->png_path[i] = '\0';
    bg->has_png = 1;
    bg->mode = DESKTOPBG_MODE_PNG;
    /* PNG decoding is not implemented; drawing falls back to gradient. */
    return -1;
}

static void desktopbg_draw_impl(DesktopBg *bg, Surface *surface) {
    bg = desktopbg_ensure(bg);
    if (!bg || !surface || !surface->pixels) return;

    int w = desktopbg_surface_width(surface);
    int h = desktopbg_surface_height(surface);
    int stride = desktopbg_surface_stride(surface);
    if (w <= 0 || h <= 0 || stride <= 0) return;

    if (bg->mode == DESKTOPBG_MODE_SOLID) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                surface->pixels[y * stride + x] = bg->solid_color;
        return;
    }

    /* Gradient, including PNG fallback. */
    for (int y = 0; y < h; ++y) {
        uint32_t row = desktopbg_lerp_color(bg->gradient_top, bg->gradient_bottom, y, h);
        for (int x = 0; x < w; ++x)
            surface->pixels[y * stride + x] = row;
    }
}

void desktopbg_draw_0(void) {
    /* Nothing to draw without a surface. */
}

void desktopbg_draw_1(uintptr_t a) {
    if (desktopbg_is_surface_ptr((void *)a)) {
        desktopbg_draw_impl(0, (Surface *)a);
        return;
    }
    DesktopBg *bg = desktopbg_as_bg((void *)a);
    if (bg) {
        /* No surface provided; nothing to draw. */
    }
}

void desktopbg_draw_2(uintptr_t a, uintptr_t b) {
    void *args[2] = { (void *)a, (void *)b };
    DesktopBg *bg = 0;
    Surface *s = 0;
    for (int i = 0; i < 2; ++i) {
        if (desktopbg_is_surface_ptr(args[i])) {
            if (!s) s = (Surface *)args[i];
        } else if (args[i]) {
            if (!bg) bg = desktopbg_as_bg(args[i]);
        }
    }
    if (s) desktopbg_draw_impl(bg, s);
}

void desktopbg_draw(Surface *surface) {
    desktopbg_draw_1((uintptr_t)surface);
}

void desktop_background_draw(Surface *surface) { desktopbg_draw(surface); }
void draw_desktop_background(Surface *surface) { desktopbg_draw(surface); }
void desktop_bg_draw(Surface *surface) { desktopbg_draw(surface); }
void desktopbg_render(Surface *surface) { desktopbg_draw(surface); }
void desktopbg_paint(Surface *surface) { desktopbg_draw(surface); }
void desktopbg_draw_background(Surface *surface) { desktopbg_draw(surface); }