#include "fb_console.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "kheap.h"
#include "mouse.h"
#include <stddef.h>

static uint32_t fg = 0x00C0FFFF; // cyan-ish
static uint32_t bg = 0x00000000;
static uint32_t cursor_col = 0; // character coords
static uint32_t cursor_row = 0;
static uint32_t *backbuf = NULL;
static uint32_t cursor_color = 0x00FFFFFF;
static int mouse_overlay_enabled = 0;
static int cursor_visible = 1;
static const uint32_t CHAR_W = 8;
static const uint32_t CHAR_H = 16;
static uint32_t cols = 0;
static uint32_t rows = 0;

static void draw_mouse_overlay(void);
static void draw_text_cursor(void);

static uint32_t palette_color(unsigned char idx) {
    static const uint32_t palette[16] = {
        0x00000000, 0x000000AA, 0x0000AA00, 0x0000AAAA,
        0x00AA0000, 0x00AA00AA, 0x00AA5500, 0x00AAAAAA,
        0x00555555, 0x005555FF, 0x0055FF55, 0x0055FFFF,
        0x00FF5555, 0x00FF55FF, 0x00FFFF55, 0x00FFFFFF,
    };
    return palette[idx & 0x0F];
}

int fb_console_init(void) {
    if (!framebuffer.addr) return -1;
    uint32_t fb_size = framebuffer.pitch * framebuffer.height;
    backbuf = (uint32_t *)kalloc(fb_size);
    if (!backbuf) return -2;
    cols = framebuffer.width / CHAR_W;
    rows = framebuffer.height / CHAR_H;
    if (cols == 0 || rows == 0) return -3;
    return 0;
}


int fb_console_blit(int x, int y, int w, int h, const unsigned int *src) {
    if (!backbuf || !src) return -1;
    if (w <= 0 || h <= 0) return -1;
    int fw = (int)framebuffer.width;
    int fh = (int)framebuffer.height;
    for (int dy = 0; dy < h; ++dy) {
        int sy = y + dy;
        if (sy < 0 || sy >= fh) continue;
        const unsigned int *srow = src + (size_t)dy * (size_t)w;
        unsigned int *drow = (unsigned int *)((uint8_t *)backbuf + (size_t)sy * framebuffer.pitch);
        for (int dx = 0; dx < w; ++dx) {
            int sx = x + dx;
            if (sx < 0 || sx >= fw) continue;
            drow[sx] = srow[dx];
        }
    }
    fb_console_present();
    return 0;
}

void fb_console_set_cursor_visible(int visible) {
    cursor_visible = visible ? 1 : 0;
}

int fb_console_blit_strided(int x, int y, int w, int h,
                            const unsigned int *src, int src_stride_bytes) {
    if (!backbuf || !src || w <= 0 || h <= 0) return -1;
    int fw = (int)framebuffer.width;
    int fh = (int)framebuffer.height;
    for (int dy = 0; dy < h; ++dy) {
        int sy = y + dy;
        if (sy < 0 || sy >= fh) continue;
        const unsigned int *srow = (const unsigned int *)((const unsigned char *)src + (size_t)dy * (size_t)src_stride_bytes);
        unsigned int *drow = (unsigned int *)((uint8_t *)backbuf + (size_t)sy * framebuffer.pitch);
        for (int dx = 0; dx < w; ++dx) {
            int sx = x + dx;
            if (sx < 0 || sx >= fw) continue;
            drow[sx] = srow[dx];
        }
    }
    fb_console_present();
    return 0;
}

void fb_console_present(void) {
    if (!backbuf || !framebuffer.addr) return;
    uint32_t fb_size = framebuffer.pitch * framebuffer.height;
    uint32_t *dst = (uint32_t *)framebuffer.addr;
    uint32_t words = fb_size / 4;
    for (uint32_t i = 0; i < words; ++i) dst[i] = backbuf[i];
    if (cursor_visible) draw_text_cursor();
    draw_mouse_overlay();
}

static void fb_plot(uint32_t x, uint32_t y, uint32_t color) {
    if (!backbuf) return;
    if (x >= framebuffer.width || y >= framebuffer.height) return;
    uint32_t *row = (uint32_t *)((uint8_t *)backbuf + y * framebuffer.pitch);
    row[x] = color;
}

