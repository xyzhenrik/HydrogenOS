#pragma once

void tty_init(void);
void tty_set_color(unsigned char fg, unsigned char bg);
void tty_putc(char c);
void tty_write(const char *s);
void tty_draw_cursor(void);
