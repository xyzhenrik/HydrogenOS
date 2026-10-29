#pragma once

#include <stdint.h>

void fb_console_clear(void);
void fb_console_putc(char c);
void fb_console_write(const char *s);
int fb_console_init(void);
void fb_console_set_color(unsigned char fg, unsigned char bg);
void fb_console_present(void);
void fb_console_draw_cursor(void);
void fb_console_set_cursor_visible(int visible);
int fb_console_blit_strided(int x, int y, int w, int h, const unsigned int *src, int src_stride_bytes);
int fb_console_blit(int x, int y, int w, int h, const unsigned int *src);

/* While suspended, text output and clears are dropped and the console no
   longer presents; whoever suspends it owns the screen. */
void fb_console_set_suspended(int suspended);

/* Console backing store, framebuffer.pitch bytes per row, or NULL before
   fb_console_init succeeded. */
uint32_t *fb_console_surface(void);

/* Copies a rectangle of the backing store to the screen, without cursor or
   mouse overlays. */
void fb_console_present_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
