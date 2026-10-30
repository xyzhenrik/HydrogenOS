#pragma once

#include <stdint.h>

void keyboard_init(void);
void keyboard_handle_scancode(uint8_t scancode);

enum {
  KEY_LEFT = 0x100,
  KEY_RIGHT,
  KEY_UP,
  KEY_DOWN,
  KEY_HOME,
  KEY_END,
  KEY_DEL,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN,
  KEY_INSERT,
  KEY_ESCAPE,
};

/* Navigation keys carry the modifiers held when they were pressed. */
#define KEY_MOD_SHIFT 0x1000
#define KEY_MOD_CTRL  0x2000
#define KEY_CODE_MASK 0x0FFF

int keyboard_read_char(void);  // key code without modifiers; -1 if none
int keyboard_read_event(void); // key code with KEY_MOD_* flags; -1 if none
void keyboard_set_layout(int layout);
int keyboard_get_layout(void);
void keyboard_poll(void);     // liest Status/Scancode ohne IRQ
