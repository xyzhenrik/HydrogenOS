#include "serial.h"
#include "io.h"

#include <stddef.h>

#define SERIAL_PORT 0x3F8

static int serial_ready = 0;

static int serial_transmit_empty(void) {
    return inb(SERIAL_PORT + 5) & 0x20;
}

void serial_init(void) {
    // Disable interrupts
    outb(SERIAL_PORT + 1, 0x00);
    // Enable DLAB
    outb(SERIAL_PORT + 3, 0x80);
    // Set divisor to 3 (38400 baud)
    outb(SERIAL_PORT + 0, 0x03);
    outb(SERIAL_PORT + 1, 0x00);
    // 8 bits, no parity, one stop bit
    outb(SERIAL_PORT + 3, 0x03);
    // Enable FIFO, clear, 14-byte threshold
    outb(SERIAL_PORT + 2, 0xC7);
    // IRQs enabled, RTS/DSR set
    outb(SERIAL_PORT + 4, 0x0B);

    // Loopback test to detect UART presence
    outb(SERIAL_PORT + 4, 0x1E);
    outb(SERIAL_PORT + 0, 0xAE);
    if (inb(SERIAL_PORT + 0) != 0xAE) {
        serial_ready = 0;
        // Restore normal operation even if not ready
        outb(SERIAL_PORT + 4, 0x0F);
        return;
    }
    outb(SERIAL_PORT + 4, 0x0F);
    serial_ready = 1;
}

int serial_is_ready(void) {
    return serial_ready;
}

void serial_putc(char c) {
    if (!serial_ready)
        return;
    if (c == '\n') {
        while (!serial_transmit_empty()) {
        }
        outb(SERIAL_PORT, '\r');
    }
    while (!serial_transmit_empty()) {
    }
    outb(SERIAL_PORT, (uint8_t)c);
}

void serial_write(const char *s) {
    if (!serial_ready || !s)
        return;
    for (size_t i = 0; s[i]; ++i) {
        serial_putc(s[i]);
    }
}

void serial_write_hex(uint32_t value) {
    const char *digits = "0123456789ABCDEF";
    serial_write("0x");
    for (int i = 7; i >= 0; --i) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        serial_putc(digits[nibble]);
    }
}
