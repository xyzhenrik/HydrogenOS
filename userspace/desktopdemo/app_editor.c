#include "app_editor.h"
#include "clipboard.h"
#include "errno.h"
#include "gfx_effects.h"
#include "kbd_keys.h"
#include "mini_libc.h"
#include "syscall.h"
#include "textedit.h"

#include <stdarg.h>
#include <stdint.h>

#define GLYPH_W      8
#define GLYPH_H      16
#define LINE_HEIGHT  18
#define MAX_COLS     512

/* Chrome metrics. Bars are one separator pixel plus their body. */
#define STATUS_H        25
#define PROMPT_H        37
#define FIELD_H         26
#define SCROLLBAR_W     14
#define THUMB_W         6
#define THUMB_MARGIN    4
#define THUMB_MIN_H     32
#define TEXT_PAD_TOP    6
#define TEXT_PAD_LEFT   12
#define GUTTER_PAD_LEFT 14
#define GUTTER_PAD_RIGHT 12
#define GUTTER_MIN_DIGITS 3
#define BAR_PAD         12
#define MARGIN_COLUMNS  4
#define DOUBLE_CLICK_MS 500
#define DOUBLE_CLICK_SLOP 4

#define PATH_CAPACITY    256
#define MESSAGE_CAPACITY 128
#define PROMPT_CAPACITY  256
#define FIND_PREFILL_MAX 64

typedef enum { PROMPT_NONE, PROMPT_SAVE_AS, PROMPT_OPEN, PROMPT_FIND } PromptKind;
typedef enum { DRAG_NONE, DRAG_SELECT, DRAG_THUMB } DragKind;
typedef enum { PENDING_NONE, PENDING_NEW, PENDING_OPEN, PENDING_CLOSE } Pending;

/* Colour tokens derived from the session palette; all 0x00RRGGBB. */
typedef struct Colors {
    uint32_t bg, text, dim, selection, accent, danger;
    uint32_t gutter, separator, line, thumb, thumb_active, field;
} Colors;

typedef struct Layout {
    int width, height;
    int area_y;       /* first row below the title bar */
    int rows_y;       /* top of the first text row */
    int area_bottom;  /* text area ends here; bars follow */
    int prompt_y;     /* -1 while no prompt bar is shown */
    int status_y;
    int gutter_w;     /* 0 without line numbers */
    int text_x;
    int track_x;      /* scrollbar column */
    int rows, cols;
} Layout;

typedef struct Snapshot {
    size_t caret, anchor, lines;
} Snapshot;

struct Editor {
    TextEdit *edit;
    char path[PATH_CAPACITY];
    char message[MESSAGE_CAPACITY];
    int message_error;

    size_t top_line;
    size_t left_column;
    int line_numbers;
    Pending pending;

    PromptKind prompt;
    char input[PROMPT_CAPACITY];
    size_t input_length;
    size_t input_caret;
    char last_find[PROMPT_CAPACITY];
    size_t find_origin;
    int find_failed;
    int prompt_dirty;

    DragKind drag;
    int thumb_grab;
    unsigned long last_click_ms;
    int last_click_x, last_click_y;
    int click_count;

    int view_w, view_h, view_y;

