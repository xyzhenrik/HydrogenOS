#include "keyboard.h"
#include "io.h"
#include "console.h"
#include "drivers/virtio_input.h"
#include "core/signal.h"

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
    [0x56] = '<',
};

static const char scancode_table_shift[128] = {
    0,   0, '!','"','$','$','%','&','/','(',')','=','?','`','\b',
    '\t','Q','W','E','R','T','Z','U','I','O','P','U','*','\n', 0,
    'A','S','D','F','G','H','J','K','L','O','A','^', 0, '\'',
    'Y','X','C','V','B','N','M',';',':','_','*', 0,   ' ', 0,
    0,
    [0x56] = '>',
};

static const char scancode_table_altgr[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '$', [0x06] = '%', [0x07] = '&',
    [0x08] = '{', [0x09] = '[', [0x0A] = ']', [0x0B] = '}', [0x0C] = '\\',
    [0x10] = '@', [0x1B] = '~', [0x56] = '|',
};

/* US layout (scancode set 1). */
static const char scancode_table_us[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
    [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
    [0x0E]='\b',[0x0F]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',
    [0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',
    [0x1C]='\n',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
    [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',[0x29]='`',
    [0x2B]='\\',
    [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',
    [0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',[0x39]=' ',
};

static const char scancode_table_us_shift[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',
    [0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',[0x0C]='_',[0x0D]='+',
    [0x0E]='\b',[0x0F]='\t',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',
    [0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',[0x1A]='{',[0x1B]='}',
    [0x1C]='\n',
    [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',
    [0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',[0x29]='~',
    [0x2B]='|',
    [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',
    [0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',[0x39]=' ',
};

static int g_layout = 0; /* 0 = DE, 1 = US */

void keyboard_set_layout(int layout) { g_layout = layout ? 1 : 0; }
int keyboard_get_layout(void) { return g_layout; }

static inline void buffer_put(uint16_t c) {
    /* Ctrl+C interrupts the console's foreground job instead of being input. */
    if (c == 0x03 && signal_console_interrupt())
        return;
    uint8_t next = (uint8_t)((buf_head + 1) % KB_BUF_SIZE);
    if (next != buf_tail) {
        keybuf[buf_head] = c;
        buf_head = next;
    }
}

int keyboard_read_event(void) {
    if (buf_head == buf_tail) return -1;
    uint16_t c = keybuf[buf_tail];
    buf_tail = (uint8_t)((buf_tail + 1) % KB_BUF_SIZE);
    return (int)(c);
}

/* Text consoles have no use for modifier flags on navigation keys. */
int keyboard_read_char(void) {
    int c = keyboard_read_event();
    return c < 0 ? c : (c & KEY_CODE_MASK);
}

void keyboard_handle_scancode(uint8_t scancode) {
    static int shift = 0;
    static int extended = 0;
    static int altgr = 0;
    static int ctrl = 0;
    static int alt = 0;
    static int caps = 0;

    if (scancode == 0xE0) { extended = 1; return; }
    if (scancode & 0x80) {
        uint8_t code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) shift = 0;
        if (code == 0x1D) ctrl = 0;
        if (!extended && code == 0x38) alt = 0;
        if (extended && code == 0x38) altgr = 0;
        extended = 0;
        return;
    }
    /* Modifier presses must also consume a pending E0 prefix (AltGr, right Ctrl),
     * otherwise the next key press is misread as an extended key and dropped. */
    if (scancode == 0x2A || scancode == 0x36) { shift = 1; extended = 0; return; }
    if (scancode == 0x1D) { ctrl = 1; extended = 0; return; }
    if (!extended && scancode == 0x38) { alt = 1; return; }
    if (extended && scancode == 0x38) { altgr = 1; extended = 0; return; }
    if (scancode == 0x3A) { caps = !caps; return; }

    if (scancode == 0x0F && alt) {
        buffer_put(KEY_ALT_TAB);
        extended = 0;
        return;
    }

    if (scancode == 0x39) { // Space
        buffer_put(' ');
        return;
    }

    if (scancode == 0x01 && !extended) {
        buffer_put(KEY_ESCAPE);
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
            case 0x49: code = KEY_PAGE_UP; break;
            case 0x51: code = KEY_PAGE_DOWN; break;
            case 0x52: code = KEY_INSERT; break;
            default: break;
        }
        if (code) {
            if (shift) code |= KEY_MOD_SHIFT;
            if (ctrl) code |= KEY_MOD_CTRL;
            buffer_put(code);
        }
        extended = 0;
        return;
    }

    const char *table;
    if (g_layout) {
        table = shift ? scancode_table_us_shift : scancode_table_us;
        if (altgr) table = scancode_table_us;
    } else {
        table = altgr ? scancode_table_altgr : (shift ? scancode_table_shift : scancode_table);
    }
    if (scancode < 128) {
        char c = table[scancode];
        if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) c ^= 0x20;
        if (c) {
            if (ctrl) {
                if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
                else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
            }
            if (c >= 0x20 || c == '\n' || c == '\b' || c == '\t' || c < 0x20) {
                buffer_put((uint16_t)c);
            }
        }
    }
    extended = 0;
}

/* Linux evdev key codes up to KEY_F12 match PS/2 set-1 make codes; the keys
 * below carry an E0 prefix there. Feeding virtio-input through the PS/2 path
 * keeps layouts, AltGr and Caps Lock identical for both devices. */
static void virtio_feed_key(int code, int value) {
    uint8_t extended = 0;
    uint8_t scancode;

    if (code > 0 && code <= 0x58) {
        scancode = (uint8_t)code;
    } else {
        switch (code) {
            case 97:  extended = 1; scancode = 0x1D; break; /* KEY_RIGHTCTRL */
            case 100: extended = 1; scancode = 0x38; break; /* KEY_RIGHTALT */
            case 102: extended = 1; scancode = 0x47; break; /* KEY_HOME */
            case 103: extended = 1; scancode = 0x48; break; /* KEY_UP */
            case 105: extended = 1; scancode = 0x4B; break; /* KEY_LEFT */
            case 106: extended = 1; scancode = 0x4D; break; /* KEY_RIGHT */
            case 107: extended = 1; scancode = 0x4F; break; /* KEY_END */
            case 108: extended = 1; scancode = 0x50; break; /* KEY_DOWN */
            case 104: extended = 1; scancode = 0x49; break; /* KEY_PAGEUP */
            case 109: extended = 1; scancode = 0x51; break; /* KEY_PAGEDOWN */
            case 110: extended = 1; scancode = 0x52; break; /* KEY_INSERT */
            case 111: extended = 1; scancode = 0x53; break; /* KEY_DELETE */
            default: return;
        }
    }

    if (extended) keyboard_handle_scancode(0xE0);
    keyboard_handle_scancode(value ? scancode : (uint8_t)(scancode | 0x80));
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
        if (status & 0x20) {
            /* fall through to virtio handling below */
        } else {
            uint8_t sc = inb(0x60);
            keyboard_handle_scancode(sc);
        }
    }

    if (virtio_input_present()) {
        for (;;) {
            int key = virtio_input_get_key_event();
            if (key < 0) break;
            virtio_feed_key(key, virtio_input_last_key_value);
        }
    }
}
