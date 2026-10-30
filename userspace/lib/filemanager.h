#pragma once

#include "compositor.h"
#include "gfx_effects.h"
#include "settings.h"

#include <stddef.h>

/* The Files app: places on the left, a toolbar with navigation, a clickable
   path and file actions on top, the folder as icons or as a list in the
   middle and a status bar below. It paints into a compositor window below the
   caller's title bar; launching programs and opening documents is left to the
   caller, which learns about them through fm_take_action(). */

/* A window of at least this size (including the caller's title bar) shows
   the whole toolbar. */
#define FM_MIN_WIDTH  520
#define FM_MIN_HEIGHT 340

#define FM_PATH_CAP    256
#define FM_NAME_CAP    256
#define FM_EDIT_CAP    128
#define FM_NOTE_CAP    96
#define FM_HISTORY     16

/* Keys beyond ASCII as delivered by sys_kbd_read (see kernel/include/keyboard.h). */
enum {
    FM_KEY_LEFT = 0x100,
    FM_KEY_RIGHT,
    FM_KEY_UP,
    FM_KEY_DOWN,
    FM_KEY_HOME,
    FM_KEY_END,
    FM_KEY_DELETE
};

/* Result bits of the event functions. */
#define FM_REDRAW (1u << 0)  /* repaint the window */
#define FM_ACTION (1u << 1)  /* the user opened a file: see fm_take_action() */

typedef enum FmKind {
    FM_KIND_FOLDER = 0,
    FM_KIND_FILE,
    FM_KIND_TEXT,
    FM_KIND_EXECUTABLE,
    FM_KIND_ARCHIVE,
    FM_KIND_IMAGE
} FmKind;

typedef enum FmView {
    FM_VIEW_ICONS = 0,
    FM_VIEW_LIST
} FmView;

typedef enum FmAction {
    FM_ACTION_NONE = 0,
    FM_ACTION_OPEN_TEXT,
    FM_ACTION_RUN
} FmAction;

typedef struct FmEntry {
    char name[FM_NAME_CAP];
    FmKind kind;
    unsigned long size;
} FmEntry;

typedef struct FileManager {
    const SettingsPalette *palette;    /* colours, owned by the caller */
    int top;                           /* height of the caller's title bar */

    char path[FM_PATH_CAP];            /* absolute, no trailing slash except "/" */
    FmEntry *entries;                  /* folders first, then by name */
    int count;
    int load_failed;
    unsigned long free_bytes;
    int free_known;

    int view;                          /* FmView */
    int selected;                      /* index into entries, or -1 */
    int scroll;                        /* pixels scrolled down */
    int hover;                         /* hit id under the pointer */

    char back[FM_HISTORY][FM_PATH_CAP];
    int back_count;
    char forward[FM_HISTORY][FM_PATH_CAP];
    int forward_count;

    int editing;                       /* inline name entry for entries[selected] */
    int edit_created;                  /* the entry is a folder made for this entry */
    int edit_replaces_all;             /* the next character replaces the whole name */
    char edit_text[FM_EDIT_CAP];
    int edit_len;

    int confirm_delete;                /* the delete dialog is open */

    char note[FM_NOTE_CAP];            /* last problem, shown in the status bar */

    int last_click_item;
    unsigned long last_click_ms;

    int action;                        /* pending FmAction */
    char action_path[FM_PATH_CAP];
} FileManager;

/* Opens the folder at path (the root if that fails). Returns 0, or -1 when
   memory is exhausted. */
int fm_init(FileManager *fm, const SettingsPalette *palette, int top, const char *path);
void fm_destroy(FileManager *fm);

/* Repaints everything below the title bar. */
void fm_draw(FileManager *fm, Window *window);

/* x and y are window coordinates; all return FM_* bits. A negative x or y
   in fm_hover() means the pointer is elsewhere. */
unsigned int fm_click(FileManager *fm, const Window *window, int x, int y);
unsigned int fm_hover(FileManager *fm, const Window *window, int x, int y);
unsigned int fm_key(FileManager *fm, const Window *window, int key);
/* delta > 0 scrolls up. */
unsigned int fm_wheel(FileManager *fm, const Window *window, int delta);

/* Shows text as a problem in the status bar until the next action. */
void fm_report(FileManager *fm, const char *text);

/* Returns the pending FM_ACTION_* (and clears it); path receives the file. */
int fm_take_action(FileManager *fm, char *path, size_t capacity);
