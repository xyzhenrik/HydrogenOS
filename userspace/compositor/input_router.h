#pragma once

#include "mini_libc.h"
#include "compositor.h"

#define INPUT_ROUTER_QUEUE_CAPACITY 64
#define INPUT_ROUTER_MAX_QUEUES     256

typedef enum {
    INPUT_EVENT_KEY = 0,
    INPUT_EVENT_MOUSE = 1
} InputEventType;

typedef struct {
    int type;
    int window_id;
    int key;
    int pressed;
    int x;
    int y;
    int buttons;
} InputEvent;

int input_router_init(void *comp);
int input_router_key(int key, int pressed);
int input_router_mouse(int x, int y, int buttons);
int input_router_set_focus(int window_id);

int input_router_next_event(int window_id, InputEvent *out_event);
int input_router_pending(int window_id);
