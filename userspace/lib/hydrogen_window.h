#pragma once

#include "mini_libc.h"

typedef struct hydrogen_window hydrogen_window_t;
typedef hydrogen_window_t HydrogenWindow;
typedef hydrogen_window_t hydrogen_window;

typedef enum hydrogen_window_event_type {
    HYDROGEN_WINDOW_EVENT_NONE = 0,
    HYDROGEN_WINDOW_EVENT_CLOSE = 1,
    HYDROGEN_WINDOW_EVENT_KEY_DOWN = 2,
    HYDROGEN_WINDOW_EVENT_KEY_UP = 3,
    HYDROGEN_WINDOW_EVENT_MOUSE_MOVE = 4,
    HYDROGEN_WINDOW_EVENT_MOUSE_BUTTON_DOWN = 5,
    HYDROGEN_WINDOW_EVENT_MOUSE_BUTTON_UP = 6,
    HYDROGEN_WINDOW_EVENT_RESIZE = 7,
    HYDROGEN_WINDOW_EVENT_EXPOSE = 8
} hydrogen_window_event_type_t;

typedef struct hydrogen_window_event {
    int type;
    int key;
    int mouse_x;
    int mouse_y;
    int button;
    int width;
    int height;
} hydrogen_window_event_t;

typedef hydrogen_window_event_t HydrogenWindowEvent;
typedef hydrogen_window_event_t hydrogen_window_event;

hydrogen_window_t *hydrogen_window_create(int width, int height, const char *title);
int hydrogen_window_destroy(hydrogen_window_t *window);
uint32_t *hydrogen_window_surface_get(hydrogen_window_t *window);
int hydrogen_window_present(hydrogen_window_t *window);
int hydrogen_window_poll_event(hydrogen_window_t *window,
                              hydrogen_window_event_t *event);