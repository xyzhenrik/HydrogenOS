#pragma once

#include "mini_libc.h"
#include "widgets.h"

#include <stdint.h>
#include <stddef.h>

#define DESKTOPBG_MODE_SOLID     0
#define DESKTOPBG_MODE_GRADIENT  1
#define DESKTOPBG_MODE_PNG       2

#define DESKTOPBG_DEFAULT_SOLID   0xFF1E3A5Fu
#define DESKTOPBG_DEFAULT_TOP     0xFF4A90D9u
#define DESKTOPBG_DEFAULT_BOTTOM  0xFF1E3A5Fu

typedef struct DesktopBg DesktopBg;
typedef DesktopBg desktopbg_t;
typedef DesktopBg DesktopBg_t;
typedef DesktopBg desktop_bg_t;

struct DesktopBg {
    int mode;
    uint32_t solid_color;
    uint32_t gradient_top;
    uint32_t gradient_bottom;
    char png_path[512];
    int has_png;
};

/* Instance helpers. */
DesktopBg *desktopbg_create(void);
void desktopbg_destroy(DesktopBg *bg);
DesktopBg *desktopbg_get(void);

/* Configuration (global defaults when bg is NULL). */
void desktopbg_set_mode(int mode);
void desktopbg_set_mode_bg(DesktopBg *bg, int mode);

void desktopbg_set_color(uint32_t color);
void desktopbg_set_color_bg(DesktopBg *bg, uint32_t color);

void desktopbg_set_solid(uint32_t color);
void desktopbg_set_solid_color(uint32_t color);
void desktopbg_set_solid_bg(DesktopBg *bg, uint32_t color);

void desktopbg_set_gradient(uint32_t top, uint32_t bottom);
void desktopbg_set_gradient_colors(uint32_t top, uint32_t bottom);
void desktopbg_set_gradient_bg(DesktopBg *bg, uint32_t top, uint32_t bottom);

void desktopbg_set_top_color(uint32_t color);
void desktopbg_set_bottom_color(uint32_t color);

int desktopbg_load_png(const char *path);
int desktopbg_load_png_bg(DesktopBg *bg, const char *path);

/* Flexible draw entry points. */
void desktopbg_draw_0(void);
void desktopbg_draw_1(uintptr_t a);
void desktopbg_draw_2(uintptr_t a, uintptr_t b);
void desktopbg_draw(Surface *surface);

/* Convenience aliases. */
void desktop_background_draw(Surface *surface);
void draw_desktop_background(Surface *surface);
void desktop_bg_draw(Surface *surface);
void desktopbg_render(Surface *surface);
void desktopbg_paint(Surface *surface);
void desktopbg_draw_background(Surface *surface);