static void fb_draw_char(uint32_t x, uint32_t y, char c) {
    const uint8_t *glyph = font8x16[(uint8_t)c];
    for (uint32_t row = 0; row < 16; ++row) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; ++col) {
            uint32_t color = (bits & (1 << (7 - col))) ? fg : bg;
            fb_plot(x + col, y + row, color);
        }
    }
}

static void draw_mouse_overlay(void) {
    if (!mouse_overlay_enabled) return;
    int mx, my;
    if (!mouse_get_pos(&mx, &my)) return;
    uint8_t buttons = mouse_get_buttons();
    uint32_t color = (buttons & 1) ? 0x00FF0000 : 0x00FFFFFF; // left click -> red
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col <= row; ++col) {
            int x = mx + col;
            int y = my + row;
            if (x >= 0 && y >= 0 && x < (int)framebuffer.width && y < (int)framebuffer.height) {
                uint32_t *pix = (uint32_t *)((uint8_t *)framebuffer.addr + y * framebuffer.pitch) + x;
                *pix = color;
            }
        }
    }
}

void fb_console_set_color(unsigned char new_fg, unsigned char new_bg) {
    fg = palette_color(new_fg);
    bg = palette_color(new_bg);
}

static void scroll_up(void) {
    if (!backbuf) return;
    uint32_t bytes_per_line = framebuffer.pitch;
    uint32_t shift_bytes = CHAR_H * bytes_per_line;
    uint32_t copy_bytes = framebuffer.height > CHAR_H ? (framebuffer.height - CHAR_H) * bytes_per_line : 0;
    if (copy_bytes) {
        uint8_t *dst = (uint8_t *)backbuf;
        uint8_t *src = (uint8_t *)backbuf + shift_bytes;
        for (uint32_t i = 0; i < copy_bytes; ++i) dst[i] = src[i];
    }
    // clear last CHAR_H rows
    uint32_t start_clear = (rows - 1) * CHAR_H * bytes_per_line;
    uint32_t words = (CHAR_H * bytes_per_line) / 4;
    uint32_t *dstw = (uint32_t *)((uint8_t *)backbuf + start_clear);
    for (uint32_t i = 0; i < words; ++i) dstw[i] = bg;
}

void fb_console_clear(void) {
    if (!backbuf) return;
    uint32_t fb_size = framebuffer.pitch * framebuffer.height;
    uint32_t words = fb_size / 4;
    for (uint32_t i = 0; i < words; ++i) backbuf[i] = bg;
    cursor_col = 0;
    cursor_row = 0;
    fb_console_present();
}

void fb_console_putc(char c) {
    if (!backbuf) return;
    switch (c) {
        case '\n':
            cursor_col = 0;
            cursor_row++;
            break;
        case '\r':
            cursor_col = 0;
            break;
        case '\b':
            if (cursor_col > 0) {
                cursor_col--;
            }
            break;
        default:
            fb_draw_char(cursor_col * CHAR_W, cursor_row * CHAR_H, c);
            cursor_col++;
            if (cursor_col >= cols) {
                cursor_col = 0;
                cursor_row++;
            }
            break;
    }
    if (cursor_row >= rows) {
        scroll_up();
        cursor_row = rows - 1;
    }
    fb_console_present();
}

void fb_console_write(const char *s) {
    while (*s) {
        fb_console_putc(*s++);
    }
}

void fb_console_draw_cursor(void) {
    fb_console_present();
}

static void draw_text_cursor(void) {
    if (!backbuf || !framebuffer.addr) return;
    uint32_t x = cursor_col * CHAR_W;
    uint32_t y = cursor_row * CHAR_H + 14;
    if (y + 2 > framebuffer.height) y = framebuffer.height - 2;
    for (uint32_t row = 0; row < 2; ++row) {
        for (uint32_t col = 0; col < 8; ++col) {
            uint32_t px = x + col;
            uint32_t py = y + row;
            if (px < framebuffer.width && py < framebuffer.height) {
                uint32_t *pix = (uint32_t *)((uint8_t *)framebuffer.addr + py * framebuffer.pitch) + px;
                *pix = cursor_color;
            }
        }
    }
}
