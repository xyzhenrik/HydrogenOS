#include "tty.h"
#include "framebuffer.h"
#include "video/fb_console.h"
#include "console.h"
#include "serial.h"
#include <stddef.h>

static int use_fb(void) { return framebuffer.addr != NULL; }

void tty_init(void) {
    // nothing to do; framebuffer is initialized in kernel.c
}

void tty_set_color(unsigned char fg, unsigned char bg) {
    if (use_fb())
        fb_console_set_color(fg, bg);
    else
        console_set_color(fg, bg);
}

void tty_putc(char c) {
    if (use_fb())
        serial_putc(c);
    if (use_fb())
        fb_console_putc(c);
    else
        console_putc(c);
}

void tty_write(const char *s) {
    if (use_fb() && s)
        serial_write(s);
    if (use_fb())
        fb_console_write(s);
    else
        console_write(s);
}

void tty_draw_cursor(void) {
    if (use_fb())
        fb_console_draw_cursor();
}
