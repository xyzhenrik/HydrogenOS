#include "keyboard.h"
#include "io.h"
#include "console.h"

#define KB_BUF_SIZE 128

static uint16_t keybuf[KB_BUF_SIZE];
static uint8_t buf_head = 0;
static uint8_t buf_tail = 0;

// German layout (DE) Scancode Set 1
// Umlaut/Fn Keys mapped to ASCII fallbacks (ä->a, ö->o, ü->u, ß->s)
static const char scancode_table[128] = {
    0,   0, '1','2','3','4','5','6','7','8','9','0','s','\'','\b',
    '\t','q','w','e','r','t','z','u','i','o','p','u','+', '\n', 0,
    'a','s','d','f','g','h','j','k','l','o','a','^', 0, '#',
    'y','x','c','v','b','n','m',',','.','-','*', 0,   ' ', 0,
    0,
};

static const char scancode_table_shift[128] = {
    0,   0, '!','"','$','$','%','&','/','(',')','=','?','`','\b',
    '\t','Q','W','E','R','T','Z','U','I','O','P','U','*','\n', 0,
    'A','S','D','F','G','H','J','K','L','O','A','^', 0, '\'',
    'Y','X','C','V','B','N','M',';',':','_','*', 0,   ' ', 0,
    0,
};

static const char scancode_table_altgr[128] = {
    0,   0, '1','2','3', '$', '%', '&', '{','[',']','}','\\', 0, 0,
    0,  '@', 0,  'E', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '~',
    0, 0, 0, 0, 0, 0, 'u', 0, 0, 0, 0, 0,  '|', 0,
    0,
};

static inline void buffer_put(uint16_t c) {
    uint8_t next = (uint8_t)((buf_head + 1) % KB_BUF_SIZE);
    if (next != buf_tail) {
        keybuf[buf_head] = c;
        buf_head = next;
    }
}

int keyboard_read_char(void) {
    if (buf_head == buf_tail) return -1;
    uint16_t c = keybuf[buf_tail];
    buf_tail = (uint8_t)((buf_tail + 1) % KB_BUF_SIZE);
    return (int)(c);
}

void keyboard_handle_scancode(uint8_t scancode) {
    static int shift = 0;
    static int extended = 0;
    static int altgr = 0;

    if (scancode == 0xE0) { extended = 1; return; }
    if (scancode & 0x80) {
        uint8_t code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) shift = 0;
        if (extended && code == 0x38) altgr = 0;
        extended = 0;
        return;
    }
    if (scancode == 0x2A || scancode == 0x36) { shift = 1; return; }
    if (extended && scancode == 0x38) { altgr = 1; return; }

    if (scancode == 0x39) { // Space
        buffer_put(' ');
        return;
    }

    if (extended) {
        uint16_t code = 0;
        switch (scancode) {
            case 0x4B: code = KEY_LEFT; break;
            case 0x4D: code = KEY_RIGHT; break;
            case 0x48: code = KEY_UP; break;
            case 0x50: code = KEY_DOWN; break;
            case 0x47: code = KEY_HOME; break;
            case 0x4F: code = KEY_END; break;
            case 0x53: code = KEY_DEL; break;
            default: break;
        }
        if (code) buffer_put(code);
        extended = 0;
        return;
    }

    const char *table = altgr ? scancode_table_altgr : (shift ? scancode_table_shift : scancode_table);
    if (scancode < 128) {
        char c = table[scancode];
        if (c) {
            if (c >= 0x20 || c == '\n' || c == '\b') {
                buffer_put((uint16_t)c); // Echo übernimmt Shell
            }
        }
    }
    extended = 0;
}

void keyboard_init(void) {
    // nothing needed yet; IRQ unmask already in kernel64.c
    // clear buffer
    buf_head = buf_tail = 0;
}

void keyboard_poll(void) {
    uint8_t status = inb(0x64);
    if (status & 0x01) {
        // Bit 5 indicates mouse data; leave it for mouse_poll()
        if (status & 0x20) return;
        uint8_t sc = inb(0x60);
        keyboard_handle_scancode(sc);
    }
}
