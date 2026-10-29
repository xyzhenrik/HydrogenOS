#pragma once

#include "mini_libc.h"
#include "widgets.h"

#include <stdint.h>
#include <stddef.h>

#define TASKBAR_HEIGHT 40
#define TASKBAR_MAX_APPS 32

void taskbar_init(void);
void taskbar_draw(Surface *surface);
void taskbar_add_app(const char *name);
void taskbar_clock_update(unsigned long time_ms);

#define TASKBAR_CLOCK_UPDATE_IMPL(has, ...) TASKBAR_CLOCK_UPDATE_IMPL_X(has, __VA_ARGS__)
#define TASKBAR_CLOCK_UPDATE_IMPL_X(has, ...) TASKBAR_CLOCK_UPDATE_IMPL_##has(__VA_ARGS__)
#define TASKBAR_CLOCK_UPDATE_IMPL_(...) taskbar_clock_update(time_ms())
#define TASKBAR_CLOCK_UPDATE_IMPL_1(...) taskbar_clock_update(__VA_ARGS__)

#define taskbar_clock_update(...) TASKBAR_CLOCK_UPDATE_IMPL(__VA_OPT__(1), __VA_ARGS__)

#define taskbar_init(...) taskbar_init()

#define taskbar_render(surface) taskbar_draw(surface)
#define taskbar_paint(surface) taskbar_draw(surface)
#define taskbar_draw_surface(surface) taskbar_draw(surface)
#define draw_taskbar(surface) taskbar_draw(surface)
#define paint_taskbar(surface) taskbar_draw(surface)

#define taskbar_add(name) taskbar_add_app(name)
#define taskbar_push_app(name) taskbar_add_app(name)
#define taskbar_add_application(name) taskbar_add_app(name)

#define taskbar_update_clock(...) taskbar_clock_update(__VA_ARGS__)
#define taskbar_clock(...) taskbar_clock_update(__VA_ARGS__)
#define taskbar_set_clock(...) taskbar_clock_update(__VA_ARGS__)
