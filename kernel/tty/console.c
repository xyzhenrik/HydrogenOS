#include "console.h"
#include "io.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

static volatile uint16_t *const VGA_MEMORY = (uint16_t *)0xB8000;
static const uint8_t VGA_WIDTH = 80;
static const uint8_t VGA_HEIGHT = 25;

static uint8_t default_color = 0x0F; // white on black
static size_t cursor_row = 0;
static size_t cursor_col = 0;
static int mouse_visible = 0;
static int mouse_cursor_enabled = 0;
static size_t mouse_row = 0;
static size_t mouse_col = 0;
static uint16_t mouse_saved = 0;

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static void move_cursor_hw(void) {
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void enable_cursor_hw(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);
}

static void scroll_if_needed(void) {
    if (cursor_row < VGA_HEIGHT) return;
    // scroll up one row
    for (size_t r = 1; r < VGA_HEIGHT; ++r) {
        for (size_t c = 0; c < VGA_WIDTH; ++c) {
            VGA_MEMORY[(r - 1) * VGA_WIDTH + c] = VGA_MEMORY[r * VGA_WIDTH + c];
        }
    }
    // clear last row
    for (size_t c = 0; c < VGA_WIDTH; ++c) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = vga_entry(' ', default_color);
    }
    cursor_row = VGA_HEIGHT - 1;
}

void console_mouse_update(uint32_t col, uint32_t row) {
    if (!mouse_cursor_enabled) return;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;

    if (mouse_visible) {
        VGA_MEMORY[mouse_row * VGA_WIDTH + mouse_col] = mouse_saved;
    }

    mouse_row = row;
    mouse_col = col;
    mouse_saved = VGA_MEMORY[mouse_row * VGA_WIDTH + mouse_col];

    uint16_t cell = mouse_saved;
    uint8_t ch = (uint8_t)(cell & 0xFF);
    uint8_t attr = (uint8_t)((cell >> 8) & 0xFF);
    // invert foreground/background for mouse indicator
    uint8_t fg = attr & 0x0F;
    uint8_t bg = (attr >> 4) & 0x0F;
    uint8_t new_attr = (fg << 4) | bg;
    VGA_MEMORY[mouse_row * VGA_WIDTH + mouse_col] = vga_entry((char)ch, new_attr);
    mouse_visible = 1;
}

void console_set_color(uint8_t fg, uint8_t bg) {
    default_color = (uint8_t)(((bg & 0x0F) << 4) | (fg & 0x0F));
}

void console_init(uint8_t color) {
    default_color = color;
    console_clear();
    enable_cursor_hw();
    move_cursor_hw();
}

void console_clear(void) {
    for (size_t i = 0; i < (size_t)VGA_WIDTH * VGA_HEIGHT; ++i) {
        VGA_MEMORY[i] = vga_entry(' ', default_color);
    }
    cursor_row = 0;
    cursor_col = 0;
    move_cursor_hw();
}

void console_putc(char c) {
    serial_putc(c);
    if (c == '\r') {
        cursor_col = 0;
        move_cursor_hw();
        return;
    }
    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(' ', default_color);
            move_cursor_hw();
        }
        return;
    }
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        scroll_if_needed();
        move_cursor_hw();
        return;
    }
    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(c, default_color);
    cursor_col++;
    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
        scroll_if_needed();
    }
    move_cursor_hw();
}

void console_write(const char *s) {
    for (size_t i = 0; s[i]; ++i) {
        console_putc(s[i]);
    }
}

void console_write_hex(uint32_t value) {
    const char *digits = "0123456789ABCDEF";
    console_write("0x");
    for (int i = 7; i >= 0; --i) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        console_putc(digits[nibble]);
    }
}

void console_write_hex64(uint64_t value) {
    const char *digits = "0123456789ABCDEF";
    console_write("0x");
    for (int i = 15; i >= 0; --i) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        console_putc(digits[nibble]);
    }
}
