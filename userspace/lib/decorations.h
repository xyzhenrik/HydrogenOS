#pragma once

#include "mini_libc.h"
#include "widgets.h"

#define DECOR_BTN_NONE       0
#define DECOR_BTN_CLOSE      1
#define DECOR_BTN_MINIMIZE   2
#define DECOR_BTN_MAXIMIZE   3

#define DECOR_BUTTON_NONE     DECOR_BTN_NONE
#define DECOR_BUTTON_CLOSE    DECOR_BTN_CLOSE
#define DECOR_BUTTON_MINIMIZE DECOR_BTN_MINIMIZE
#define DECOR_BUTTON_MAXIMIZE DECOR_BTN_MAXIMIZE

#define DECOR_CLOSE  DECOR_BTN_CLOSE
#define DECOR_MIN    DECOR_BTN_MINIMIZE
#define DECOR_MAX    DECOR_BTN_MAXIMIZE
#define DECOR_NONE   DECOR_BTN_NONE

void decor_draw(Surface *window_surface, const char *title);
int decor_hit_test(int x, int y);

void decor_draw_titlebar(Surface *window_surface, const char *title);
int  decor_hit_test_xy(int x, int y);
int  decor_hit_test_surface(Surface *window_surface, int x, int y);

void decorations_draw(Surface *window_surface, const char *title);
void window_decor_draw(Surface *window_surface, const char *title);
void draw_decorations(Surface *window_surface, const char *title);
int  decorations_hit_test(int x, int y);
int  window_decor_hit_test(int x, int y);