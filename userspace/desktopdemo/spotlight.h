#pragma once

/* The desktop's launcher: a search field over the applications of the session
   and the programs found on disk, with the power controls underneath. It lives
   in one compositor window that the host creates, places and styles; the
   launcher only draws into it and shows or hides it. */

#include "compositor.h"
#include "launcher_model.h"
#include "settings.h"

#define SPOTLIGHT_WIDTH  400
/* The tallest the panel gets; it is shorter when there is less to show. */
#define SPOTLIGHT_HEIGHT 532

typedef LauncherApp SpotlightApp;

typedef struct SpotlightHost {
    const SpotlightApp *apps;   /* listed in this order while the query is empty */
    int app_count;
    void (*draw_icon)(Window *window, int app_id, int x, int y, int size);
    void (*launch)(int app_id);
    /* Optional. Called when the launcher opens or closes, in place of showing
       or hiding the window directly, so the host can animate the change and
       hide the window when it is over. */
    void (*shown_changed)(Window *window, int shown);
} SpotlightHost;

/* The palette and settings are read on every redraw, so theme changes show up
   the next time the launcher is drawn. window must be SPOTLIGHT_WIDTH x
   SPOTLIGHT_HEIGHT; the launcher shortens it as needed and keeps its top edge. */
void spotlight_init(Window *window, const SpotlightHost *host,
                    const SettingsPalette *palette, const Settings *settings);

int spotlight_is_open(void);
void spotlight_open(void);
void spotlight_close(void);
/* Repaints the open launcher, e.g. after the theme changed. */
void spotlight_redraw(void);

/* Feeds a sys_kbd_read() value. Returns nonzero when the launcher used it:
   everything while it is open, and the launcher key while it is closed. */
int spotlight_key(long key);

/* Pointer input in screen coordinates. Both are no-ops while closed.
   spotlight_press() returns nonzero when the press landed on the launcher; a
   press elsewhere leaves it to the caller, which usually closes the launcher. */
int spotlight_press(int x, int y);
void spotlight_hover(int x, int y);

/* Collects programs started from the launcher once they have exited. */
void spotlight_service(void);
