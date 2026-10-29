#pragma once

#include "mini_libc.h"
#include "syscall.h"

#define COMP_MAX_WINDOWS   256
#define COMP_MAX_DAMAGES   256
#define COMP_DEFAULT_WIDTH  1024
#define COMP_DEFAULT_HEIGHT 768

/* Window structure (v0) */
struct Window {
    int id;
    int x, y, w, h;
    void *surface;
    int visible;
    int focused;
    unsigned int bg_color;
    int damaged;
};

struct window {
    int id;
    int x, y, w, h;
    void *surface;
    int visible;
    int focused;
    unsigned int bg_color;
    int damaged;
};

struct comp_window {
    int id;
    int x, y, w, h;
    void *surface;
    int visible;
    int focused;
    unsigned int bg_color;
    int damaged;
};

typedef struct Window Window;
typedef struct window window;
typedef struct comp_window comp_window_t;

typedef struct {
    int x, y, w, h;
} CompRect;

/* Display / compositor state */
int comp_init_default(void);
int comp_init_one(int width);
int comp_init_size(int width, int height, ...);

/* Window management */
Window *comp_create_window4(int x, int y, int w, int h);
Window *comp_create_window5(int x, int y, int w, int h, void *surface);
int comp_destroy_window(Window *win);
int comp_raise(Window *win);
int comp_focus(Window *win);

/* Damage tracking */
int comp_damage_window(Window *win);
int comp_damage_rectp(Window *win, const CompRect *rect);
int comp_damage_rect(Window *win, int x, int y, int w, int h);
int comp_damage_all(void);

/* Rendering */
int comp_render(void);
int comp_present(void);
int comp_present_damage(void);
int comp_present_rect(int x, int y, int w, int h);
void comp_flip(void);
uint32_t *comp_front_buffer(void);
uint32_t *comp_back_buffer(void);
int comp_width(void);
int comp_height(void);

/* Event loop */
void comp_event_loop(void);

/* Helpers */
Window *comp_find_window(int id);
Window *comp_get_window(int id);
int comp_window_count(void);

/* Dispatch helpers for flexible argument counts */
int _comp_damage_1(Window *win);
int _comp_damage_2(Window *win, const CompRect *rect);
int _comp_damage_3(Window *win, int x, int y);
int _comp_damage_4(Window *win, int x, int y, int w);
int _comp_damage_5(Window *win, int x, int y, int w, int h);

/* Overload macros */
#define COMP_INIT_GET_MACRO(_1, _2, NAME, ...) NAME
#define comp_init(...) comp_init_size(__VA_ARGS__ __VA_OPT__(,) 1024, 768)

#define COMP_CREATE_WINDOW_GET_MACRO(_1, _2, _3, _4, _5, NAME, ...) NAME
#define comp_create_window(...) COMP_CREATE_WINDOW_GET_MACRO(__VA_ARGS__, comp_create_window5, comp_create_window4, comp_create_window4, comp_create_window4, comp_create_window4)(__VA_ARGS__)

#define COMP_DAMAGE_GET_MACRO(_1, _2, _3, _4, _5, NAME, ...) NAME
#define comp_damage(...) COMP_DAMAGE_GET_MACRO(__VA_ARGS__, _comp_damage_5, _comp_damage_4, _comp_damage_3, _comp_damage_2, _comp_damage_1)(__VA_ARGS__)