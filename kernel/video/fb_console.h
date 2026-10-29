#pragma once

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
