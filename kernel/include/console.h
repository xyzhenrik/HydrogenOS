#pragma once

#include <stdint.h>

void console_init(uint8_t color);
void console_clear(void);
void console_set_color(uint8_t fg, uint8_t bg);
void console_putc(char c);
void console_write(const char *s);
void console_write_hex(uint32_t value);
void console_write_hex64(uint64_t value);
void console_mouse_update(uint32_t col, uint32_t row);
