#pragma once

#include "compositor.h"

/* Window-manager rules that do not depend on any state of the session:
   where windows may go, and what a click on a taskbar icon does. Kept pure
   so desktoptest can pin them down. */

/* A window can be dragged partly beyond the left and right screen edges, but
   at least this much of its title bar stays reachable. */
#define WM_MIN_TITLE_VISIBLE 96

/* The part of the screen windows live in: below the top bar, above the
   taskbar. */
static inline CompRect wm_work_area(int screen_w, int screen_h, int top_inset, int bottom_inset) {
    CompRect area = { 0, top_inset, screen_w, screen_h - top_inset - bottom_inset };
    if (area.h < 0) area.h = 0;
    return area;
}

/* Clamps the top-left corner of a window of width w so its title bar (height
   title_h) never leaves the work area: it cannot rise above the work area,
   sink below it, or slide so far sideways that fewer than
   WM_MIN_TITLE_VISIBLE pixels of it remain on screen. */
static inline void wm_clamp_position(const CompRect *work, int w, int title_h, int *x, int *y) {
    int visible = w < WM_MIN_TITLE_VISIBLE ? w : WM_MIN_TITLE_VISIBLE;
    int min_x = work->x + visible - w;
    int max_x = work->x + work->w - visible;
    int min_y = work->y;
    int max_y = work->y + work->h - title_h;
    if (max_y < min_y) max_y = min_y;
    if (*x < min_x) *x = min_x;
    if (*x > max_x) *x = max_x;
    if (*y < min_y) *y = min_y;
    if (*y > max_y) *y = max_y;
}

typedef enum WmDockAction {
    WM_DOCK_LAUNCH,    /* no window of the app: start it */
    WM_DOCK_RESTORE,   /* minimized: show it again and focus it */
    WM_DOCK_RAISE,     /* open but behind or unfocused: bring it to the front */
    WM_DOCK_MINIMIZE   /* already the focused window: put it away */
} WmDockAction;

static inline WmDockAction wm_dock_action(int has_window, int minimized, int focused) {
    if (!has_window) return WM_DOCK_LAUNCH;
    if (minimized) return WM_DOCK_RESTORE;
    return focused ? WM_DOCK_MINIMIZE : WM_DOCK_RAISE;
}

/* Two presses count as a double click within this time and distance. */
#define WM_DOUBLE_CLICK_MS 400
#define WM_DOUBLE_CLICK_SLOP 4

static inline int wm_is_double_click(unsigned long previous_ms, int previous_x, int previous_y,
                                     unsigned long now_ms, int x, int y) {
    int dx = x - previous_x;
    int dy = y - previous_y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return now_ms - previous_ms <= WM_DOUBLE_CLICK_MS &&
           dx <= WM_DOUBLE_CLICK_SLOP && dy <= WM_DOUBLE_CLICK_SLOP;
}