    /* What is on the surface right now, to repaint only the difference. */
    int painted_valid;
    int need_full;
    int need_text;
    int dirty_any, dirty_to_end;
    size_t dirty_first, dirty_last;
    Layout painted_layout;
    Colors painted_colors;
    size_t painted_top, painted_left;
    int painted_thumb_y, painted_thumb_h, painted_thumb_active;
    char painted_status_left[MESSAGE_CAPACITY + PATH_CAPACITY];
    char painted_status_right[96];
    int painted_status_error;
    char painted_title[64];
    int painted_modified;
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static size_t min_size(size_t a, size_t b) { return a < b ? a : b; }
static int clamp_int(int v, int low, int high) { return v < low ? low : (v > high ? high : v); }

static uint32_t mix(uint32_t bg, uint32_t fg, unsigned int alpha) {
    return gfx_mix(bg, fg, alpha) & 0x00FFFFFFu;
}

static const char *reason(int error) {
    switch (error) {
    case ENOENT:  return "no such file or directory";
    case ENOTDIR: return "not a directory";
    case EISDIR:  return "is a directory";
    case ENOSPC:  return "no space left on device";
    case EROFS:   return "read-only file system";
    case EACCES:
    case EPERM:   return "permission denied";
    case ENOMEM:  return "out of memory";
    case E2BIG:   return "too large";
    case EIO:     return "I/O error";
    default:      return "unexpected error";
    }
}

static void set_message(Editor *ed, int error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(ed->message, sizeof(ed->message), format, args);
    va_end(args);
    ed->message_error = error;
}

static void clear_message(Editor *ed) {
    ed->message[0] = 0;
    ed->message_error = 0;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* "1.2 KB" style size for the status bar. */
static void format_size(char *out, size_t capacity, size_t bytes) {
    if (bytes < 1024)
        snprintf(out, capacity, "%lu B", (unsigned long)bytes);
    else if (bytes < 1024 * 1024)
        snprintf(out, capacity, "%lu.%lu KB", (unsigned long)(bytes / 1024),
                 (unsigned long)(bytes % 1024 * 10 / 1024));
    else
        snprintf(out, capacity, "%lu.%02lu MB", (unsigned long)(bytes >> 20),
                 (unsigned long)((bytes & 0xFFFFF) * 100 >> 20));
}

static int digit_count(size_t value) {
    int digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

Editor *editor_create(void) {
    Editor *ed = calloc(1, sizeof(*ed));
    if (!ed) return 0;
    ed->edit = text_edit_create();
    if (!ed->edit) {
        free(ed);
        return 0;
    }
    ed->line_numbers = 1;
    ed->need_full = 1;
    return ed;
}

void editor_destroy(Editor *ed) {
    if (!ed) return;
    text_edit_destroy(ed->edit);
    free(ed);
}

void editor_invalidate(Editor *ed) {
    ed->need_full = 1;
}

int editor_modified(const Editor *ed) {
    return text_buffer_modified(ed->edit->buffer);
}

void editor_title(const Editor *ed, char *out, size_t size) {
    if (size == 0) return;
    const char *name = ed->path[0] ? base_name(ed->path) : "Untitled";
    size_t length = strlen(name);
    if (length >= size) length = size > 4 ? size - 4 : 0;
    memcpy(out, name, length);
    if (length < strlen(name) && size > 3) {
        memcpy(out + length, "...", 3);
        length += 3;
    }
    out[length < size ? length : size - 1] = 0;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

static Layout compute_layout(const Editor *ed) {
    Layout lay;
    lay.width = ed->view_w;
    lay.height = ed->view_h;
    lay.area_y = ed->view_y;
    lay.status_y = ed->view_h - STATUS_H;
    lay.prompt_y = ed->prompt != PROMPT_NONE ? lay.status_y - PROMPT_H : -1;
    lay.area_bottom = lay.prompt_y >= 0 ? lay.prompt_y : lay.status_y;
    lay.rows_y = lay.area_y + TEXT_PAD_TOP;
    lay.rows = (lay.area_bottom - lay.rows_y) / LINE_HEIGHT;
    if (lay.rows < 1) lay.rows = 1;

    size_t lines = text_buffer_line_count(ed->edit->buffer);
    int digits = digit_count(lines);
    if (digits < GUTTER_MIN_DIGITS) digits = GUTTER_MIN_DIGITS;
    lay.gutter_w = ed->line_numbers ? GUTTER_PAD_LEFT + digits * GLYPH_W + GUTTER_PAD_RIGHT : 0;
    lay.text_x = lay.gutter_w + TEXT_PAD_LEFT;
    lay.track_x = lay.width - SCROLLBAR_W;
    lay.cols = clamp_int((lay.track_x - lay.text_x) / GLYPH_W, 1, MAX_COLS);
    return lay;
}

static int same_layout(const Layout *a, const Layout *b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

static size_t max_top_line(const Editor *ed, const Layout *lay) {
    size_t lines = text_buffer_line_count(ed->edit->buffer);
    return lines > (size_t)lay->rows ? lines - (size_t)lay->rows : 0;
}

/* Where the scrollbar thumb sits; height 0 when everything is visible. */
static void thumb_geometry(const Editor *ed, const Layout *lay, int *y, int *h) {
    size_t lines = text_buffer_line_count(ed->edit->buffer);
    int track = lay->area_bottom - lay->area_y - 2 * THUMB_MARGIN;
    *y = 0;
    *h = 0;
    if (lines <= (size_t)lay->rows || track <= 0) return;

    int height = (int)((size_t)track * (size_t)lay->rows / lines);
    height = clamp_int(height, THUMB_MIN_H, track);
    size_t max_top = lines - (size_t)lay->rows;
    *h = height;
    *y = lay->area_y + THUMB_MARGIN + (int)((size_t)(track - height) * ed->top_line / max_top);
}

static void clamp_view(Editor *ed, const Layout *lay) {
    size_t max_top = max_top_line(ed, lay);
    if (ed->top_line > max_top) ed->top_line = max_top;
}

/* Scrolls the least that brings the caret into view. */
static void reveal_caret(Editor *ed) {
    Layout lay = compute_layout(ed);
    TextEdit *e = ed->edit;
    size_t line = text_buffer_line_of(e->buffer, e->caret);
    if (line < ed->top_line) ed->top_line = line;
    else if (line >= ed->top_line + (size_t)lay.rows) ed->top_line = line - (size_t)lay.rows + 1;
    clamp_view(ed, &lay);

    size_t column = text_edit_column_of(e, e->caret);
    size_t margin = min_size(MARGIN_COLUMNS, (size_t)lay.cols / 4);
    if (column < ed->left_column)
        ed->left_column = column > margin ? column - margin : 0;
    else if (column + margin >= ed->left_column + (size_t)lay.cols)
        ed->left_column = column + margin + 1 - (size_t)lay.cols;
}

/* ------------------------------------------------------------------ */
/* Dirty tracking                                                      */
/* ------------------------------------------------------------------ */

static void touch_lines(Editor *ed, size_t first, size_t last) {
    if (!ed->dirty_any) {
        ed->dirty_any = 1;
        ed->dirty_first = first;
        ed->dirty_last = last;
        return;
    }
    if (first < ed->dirty_first) ed->dirty_first = first;
    if (last > ed->dirty_last) ed->dirty_last = last;
}

static Snapshot snapshot(const Editor *ed) {
    Snapshot s = { ed->edit->caret, ed->edit->anchor, text_buffer_line_count(ed->edit->buffer) };
    return s;
}

/* Marks the lines a command touched: those of the old and new caret and
   selection, and everything below a point where the line count changed. */
static void settle(Editor *ed, const Snapshot *before, int follow, int everything) {
    TextBuffer *buffer = ed->edit->buffer;
    size_t caret = ed->edit->caret, anchor = ed->edit->anchor;
    size_t lines = text_buffer_line_count(buffer);

    if (everything) {
        ed->need_text = 1;
    } else {
        size_t old_caret = text_buffer_line_of(buffer, min_size(before->caret, text_buffer_length(buffer)));
        size_t old_anchor = text_buffer_line_of(buffer, min_size(before->anchor, text_buffer_length(buffer)));
        size_t new_caret = text_buffer_line_of(buffer, caret);
        size_t new_anchor = text_buffer_line_of(buffer, anchor);
        int selecting = before->caret != before->anchor || caret != anchor;

        size_t first = min_size(old_caret, new_caret);
        size_t last = old_caret > new_caret ? old_caret : new_caret;
        if (selecting) {
            first = min_size(first, min_size(old_anchor, new_anchor));
            if (old_anchor > last) last = old_anchor;
            if (new_anchor > last) last = new_anchor;
        }
        if (lines != before->lines) {
            ed->dirty_to_end = 1;
            touch_lines(ed, first, first);
        } else {
            touch_lines(ed, first, last);
        }
    }
    if (follow) reveal_caret(ed);
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

/* Makes path absolute against the working directory and folds repeated
   slashes, which prompts easily produce ("/" followed by "/name"). */
static int resolve_path(const char *path, char *out, size_t capacity) {
    while (*path == ' ') ++path;
    if (!*path) return -EINVAL;
    char joined[PATH_CAPACITY];
    int written;
    if (path[0] == '/') {
        written = snprintf(joined, sizeof(joined), "%s", path);
    } else {
        char cwd[PATH_CAPACITY];
        if (!getcwd(cwd, sizeof(cwd))) return -EIO;
        written = snprintf(joined, sizeof(joined), "%s/%s", cwd, path);
    }
    if (written < 0 || (size_t)written >= sizeof(joined)) return -E2BIG;

    size_t length = 0;
    for (const char *p = joined; *p; ++p) {
        if (*p == '/' && length > 0 && out[length - 1] == '/') continue;
        if (length + 1 >= capacity) return -E2BIG;
        out[length++] = *p;
    }
    out[length] = 0;
    return 0;
}

/* Replaces the document with whatever path holds. The old document stays
   when anything goes wrong. */
static int load_file(Editor *ed, const char *path) {
    stat_t st;
    long rc = sys_stat(path, &st);
    char *data = 0;
    size_t length = 0;

    if (rc == 0) {
        if (S_ISDIR(st.st_mode)) rc = -EISDIR;
        else if (st.st_size > TEXT_BUFFER_MAX_BYTES) rc = -E2BIG;
    }
    if (rc == 0) {
        long fd = open(path, 0);
        if (fd < 0) {
            rc = fd;
        } else {
            data = malloc((size_t)st.st_size + 1);
            if (!data) rc = -ENOMEM;
            while (rc == 0 && length < (size_t)st.st_size) {
                long n = read((int)fd, data + length, (size_t)st.st_size - length);
                if (n < 0) rc = n;
                else if (n == 0) break;
                else length += (size_t)n;
            }
            close((int)fd);
        }
    }
    int created = rc == -ENOENT;
    if (created) rc = 0;
    if (rc == 0) rc = text_edit_load(ed->edit, data, length);
    free(data);

    if (rc != 0) {
        set_message(ed, 1, "Could not open %s: %s", base_name(path), reason((int)-rc));
        return (int)rc;
    }
    snprintf(ed->path, sizeof(ed->path), "%s", path);
    ed->top_line = 0;
    ed->left_column = 0;
    ed->need_full = 1;
    if (created) set_message(ed, 0, "New file");
    else clear_message(ed);
    return 0;
}

int editor_open(Editor *ed, const char *path) {
    char absolute[PATH_CAPACITY];
    int rc = resolve_path(path, absolute, sizeof(absolute));
    if (rc != 0) {
        set_message(ed, 1, "Could not open: invalid path");
        return rc;
    }
    return load_file(ed, absolute);
}

/* Writes the document to path; on success the document is bound to it. */
static int save_file(Editor *ed, const char *path) {
    size_t length;
    const char *data = text_buffer_contiguous(ed->edit->buffer, &length);
    long rc = sys_writef(path, data, length);
    if (rc == 0) {
        stat_t st;
        if (sys_stat(path, &st) != 0 || st.st_size != length) rc = -EIO;
    }
    if (rc != 0) {
        set_message(ed, 1, "Could not save %s: %s", base_name(path), reason((int)-rc));
        return (int)rc;
    }
    snprintf(ed->path, sizeof(ed->path), "%s", path);
    text_buffer_mark_clean(ed->edit->buffer);
    char size[24];
    format_size(size, sizeof(size), length);
    set_message(ed, 0, "Saved %s (%s)", base_name(path), size);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Prompt bar                                                          */
/* ------------------------------------------------------------------ */

static void open_prompt(Editor *ed, PromptKind kind, const char *initial) {
    ed->prompt = kind;
    snprintf(ed->input, sizeof(ed->input), "%s", initial);
    ed->input_length = strlen(ed->input);
    ed->input_caret = ed->input_length;
    ed->find_failed = 0;
    ed->prompt_dirty = 1;
    ed->need_full = 1;
}

static void close_prompt(Editor *ed) {
    ed->prompt = PROMPT_NONE;
    ed->need_full = 1;
}

static void prompt_for_save_as(Editor *ed) {
    char initial[PATH_CAPACITY];
    if (ed->path[0]) {
        snprintf(initial, sizeof(initial), "%s", ed->path);
    } else {
        char cwd[PATH_CAPACITY];
        if (!getcwd(cwd, sizeof(cwd))) cwd[0] = 0;
        snprintf(initial, sizeof(initial), "%s%s", cwd, strcmp(cwd, "/") == 0 ? "" : "/");
    }
    open_prompt(ed, PROMPT_SAVE_AS, initial);
}

static void select_match(Editor *ed, size_t position, size_t length) {
    ed->edit->anchor = position;
    ed->edit->caret = position + length;
    ed->need_text = 1;
    reveal_caret(ed);
}

static void find_from(Editor *ed, size_t from, int forward) {
    if (ed->input_length == 0) {
        ed->find_failed = 0;
        return;
    }
    long found = text_edit_find(ed->edit, ed->input, ed->input_length, from, forward);
    ed->find_failed = found < 0;
    if (found >= 0) select_match(ed, (size_t)found, ed->input_length);
}

static void open_find(Editor *ed) {
    size_t start, end;
    text_edit_selection(ed->edit, &start, &end);
    char initial[PROMPT_CAPACITY];
    snprintf(initial, sizeof(initial), "%s", ed->last_find);
    if (end > start && end - start <= FIND_PREFILL_MAX) {
        size_t n = text_buffer_read(ed->edit->buffer, start, initial, end - start);
        initial[n] = 0;
        if (memchr(initial, '\n', n)) snprintf(initial, sizeof(initial), "%s", ed->last_find);
    }
    open_prompt(ed, PROMPT_FIND, initial);
    ed->find_origin = start;
    find_from(ed, ed->find_origin, 1);
    reveal_caret(ed);
}

static void accept_prompt(Editor *ed) {
    char absolute[PATH_CAPACITY];
    if (ed->prompt == PROMPT_FIND) {
        size_t start, end;
        text_edit_selection(ed->edit, &start, &end);
        find_from(ed, end, 1);
        text_edit_selection(ed->edit, &start, &end);
        ed->find_origin = start;
        return;
    }
    int rc = resolve_path(ed->input, absolute, sizeof(absolute));
    if (rc != 0) {
        set_message(ed, 1, "Enter a file name");
        return;
    }
    rc = ed->prompt == PROMPT_SAVE_AS ? save_file(ed, absolute) : load_file(ed, absolute);
    if (rc == 0) {
        close_prompt(ed);
        ed->need_text = 1;
    }
}

static void input_insert(Editor *ed, const char *data, size_t length) {
    size_t room = sizeof(ed->input) - 1 - ed->input_length;
    if (length > room) length = room;
    memmove(ed->input + ed->input_caret + length, ed->input + ed->input_caret,
            ed->input_length - ed->input_caret);
    memcpy(ed->input + ed->input_caret, data, length);
    ed->input_length += length;
    ed->input_caret += length;
    ed->input[ed->input_length] = 0;
}

static void input_erase(Editor *ed, size_t position) {
    memmove(ed->input + position, ed->input + position + 1, ed->input_length - position);
    --ed->input_length;
}

static void prompt_key(Editor *ed, int key) {
    int code = key & KBD_CODE_MASK;
    int edited = 0;
    clear_message(ed);

    switch (code) {
    case KBD_KEY_ESCAPE:
        if (ed->prompt == PROMPT_FIND) snprintf(ed->last_find, sizeof(ed->last_find), "%s", ed->input);
        close_prompt(ed);
        return;
    case '\n':
    case '\r':
        accept_prompt(ed);
        break;
    case KBD_KEY_LEFT:
        if (ed->input_caret > 0) --ed->input_caret;
        break;
    case KBD_KEY_RIGHT:
        if (ed->input_caret < ed->input_length) ++ed->input_caret;
        break;
    case KBD_KEY_HOME:
        ed->input_caret = 0;
        break;
    case KBD_KEY_END:
        ed->input_caret = ed->input_length;
        break;
    case KBD_KEY_UP:
    case KBD_KEY_DOWN:
        if (ed->prompt == PROMPT_FIND) {
            size_t start, end;
            text_edit_selection(ed->edit, &start, &end);
            int forward = code == KBD_KEY_DOWN;
            find_from(ed, forward ? end : start, forward);
            text_edit_selection(ed->edit, &start, &end);
            ed->find_origin = start;
        }
        break;
    case '\b':
    case 0x7F:
        if (ed->input_caret > 0) {
            input_erase(ed, --ed->input_caret);
            edited = 1;
        }
        break;
    case KBD_KEY_DELETE:
        if (ed->input_caret < ed->input_length) {
            input_erase(ed, ed->input_caret);
            edited = 1;
        }
        break;
    case 0x16: {
        size_t length;
        const char *data = clipboard_data(&length);
        const char *newline = memchr(data, '\n', length);
        input_insert(ed, data, newline ? (size_t)(newline - data) : length);
        edited = 1;
        break;
    }
    default:
        if (key >= 0x20 && key < 0x7F) {
            char c = (char)key;
            input_insert(ed, &c, 1);
            edited = 1;
        }
        break;
    }

    if (edited && ed->prompt == PROMPT_FIND) find_from(ed, ed->find_origin, 1);
    ed->prompt_dirty = 1;
    reveal_caret(ed);
}

/* ------------------------------------------------------------------ */
/* Keyboard                                                            */
/* ------------------------------------------------------------------ */

static void report(Editor *ed, int rc) {
    if (rc < 0) set_message(ed, 1, "Could not complete: %s", reason(-rc));
}

/* True when the command may go ahead; otherwise asks for confirmation. */
static int confirm_discard(Editor *ed, Pending armed, Pending command, const char *keys) {
    if (!editor_modified(ed) || armed == command) return 1;
    ed->pending = command;
    set_message(ed, 1, "Unsaved changes. Press %s again to discard them.", keys);
    return 0;
}

static void page_move(Editor *ed, int up, int extend) {
    Layout lay = compute_layout(ed);
    TextEdit *e = ed->edit;
    long before = (long)text_buffer_line_of(e->buffer, e->caret);
    text_edit_move(e, up ? TEXT_MOVE_PAGE_UP : TEXT_MOVE_PAGE_DOWN, extend,
                   lay.rows > 1 ? (size_t)lay.rows - 1 : 1);
    long moved = (long)text_buffer_line_of(e->buffer, e->caret) - before;
    long top = (long)ed->top_line + moved;
    ed->top_line = top < 0 ? 0 : (size_t)top;
    clamp_view(ed, &lay);
}

static void navigate(Editor *ed, int key) {
    TextEdit *e = ed->edit;
    int shift = (key & KBD_MOD_SHIFT) != 0;
    int ctrl = (key & KBD_MOD_CTRL) != 0;

    switch (key & KBD_CODE_MASK) {
    case KBD_KEY_LEFT:
        text_edit_move(e, ctrl ? TEXT_MOVE_WORD_LEFT : TEXT_MOVE_LEFT, shift, 0);
        break;
    case KBD_KEY_RIGHT:
        text_edit_move(e, ctrl ? TEXT_MOVE_WORD_RIGHT : TEXT_MOVE_RIGHT, shift, 0);
        break;
    case KBD_KEY_UP:
        text_edit_move(e, TEXT_MOVE_UP, shift, 0);
        break;
    case KBD_KEY_DOWN:
        text_edit_move(e, TEXT_MOVE_DOWN, shift, 0);
        break;
    case KBD_KEY_HOME:
        text_edit_move(e, ctrl ? TEXT_MOVE_TEXT_START : TEXT_MOVE_LINE_START, shift, 0);
        break;
    case KBD_KEY_END:
        text_edit_move(e, ctrl ? TEXT_MOVE_TEXT_END : TEXT_MOVE_LINE_END, shift, 0);
        break;
    case KBD_KEY_PAGE_UP:
        page_move(ed, 1, shift);
        break;
    case KBD_KEY_PAGE_DOWN:
        page_move(ed, 0, shift);
        break;
    }
}

void editor_key(Editor *ed, int key) {
    if (key < 0) return;
    Pending armed = ed->pending;
    ed->pending = PENDING_NONE;
    if (ed->prompt != PROMPT_NONE) {
        prompt_key(ed, key);
        return;
    }

    TextEdit *e = ed->edit;
    Snapshot before = snapshot(ed);
    int everything = 0;
    int code = key & KBD_CODE_MASK;
    clear_message(ed);

    if (code >= 0x100) {
        switch (code) {
        case KBD_KEY_DELETE:
            report(ed, text_edit_delete(e));
            break;
        case KBD_KEY_INSERT:
            e->overwrite = !e->overwrite;
            break;
        case KBD_KEY_ESCAPE:
            text_edit_set_caret(e, e->caret, 0);
            break;
        default:
            navigate(ed, key);
            break;
        }
    } else {
        switch (key) {
        case '\n':
        case '\r':
            report(ed, text_edit_newline(e));
            break;
        case '\b':
        case 0x7F:
            report(ed, text_edit_backspace(e));
            break;
        case '\t':
            report(ed, text_edit_tab(e));
            break;
        case 0x01:
            text_edit_select_all(e);
            break;
        case 0x03:
            report(ed, text_edit_copy(e));
            if (text_edit_has_selection(e)) set_message(ed, 0, "Copied");
            break;
        case 0x18:
            report(ed, text_edit_cut(e));
            break;
        case 0x16:
            report(ed, text_edit_paste(e));
            everything = 1;
            break;
        case 0x1A:
            report(ed, text_edit_undo(e));
            everything = 1;
            break;
        case 0x19:
            report(ed, text_edit_redo(e));
            everything = 1;
            break;
        case 0x13:
            if (ed->path[0]) save_file(ed, ed->path);
            else prompt_for_save_as(ed);
            break;
        case 0x12:
            prompt_for_save_as(ed);
            break;
        case 0x0F:
            if (confirm_discard(ed, armed, PENDING_OPEN, "Ctrl+O")) open_prompt(ed, PROMPT_OPEN, "");
            break;
        case 0x0E:
            if (confirm_discard(ed, armed, PENDING_NEW, "Ctrl+N")) {
                text_edit_load(e, "", 0);
                ed->path[0] = 0;
                ed->top_line = 0;
                ed->left_column = 0;
                ed->need_full = 1;
                everything = 1;
            }
            break;
        case 0x06:
            open_find(ed);
            return;
        case 0x0C:
            ed->line_numbers = !ed->line_numbers;
            ed->need_full = 1;
            break;
        default:
            if (key >= 0x20 && key < 0x7F) {
                char c = (char)key;
                report(ed, text_edit_insert(e, &c, 1));
            } else {
                ed->pending = armed;
                return;
            }
            break;
        }
    }
    settle(ed, &before, 1, everything);
}

int editor_close_allowed(Editor *ed) {
    if (!editor_modified(ed) || ed->pending == PENDING_CLOSE) return 1;
    ed->pending = PENDING_CLOSE;
    set_message(ed, 1, "Unsaved changes. Click close again to discard them.");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Mouse                                                               */
/* ------------------------------------------------------------------ */

static size_t position_at_point(Editor *ed, const Layout *lay, int x, int y) {
    TextBuffer *buffer = ed->edit->buffer;
    int row = y < lay->rows_y ? 0 : (y - lay->rows_y) / LINE_HEIGHT;
    size_t line = ed->top_line + (size_t)row;
    size_t last = text_buffer_line_count(buffer) - 1;
    if (line > last) line = last;
    int offset = x - lay->text_x + GLYPH_W / 2;
    size_t column = ed->left_column + (offset > 0 ? (size_t)(offset / GLYPH_W) : 0);
    return text_edit_position_at(ed->edit, line, column);
}

static void scroll_to(Editor *ed, size_t top) {
    Layout lay = compute_layout(ed);
    size_t max_top = max_top_line(ed, &lay);
    ed->top_line = top > max_top ? max_top : top;
}

void editor_scroll(Editor *ed, int lines) {
    long top = (long)ed->top_line + lines;
    scroll_to(ed, top < 0 ? 0 : (size_t)top);
}

void editor_mouse_press(Editor *ed, int x, int y, unsigned long now_ms) {
    ed->pending = PENDING_NONE;
    Layout lay = compute_layout(ed);
    TextEdit *e = ed->edit;
    Snapshot before = snapshot(ed);

    if (y < lay.area_y || y >= lay.area_bottom) return;

    if (x >= lay.track_x) {
        int thumb_y, thumb_h;
        thumb_geometry(ed, &lay, &thumb_y, &thumb_h);
        if (thumb_h == 0) return;
        if (y >= thumb_y && y < thumb_y + thumb_h) {
            ed->drag = DRAG_THUMB;
            ed->thumb_grab = y - thumb_y;
        } else {
            size_t page = lay.rows > 1 ? (size_t)lay.rows - 1 : 1;
            if (y < thumb_y) scroll_to(ed, ed->top_line > page ? ed->top_line - page : 0);
            else scroll_to(ed, ed->top_line + page);
        }
        return;
    }

    size_t position = position_at_point(ed, &lay, x, y);
    if (x < lay.gutter_w) {
        text_edit_select_line(e, position);
        ed->click_count = 0;
        settle(ed, &before, 0, 0);
        return;
    }

    int near = now_ms - ed->last_click_ms <= DOUBLE_CLICK_MS &&
               x - ed->last_click_x <= DOUBLE_CLICK_SLOP && ed->last_click_x - x <= DOUBLE_CLICK_SLOP &&
               y - ed->last_click_y <= DOUBLE_CLICK_SLOP && ed->last_click_y - y <= DOUBLE_CLICK_SLOP;
    ed->click_count = near && ed->click_count > 0 ? ed->click_count % 3 + 1 : 1;
    ed->last_click_ms = now_ms;
    ed->last_click_x = x;
    ed->last_click_y = y;

    if (ed->click_count == 1) {
        text_edit_set_caret(e, position, 0);
        ed->drag = DRAG_SELECT;
    } else if (ed->click_count == 2) {
        text_edit_select_word(e, position);
    } else {
        text_edit_select_line(e, position);
    }
    settle(ed, &before, 0, 0);
}

void editor_mouse_drag(Editor *ed, int x, int y) {
    Layout lay = compute_layout(ed);
    if (ed->drag == DRAG_THUMB) {
        int thumb_y, thumb_h;
        thumb_geometry(ed, &lay, &thumb_y, &thumb_h);
        int track = lay.area_bottom - lay.area_y - 2 * THUMB_MARGIN - thumb_h;
        if (thumb_h == 0 || track <= 0) return;
        int offset = clamp_int(y - ed->thumb_grab - lay.area_y - THUMB_MARGIN, 0, track);
        scroll_to(ed, (size_t)offset * max_top_line(ed, &lay) / (size_t)track);
        return;
    }
    if (ed->drag != DRAG_SELECT) return;

    Snapshot before = snapshot(ed);
    int row = (y - lay.rows_y) / LINE_HEIGHT;
    if (y < lay.rows_y) {
        if (ed->top_line > 0) --ed->top_line;
    } else if (row >= lay.rows) {
        scroll_to(ed, ed->top_line + 1);
    }
    if (x < lay.text_x) {
        if (ed->left_column > 0) --ed->left_column;
    } else if (x >= lay.track_x) {
        ++ed->left_column;
    }
    int clamped_y = clamp_int(y, lay.rows_y, lay.rows_y + lay.rows * LINE_HEIGHT - 1);
    text_edit_set_caret(ed->edit, position_at_point(ed, &lay, x, clamped_y), 1);
    settle(ed, &before, 0, 0);
}

void editor_mouse_release(Editor *ed) {
    ed->drag = DRAG_NONE;
}

/* ------------------------------------------------------------------ */
/* Painting                                                            */
/* ------------------------------------------------------------------ */

static Colors make_colors(const SettingsPalette *pal) {
    Colors c;
    c.bg = pal->document_bg;
    c.text = pal->document_text;
    c.selection = pal->document_selection;
    c.accent = pal->accent;
    c.danger = pal->danger;
    c.dim = mix(c.bg, c.text, 120);
    c.gutter = mix(c.bg, c.text, 10);
    c.separator = mix(c.bg, c.text, 34);
    c.line = mix(c.bg, c.text, 12);
    c.thumb = mix(c.bg, c.text, 80);
    c.thumb_active = mix(c.bg, c.text, 140);
    c.field = mix(c.bg, 0x00FFFFFFu, 200);
    return c;
}

static void fill_rect(Window *win, int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > win->w) w = win->w - x;
    if (y + h > win->h) h = win->h - y;
    if (w <= 0 || h <= 0) return;
    uint32_t *row = (uint32_t *)win->surface + (size_t)y * (size_t)win->w + (size_t)x;
    for (int yy = 0; yy < h; ++yy, row += win->w)
        for (int xx = 0; xx < w; ++xx) row[xx] = color;
}

static void damage(Window *win, int x, int y, int w, int h) {
    if (w > 0 && h > 0) comp_damage_rect(win, win->x + x, win->y + y, w, h);
}

/* Gutter and background of a horizontal band, as an empty text row looks. */
static void paint_band(Window *win, const Layout *lay, const Colors *c, int y, int h, uint32_t bg) {
    if (lay->gutter_w > 0) {
        fill_rect(win, 0, y, lay->gutter_w - 1, h, c->gutter);
        fill_rect(win, lay->gutter_w - 1, y, 1, h, c->separator);
    }
    fill_rect(win, lay->gutter_w, y, lay->track_x - lay->gutter_w, h, bg);
}

static char printable(int byte) {
    if (byte < 0x20 || byte == 0x7F) return '.';
    if (byte >= 0x80) return '?';
    return (char)byte;
}

static void paint_line(const Editor *ed, Window *win, const Layout *lay, const Colors *c,
                       int row, size_t line) {
    static char cells[MAX_COLS];
    static unsigned char selected[MAX_COLS];
    TextBuffer *buffer = ed->edit->buffer;
    TextEdit *e = ed->edit;
    int y = lay->rows_y + row * LINE_HEIGHT;
    int has_line = line < text_buffer_line_count(buffer);
    size_t caret_line = text_buffer_line_of(buffer, e->caret);
    int on_caret_line = has_line && line == caret_line;
    int selecting = text_edit_has_selection(e);

    paint_band(win, lay, c, y, LINE_HEIGHT,
               on_caret_line && !selecting ? c->line : c->bg);
    if (!has_line) return;

    if (lay->gutter_w > 0) {
        char number[24];
        int n = snprintf(number, sizeof(number), "%lu", (unsigned long)line + 1);
        CompRect gutter = { 0, y, lay->gutter_w - 1, LINE_HEIGHT };
        comp_text_draw(win, lay->gutter_w - GUTTER_PAD_RIGHT - n * GLYPH_W, y + 1, number, (size_t)n,
                  on_caret_line ? c->text : c->dim, &gutter);
    }

    size_t sel_start, sel_end;
    text_edit_selection(e, &sel_start, &sel_end);
    size_t start = text_buffer_line_start(buffer, line);
    size_t end = text_buffer_line_end(buffer, line);
    size_t first = ed->left_column;
    size_t cols = (size_t)lay->cols;

    memset(cells, ' ', cols);
    memset(selected, 0, cols);
    size_t column = 0;
    char chunk[128];
    for (size_t position = start; position < end && column < first + cols;) {
        size_t n = text_buffer_read(buffer, position, chunk, min_size(sizeof(chunk), end - position));
        for (size_t i = 0; i < n && column < first + cols; ++i) {
            int byte = (unsigned char)chunk[i];
            size_t width = byte == '\t' ? TEXT_EDIT_TAB_WIDTH - column % TEXT_EDIT_TAB_WIDTH : 1;
            int in_selection = position + i >= sel_start && position + i < sel_end;
            for (size_t k = 0; k < width; ++k) {
                size_t at = column + k;
                if (at < first || at >= first + cols) continue;
                cells[at - first] = byte == '\t' ? ' ' : printable(byte);
                selected[at - first] = (unsigned char)in_selection;
            }
            column += width;
        }
        position += n;
    }
    if (end < text_buffer_length(buffer) && end >= sel_start && end < sel_end &&
        column >= first && column - first < cols)
        selected[column - first] = 1;

    for (size_t i = 0; i < cols;) {
        if (!selected[i]) { ++i; continue; }
        size_t run = i;
        while (i < cols && selected[i]) ++i;
        fill_rect(win, lay->text_x + (int)run * GLYPH_W, y, (int)(i - run) * GLYPH_W, LINE_HEIGHT,
                  c->selection);
    }

    size_t caret_column = on_caret_line ? text_edit_column_of(e, e->caret) : 0;
    int caret_visible = on_caret_line && caret_column >= first && caret_column - first <= cols;
    int caret_x = lay->text_x + (int)(caret_column - first) * GLYPH_W;
    if (caret_visible && e->overwrite && caret_x + GLYPH_W <= lay->track_x)
        fill_rect(win, caret_x, y, GLYPH_W, LINE_HEIGHT, c->selection);

    size_t low = 0, high = cols;
    while (low < high && cells[low] == ' ') ++low;
    while (high > low && cells[high - 1] == ' ') --high;
    CompRect clip = { lay->gutter_w, y, lay->track_x - lay->gutter_w, LINE_HEIGHT };
    if (high > low)
        comp_text_draw(win, lay->text_x + (int)low * GLYPH_W, y + 1, cells + low, high - low, c->text, &clip);

    if (caret_visible && !e->overwrite && caret_x < lay->track_x)
        fill_rect(win, caret_x - 1, y + 1, 2, LINE_HEIGHT - 2, c->text);
}

static void paint_scrollbar(Window *win, const Layout *lay, const Colors *c, int thumb_y,
                            int thumb_h, int active) {
    fill_rect(win, lay->track_x, lay->area_y, SCROLLBAR_W, lay->area_bottom - lay->area_y, c->bg);
    if (thumb_h <= 0) return;
    GfxCanvas canvas = { (uint32_t *)win->surface, win->w, win->h, win->w, 0 };
    gfx_fill_round_rect(&canvas, lay->track_x + (SCROLLBAR_W - THUMB_W) / 2, thumb_y, THUMB_W,
                        thumb_h, THUMB_W / 2, active ? c->thumb_active : c->thumb, 255);
}

static const char *prompt_label(PromptKind kind) {
    switch (kind) {
    case PROMPT_SAVE_AS: return "Save as";
    case PROMPT_OPEN:    return "Open";
    default:             return "Find";
    }
}

static void paint_prompt(const Editor *ed, Window *win, const Layout *lay, const Colors *c) {
    int y = lay->prompt_y;
    fill_rect(win, 0, y, lay->width, 1, c->separator);
    fill_rect(win, 0, y + 1, lay->width, PROMPT_H - 1, c->gutter);

    const char *label = prompt_label(ed->prompt);
    int label_w = (int)strlen(label) * GLYPH_W;
    int text_y = y + 1 + (PROMPT_H - 1 - GLYPH_H) / 2;
    comp_text_draw(win, BAR_PAD, text_y, label, strlen(label), c->text, 0);

    const char *hint = ed->prompt == PROMPT_FIND ? (ed->find_failed ? "No matches" : "Enter next  Esc close")
                                                  : "Enter confirm  Esc cancel";
    int hint_w = (int)strlen(hint) * GLYPH_W;
    int field_x = BAR_PAD + label_w + 10;
    int field_w = lay->width - field_x - BAR_PAD - hint_w - 12;
    if (field_w < 14 * GLYPH_W) {
        hint = "";
        hint_w = 0;
        field_w = lay->width - field_x - BAR_PAD;
    }
    if (field_w < 4 * GLYPH_W) return;
    if (hint_w > 0)
        comp_text_draw(win, lay->width - BAR_PAD - hint_w, text_y, hint, strlen(hint),
                  ed->find_failed ? c->danger : c->dim, 0);

    int field_y = y + 1 + (PROMPT_H - 1 - FIELD_H) / 2;
    GfxCanvas canvas = { (uint32_t *)win->surface, win->w, win->h, win->w, 0 };
    gfx_fill_round_rect(&canvas, field_x, field_y, field_w, FIELD_H, 6, c->separator, 255);
    gfx_fill_round_rect(&canvas, field_x + 1, field_y + 1, field_w - 2, FIELD_H - 2, 5, c->field, 255);

    size_t fit = (size_t)(field_w - 16) / GLYPH_W;
    size_t shown_from = ed->input_caret >= fit ? ed->input_caret - fit + 1 : 0;
    size_t shown = min_size(fit, ed->input_length - shown_from);
    CompRect clip = { field_x + 4, field_y, field_w - 8, FIELD_H };
    comp_text_draw(win, field_x + 8, field_y + (FIELD_H - GLYPH_H) / 2, ed->input + shown_from, shown,
              c->text, &clip);
    int caret_x = field_x + 8 + (int)(ed->input_caret - shown_from) * GLYPH_W;
    fill_rect(win, caret_x - 1, field_y + 5, 2, FIELD_H - 10, c->accent);
}

static void build_status(const Editor *ed, char *left, size_t left_capacity, char *right,
                         size_t right_capacity, int width) {
    TextEdit *e = ed->edit;
    TextBuffer *buffer = e->buffer;
    size_t line = text_buffer_line_of(buffer, e->caret);
    size_t column = text_edit_column_of(e, e->caret);
    size_t start, end;
    text_edit_selection(e, &start, &end);

    char position[48];
    if (end > start)
        snprintf(position, sizeof(position), "Ln %lu, Col %lu (%lu selected)", (unsigned long)line + 1,
                 (unsigned long)column + 1, (unsigned long)(end - start));
    else
        snprintf(position, sizeof(position), "Ln %lu, Col %lu", (unsigned long)line + 1,
                 (unsigned long)column + 1);
    char size[24];
    format_size(size, sizeof(size), text_buffer_length(buffer));
    const char *mode = e->overwrite ? "OVR" : "INS";

    snprintf(right, right_capacity, "%s   %s   %s", position, mode, size);
    if ((int)strlen(right) * GLYPH_W > width / 2)
        snprintf(right, right_capacity, "%s   %s", position, size);
    if ((int)strlen(right) * GLYPH_W > width / 2)
        snprintf(right, right_capacity, "%s", position);

    if (ed->message[0]) {
        snprintf(left, left_capacity, "%s", ed->message);
    } else if (ed->path[0]) {
        snprintf(left, left_capacity, "%s", ed->path);
    } else {
        snprintf(left, left_capacity, "Untitled");
    }
    /* Long paths lose their beginning: the file name is what matters. */
    int room = (width - 2 * BAR_PAD) / GLYPH_W - (int)strlen(right) - 3;
    size_t length = strlen(left);
    if (room < 8) room = 8;
    if (!ed->message[0] && length > (size_t)room) {
        memmove(left + 3, left + length - (size_t)room + 3, (size_t)room - 3 + 1);
        memcpy(left, "...", 3);
    } else if (length > (size_t)room) {
        left[room] = 0;
    }
}

static void paint_status(Window *win, const Layout *lay, const Colors *c, const char *left,
                         const char *right, int error) {
    int y = lay->status_y;
    fill_rect(win, 0, y, lay->width, 1, c->separator);
    fill_rect(win, 0, y + 1, lay->width, STATUS_H - 1, c->gutter);
    int text_y = y + 1 + (STATUS_H - 1 - GLYPH_H) / 2;
    comp_text_draw(win, BAR_PAD, text_y, left, strlen(left), error ? c->danger : c->dim, 0);
    int right_w = (int)strlen(right) * GLYPH_W;
    comp_text_draw(win, lay->width - BAR_PAD - right_w, text_y, right, strlen(right), c->dim, 0);
}

unsigned editor_draw(Editor *ed, Window *win, const SettingsPalette *palette, int top) {
    if (!win || !win->surface) return 0;
    Colors c = make_colors(palette);
    ed->view_w = win->w;
    ed->view_h = win->h;
    ed->view_y = top + 1;
    Layout lay = compute_layout(ed);
    clamp_view(ed, &lay);

    int chrome = ed->need_full || !ed->painted_valid || !same_layout(&lay, &ed->painted_layout) ||
                 memcmp(&c, &ed->painted_colors, sizeof(c)) != 0;
    int all_rows = chrome || ed->need_text || ed->top_line != ed->painted_top ||
                   ed->left_column != ed->painted_left;

    if (all_rows) {
        paint_band(win, &lay, &c, lay.area_y, TEXT_PAD_TOP, c.bg);
        int below = lay.rows_y + lay.rows * LINE_HEIGHT;
        paint_band(win, &lay, &c, below, lay.area_bottom - below, c.bg);
    }
    int run_start = -1;
    for (int row = 0; row <= lay.rows; ++row) {
        size_t line = ed->top_line + (size_t)row;
        int paint = row < lay.rows &&
                    (all_rows || (ed->dirty_any && line >= ed->dirty_first &&
                                  (ed->dirty_to_end || line <= ed->dirty_last)));
        if (paint) {
            paint_line(ed, win, &lay, &c, row, line);
            if (run_start < 0) run_start = row;
        } else if (run_start >= 0) {
            damage(win, 0, lay.rows_y + run_start * LINE_HEIGHT, lay.track_x,
                   (row - run_start) * LINE_HEIGHT);
            run_start = -1;
        }
    }
    if (all_rows) damage(win, 0, lay.area_y, lay.track_x, lay.area_bottom - lay.area_y);

    int thumb_y, thumb_h;
    thumb_geometry(ed, &lay, &thumb_y, &thumb_h);
    int thumb_active = ed->drag == DRAG_THUMB;
    if (chrome || thumb_y != ed->painted_thumb_y || thumb_h != ed->painted_thumb_h ||
        thumb_active != ed->painted_thumb_active) {
        paint_scrollbar(win, &lay, &c, thumb_y, thumb_h, thumb_active);
        damage(win, lay.track_x, lay.area_y, SCROLLBAR_W, lay.area_bottom - lay.area_y);
    }

    if (lay.prompt_y >= 0 && (chrome || ed->prompt_dirty)) {
        paint_prompt(ed, win, &lay, &c);
        damage(win, 0, lay.prompt_y, lay.width, PROMPT_H);
    }

    char left[sizeof(ed->painted_status_left)];
    char right[sizeof(ed->painted_status_right)];
    build_status(ed, left, sizeof(left), right, sizeof(right), lay.width);
    int error = ed->message[0] && ed->message_error;
    if (chrome || error != ed->painted_status_error || strcmp(left, ed->painted_status_left) != 0 ||
        strcmp(right, ed->painted_status_right) != 0) {
        paint_status(win, &lay, &c, left, right, error);
        damage(win, 0, lay.status_y, lay.width, STATUS_H);
        memcpy(ed->painted_status_left, left, sizeof(left));
        memcpy(ed->painted_status_right, right, sizeof(right));
        ed->painted_status_error = error;
    }

    char title[sizeof(ed->painted_title)];
    editor_title(ed, title, sizeof(title));
    int modified = editor_modified(ed);
    unsigned result = 0;
    if (chrome || modified != ed->painted_modified || strcmp(title, ed->painted_title) != 0)
        result |= EDITOR_DRAW_TITLE;
    memcpy(ed->painted_title, title, sizeof(title));
    ed->painted_modified = modified;

    ed->painted_layout = lay;
    ed->painted_colors = c;
    ed->painted_top = ed->top_line;
    ed->painted_left = ed->left_column;
    ed->painted_thumb_y = thumb_y;
    ed->painted_thumb_h = thumb_h;
    ed->painted_thumb_active = thumb_active;
    ed->painted_valid = 1;
    ed->need_full = 0;
    ed->need_text = 0;
    ed->dirty_any = 0;
    ed->dirty_to_end = 0;
    ed->prompt_dirty = 0;
    return result;
}
