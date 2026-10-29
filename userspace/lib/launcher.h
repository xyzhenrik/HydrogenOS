#pragma once

#include "mini_libc.h"
#include "widgets.h"

#include <stdint.h>
#include <stddef.h>

#define LAUNCHER_MAX_APPS    64
#define LAUNCHER_QUERY_CAP   128
#define LAUNCHER_RESULT_CAP  64

typedef struct LauncherApp {
    char name[64];
    char path[128];
    char keywords[128];
} LauncherApp;

typedef struct Launcher Launcher;
typedef Launcher launcher_t;
typedef Launcher Launcher_t;

struct Launcher {
    int visible;

    char query[LAUNCHER_QUERY_CAP];
    int cursor;

    LauncherApp apps[LAUNCHER_MAX_APPS];
    int app_count;

    int filtered[LAUNCHER_RESULT_CAP];
    int filtered_count;
    int selected;

    char selected_path[128];

    int x;
    int y;
    int w;
    int h;

    uint32_t bg_color;
    uint32_t fg_color;
    uint32_t accent_color;
    uint32_t input_bg;

    void (*on_select)(const char *path);
    void *userdata;
    void *user_data;
    void *context;
};

/* Core instance API. */
Launcher *launcher_create(void);
void launcher_destroy(Launcher *launcher);

Launcher *launcher_init_0(void);
Launcher *launcher_init_1(uintptr_t a);
Launcher *launcher_init_2(uintptr_t a, uintptr_t b);
Launcher *launcher_init_3(uintptr_t a, uintptr_t b, uintptr_t c);
Launcher *launcher_init_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

void launcher_show_0(void);
void launcher_show_1(uintptr_t a);
void launcher_show_2(uintptr_t a, uintptr_t b);
void launcher_show_3(uintptr_t a, uintptr_t b, uintptr_t c);
void launcher_show_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

void launcher_hide_0(void);
void launcher_hide_1(uintptr_t a);
void launcher_hide_2(uintptr_t a, uintptr_t b);
void launcher_hide_3(uintptr_t a, uintptr_t b, uintptr_t c);
void launcher_hide_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

int launcher_handle_key_0(void);
int launcher_handle_key_1(uintptr_t a);
int launcher_handle_key_2(uintptr_t a, uintptr_t b);
int launcher_handle_key_3(uintptr_t a, uintptr_t b, uintptr_t c);
int launcher_handle_key_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

void launcher_draw_0(void);
void launcher_draw_1(uintptr_t a);
void launcher_draw_2(uintptr_t a, uintptr_t b);
void launcher_draw_3(uintptr_t a, uintptr_t b, uintptr_t c);
void launcher_draw_4(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d);

/* Helpers / convenience. */
Launcher *launcher_get(void);
int launcher_is_visible(Launcher *launcher);
const char *launcher_selected_path(Launcher *launcher);
const char *launcher_get_selected_path(Launcher *launcher);

void launcher_add_app(Launcher *launcher, const char *name, const char *path, const char *keywords);
void launcher_set_apps(Launcher *launcher, const LauncherApp *apps, int count);
void launcher_clear_apps(Launcher *launcher);

void launcher_set_pos(Launcher *launcher, int x, int y);
void launcher_set_size(Launcher *launcher, int w, int h);

/* Simple global API (primary). */
void launcher_init(void);
void launcher_show(void);
void launcher_hide(void);
int launcher_handle_key(int key);
/* The panel is glass with a soft shadow over the surface's current contents:
   the caller must restore the backdrop beneath the panel and its shadow before
   every redraw, otherwise both accumulate. */
void launcher_draw(Surface *surface);