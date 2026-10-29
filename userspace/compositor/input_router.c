#include "input_router.h"

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);

static void *ir_comp = 0;
static int ir_focused_window_id = 0;

typedef struct {
    int window_id;
    int used;
    InputEvent buf[INPUT_ROUTER_QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
} IrQueue;

static IrQueue ir_queues[INPUT_ROUTER_MAX_QUEUES];

static IrQueue *ir_find_queue(int window_id, int create) {
    for (int i = 0; i < INPUT_ROUTER_MAX_QUEUES; ++i) {
        if (ir_queues[i].used && ir_queues[i].window_id == window_id) {
            return &ir_queues[i];
        }
    }
    if (!create) return 0;
    for (int i = 0; i < INPUT_ROUTER_MAX_QUEUES; ++i) {
        if (!ir_queues[i].used) {
            memset(&ir_queues[i], 0, sizeof(IrQueue));
            ir_queues[i].used = 1;
            ir_queues[i].window_id = window_id;
            return &ir_queues[i];
        }
    }
    return 0;
}

static int ir_enqueue(int window_id, const InputEvent *ev) {
    if (window_id <= 0) return -1;
    IrQueue *q = ir_find_queue(window_id, 1);
    if (!q) return -1;
    if (q->count >= INPUT_ROUTER_QUEUE_CAPACITY) return -1;
    q->buf[q->tail] = *ev;
    q->tail = (q->tail + 1) % INPUT_ROUTER_QUEUE_CAPACITY;
    q->count++;
    return 0;
}

int input_router_init(void *comp) {
    ir_comp = comp;
    ir_focused_window_id = 0;
    for (int i = 0; i < INPUT_ROUTER_MAX_QUEUES; ++i) {
        ir_queues[i].used = 0;
        ir_queues[i].window_id = 0;
        ir_queues[i].head = 0;
        ir_queues[i].tail = 0;
        ir_queues[i].count = 0;
    }
    return 0;
}

int input_router_set_focus(int window_id) {
    if (window_id != 0) {
        Window *win = comp_find_window(window_id);
        if (!win) return -1;
    }
    ir_focused_window_id = window_id;
    if (window_id != 0) {
        ir_find_queue(window_id, 1);
    }
    return 0;
}

int input_router_key(int key, int pressed) {
    if (ir_focused_window_id == 0) return -1;
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EVENT_KEY;
    ev.window_id = ir_focused_window_id;
    ev.key = key;
    ev.pressed = pressed;
    return ir_enqueue(ir_focused_window_id, &ev);
}

int input_router_mouse(int x, int y, int buttons) {
    if (ir_focused_window_id == 0) return -1;
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EVENT_MOUSE;
    ev.window_id = ir_focused_window_id;
    ev.x = x;
    ev.y = y;
    ev.buttons = buttons;
    return ir_enqueue(ir_focused_window_id, &ev);
}

int input_router_next_event(int window_id, InputEvent *out_event) {
    if (!out_event || window_id <= 0) return -1;
    IrQueue *q = ir_find_queue(window_id, 0);
    if (!q || q->count == 0) return -1;
    *out_event = q->buf[q->head];
    q->head = (q->head + 1) % INPUT_ROUTER_QUEUE_CAPACITY;
    q->count--;
    return 0;
}

int input_router_pending(int window_id) {
    IrQueue *q = ir_find_queue(window_id, 0);
    if (!q) return 0;
    return q->count;
}
