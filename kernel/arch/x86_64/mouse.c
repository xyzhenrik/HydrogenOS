#include "mouse.h"
#include "io.h"
#include <stddef.h>
#include <stdint.h>

#define MOUSE_BUF_SIZE 64

static struct mouse_packet buf[MOUSE_BUF_SIZE];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;
static struct mouse_packet last_pkt = {0, 0, 0};
static int mouse_x = 0;
static int mouse_y = 0;
static int wheel_delta = 0;
static int four_byte_mode = 0;
static int bounds_w = 0;
static int bounds_h = 0;

static void buffer_put(struct mouse_packet p) {
    uint8_t next = (uint8_t)((head + 1) % MOUSE_BUF_SIZE);
    if (next == tail) {
        // drop oldest to make room
        tail = (uint8_t)((tail + 1) % MOUSE_BUF_SIZE);
    }
    buf[head] = p;
    head = next;
    last_pkt = p;
}

int mouse_read_packet(struct mouse_packet *out) {
    if (tail == head) return 0;
    *out = buf[tail];
    tail = (uint8_t)((tail + 1) % MOUSE_BUF_SIZE);
    return 1;
}

struct mouse_packet mouse_peek_last(void) { return last_pkt; }

void mouse_set_bounds(int width, int height) {
    bounds_w = width;
    bounds_h = height;
    if (bounds_w < 0) bounds_w = 0;
    if (bounds_h < 0) bounds_h = 0;
    mouse_x = bounds_w / 2;
    mouse_y = bounds_h / 2;
}

int mouse_get_pos(int *x, int *y) {
    if (bounds_w == 0 || bounds_h == 0) return 0;
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    return 1;
}

uint8_t mouse_get_buttons(void) { return last_pkt.buttons; }

int mouse_take_wheel(void) {
    int w = wheel_delta;
    wheel_delta = 0;
    return w;
}

static inline void mouse_wait_write(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((inb(0x64) & 0x02) == 0) return;
    }
}

static inline uint8_t mouse_wait_read(void) {
    for (;;) {
        if (inb(0x64) & 0x01) return 1;
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(0x64, 0xD4); // write to mouse
    mouse_wait_write();
    outb(0x60, data);
    mouse_wait_read();
    inb(0x60); // ack
}

static uint8_t packet_cycle = 0;
/* Four slots: IntelliMouse mode appends a wheel byte to the 3-byte packet. */
static uint8_t packet_bytes[4];

static void mouse_handle_byte(uint8_t data) {
    if (four_byte_mode && packet_cycle == 3) {
        packet_bytes[3] = data;
        packet_cycle = 0;
        wheel_delta += (int8_t)(packet_bytes[3] << 4) >> 4;
        int8_t dx = (int8_t)packet_bytes[1];
        int8_t dy = (int8_t)packet_bytes[2];
        struct mouse_packet p = { .dx = dx, .dy = dy,
                                  .buttons = (uint8_t)(packet_bytes[0] & 0x07) };
        buffer_put(p);
        if (bounds_w && bounds_h) {
            mouse_x += dx;
            mouse_y -= dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= bounds_w) mouse_x = bounds_w - 1;
            if (mouse_y >= bounds_h) mouse_y = bounds_h - 1;
        }
        return;
    }
    if (packet_cycle == 0) {
        if (data & 0x08) { /* sync byte */
            packet_bytes[0] = data;
            packet_cycle = 1;
        }
        return;
    }
    if (packet_cycle == 1) {
        packet_bytes[1] = data;
        packet_cycle = 2;
        return;
    }
    if (four_byte_mode && packet_cycle == 2) {
        packet_bytes[2] = data;
        packet_cycle = 3;
        return;
    }
    packet_bytes[2] = data;
    packet_cycle = 0;
    int8_t dx = (int8_t)packet_bytes[1];
    int8_t dy = (int8_t)packet_bytes[2];
    struct mouse_packet p = {
        .dx = dx,
        .dy = dy,
        .buttons = (uint8_t)(packet_bytes[0] & 0x07),
    };
    buffer_put(p);
    if (bounds_w && bounds_h) {
        mouse_x += dx;
        mouse_y -= dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= bounds_w) mouse_x = bounds_w - 1;
        if (mouse_y >= bounds_h) mouse_y = bounds_h - 1;
    }
}

void mouse_handle_irq(void) {
    uint8_t status = inb(0x64);
    if ((status & 0x20) == 0) return; // not from mouse
    uint8_t data = inb(0x60);
    mouse_handle_byte(data);
}

void mouse_poll(void) {
    for (int i = 0; i < 4; ++i) {
        uint8_t status = inb(0x64);
        if ((status & 0x01) == 0) break;
        if ((status & 0x20) == 0) {
            // keyboard data pending; leave it for keyboard_poll
            break;
        }
        uint8_t data = inb(0x60);
        mouse_handle_byte(data);
    }
}

void mouse_init(void) {
    // Enable auxiliary device
    mouse_wait_write();
    outb(0x64, 0xA8);

    // Enable IRQ12 in controller status byte
    mouse_wait_write();
    outb(0x64, 0x20); // read command byte
    mouse_wait_read();
    uint8_t status = inb(0x60);
    status |= 0x02;   // enable mouse IRQ12
    status &= ~(0x20); // clear translation
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    // Reset, then try IntelliMouse (4-byte packets with wheel).
    mouse_write(0xF6);
    mouse_write(0xF3); mouse_write(200);
    mouse_write(0xF3); mouse_write(100);
    mouse_write(0xF3); mouse_write(80);
    mouse_write(0xF2);
    {
        uint8_t id = 0;
        for (int i = 0; i < 100000; ++i) {
            if (inb(0x64) & 0x01) { id = inb(0x60); break; }
        }
        four_byte_mode = (id == 3) ? 1 : 0;
    }
    mouse_write(0xF4); // enable streaming

    // Reset buffers
    head = tail = 0;
    last_pkt.dx = last_pkt.dy = 0;
    last_pkt.buttons = 0;
}
