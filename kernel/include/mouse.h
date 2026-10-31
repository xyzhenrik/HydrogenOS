#pragma once

#include <stdint.h>

struct mouse_packet {
    int8_t dx;
    int8_t dy;
    uint8_t buttons; // bit0=left, bit1=right, bit2=middle
};

void mouse_init(void);
void mouse_handle_irq(void);
int mouse_read_packet(struct mouse_packet *out); // returns 1 if packet available
struct mouse_packet mouse_peek_last(void);

void mouse_set_bounds(int width, int height);
int mouse_get_pos(int *x, int *y); // returns 1 if valid
uint8_t mouse_get_buttons(void);
int mouse_take_wheel(void);
void mouse_poll(void);             // liest Mausdaten ohne IRQ

/* Absolute pointing devices (virtio tablet). Coordinates are device units in
   [0, range] per axis, scaled onto the bounds set by mouse_set_bounds().
   The most recent report from any device, relative or absolute, wins. */
void mouse_set_position_scaled(uint32_t x, uint32_t x_range, uint32_t y, uint32_t y_range);
void mouse_set_buttons(uint8_t buttons);   // bit0=left, bit1=right, bit2=middle
void mouse_add_wheel(int delta);           // positive = towards the user (scroll down)
