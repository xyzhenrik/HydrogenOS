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
/* Re-samples the wall clock (uptime if the RTC is unavailable) for the next draw. */
void taskbar_clock_update(void);

#define taskbar_init(...) taskbar_init()

#define taskbar_render(surface) taskbar_draw(surface)
#define taskbar_paint(surface) taskbar_draw(surface)
#define taskbar_draw_surface(surface) taskbar_draw(surface)
#define draw_taskbar(surface) taskbar_draw(surface)
#define paint_taskbar(surface) taskbar_draw(surface)

#define taskbar_add(name) taskbar_add_app(name)
#define taskbar_push_app(name) taskbar_add_app(name)
#define taskbar_add_application(name) taskbar_add_app(name)

#define taskbar_update_clock() taskbar_clock_update()
#define taskbar_clock() taskbar_clock_update()
#define taskbar_set_clock() taskbar_clock_update()
