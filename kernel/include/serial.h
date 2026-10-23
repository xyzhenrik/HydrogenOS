#pragma once

#include <stdint.h>

void serial_init(void);
int serial_is_ready(void);
int serial_can_read(void);
int serial_getc(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_write_hex(uint32_t value);
