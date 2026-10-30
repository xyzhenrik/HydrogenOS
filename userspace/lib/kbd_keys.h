#pragma once

/*
 * Values returned by sys_kbd_read(). Printable keys and control characters
 * (Ctrl+letter is 1..26, Enter '\n', Backspace '\b', Tab '\t') come as their
 * ASCII code; the keys below have no character and start at 0x100. Navigation
 * keys carry the modifiers held at the time as flag bits.
 * Keep in step with kernel/include/keyboard.h.
 */
enum {
    KBD_KEY_LEFT = 0x100,
    KBD_KEY_RIGHT,
    KBD_KEY_UP,
    KBD_KEY_DOWN,
    KBD_KEY_HOME,
    KBD_KEY_END,
    KBD_KEY_DELETE,
    KBD_KEY_PAGE_UP,
    KBD_KEY_PAGE_DOWN,
    KBD_KEY_INSERT,
    KBD_KEY_ESCAPE
};

#define KBD_MOD_SHIFT 0x1000
#define KBD_MOD_CTRL  0x2000
#define KBD_CODE_MASK 0x0FFF
