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
  KEY_ALT_TAB,
};

int keyboard_read_char(void); // returns -1 if none
void keyboard_set_layout(int layout);
int keyboard_get_layout(void);
void keyboard_poll(void);     // liest Status/Scancode ohne IRQ
