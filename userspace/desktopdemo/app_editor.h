#pragma once

#include "compositor.h"
#include "settings.h"

#include <stddef.h>

/*
 * The Editor app: a small text editor in a window. It owns one document (a
 * TextEdit), the view onto it, and a prompt bar for Save As, Open and Find.
 * Coordinates handed in are window-local pixels.
 *
 * Keys are the values of sys_kbd_read() (see kbd_keys.h):
 *   Ctrl+S save   Ctrl+R save as   Ctrl+O open   Ctrl+N new   Ctrl+F find
 *   Ctrl+Z undo   Ctrl+Y redo      Ctrl+A/C/X/V select all, copy, cut, paste
 *   Ctrl+L line numbers   Insert toggles overwrite
 * Opening or creating over unsaved changes (and closing the window) asks for
 * the same command a second time before it discards them.
 */
typedef struct Editor Editor;

/* Text lines one notch of the mouse wheel scrolls. */
#define EDITOR_WHEEL_LINES 3

Editor *editor_create(void);
void editor_destroy(Editor *editor);

/* Loads path (relative paths resolve against the working directory). A file
   that does not exist yet becomes a new, empty document bound to that path.
   Returns 0 or a negative errno; failures are also reported in the status bar. */
int editor_open(Editor *editor, const char *path);

/* The document's name for the title bar ("Untitled" when it has no path). */
void editor_title(const Editor *editor, char *out, size_t size);
int editor_modified(const Editor *editor);

void editor_key(Editor *editor, int key);
void editor_mouse_press(Editor *editor, int x, int y, unsigned long now_ms);
void editor_mouse_drag(Editor *editor, int x, int y);
void editor_mouse_release(Editor *editor);
/* Scrolls the view by lines (positive: towards the end of the text). */
void editor_scroll(Editor *editor, int lines);
/* True if the window may close now. With unsaved changes the first request
   is refused and reported in the status bar; repeating it right away passes. */
int editor_close_allowed(Editor *editor);

/* The next editor_draw() repaints everything (new surface, new palette). */
void editor_invalidate(Editor *editor);

#define EDITOR_DRAW_TITLE (1u << 0)

/*
 * Repaints what changed since the last call below the title bar (top is its
 * height) and damages exactly those parts of win. Returns EDITOR_DRAW_TITLE
 * when the title or its modified marker changed and the title bar needs a
 * repaint.
 */
unsigned editor_draw(Editor *editor, Window *win, const SettingsPalette *palette, int top);
