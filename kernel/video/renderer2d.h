#pragma once
#include <stdint.h>
#include "framebuffer.h"

void r2d_init(const fb_t *fb);
void r2d_clear(uint32_t color);
void r2d_rect(int x, int y, int w, int h, uint32_t color);
void r2d_rect_filled(int x, int y, int w, int h, uint32_t color);
void r2d_line(int x0, int y0, int x1, int y1, uint32_t color);
void r2d_circle(int cx, int cy, int r, uint32_t color);
void r2d_circle_filled(int cx, int cy, int r, uint32_t color);
void r2d_blit_alpha(const uint32_t *src, int src_w, int src_h, int dst_x, int dst_y);
void r2d_clip_set(int x, int y, int w, int h);
void r2d_clip_restore(void);