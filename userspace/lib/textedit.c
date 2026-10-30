#include "textedit.h"
#include "clipboard.h"
#include "errno.h"
#include "mini_libc.h"

/* Longest indentation a new line inherits. */
#define INDENT_MAX 256u

typedef enum { CLASS_BLANK, CLASS_WORD, CLASS_PUNCT, CLASS_NEWLINE } CharClass;

static CharClass class_of(int c) {
    if (c < 0 || c == '\n') return CLASS_NEWLINE;
    if (c == ' ' || c == '\t') return CLASS_BLANK;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        c == '_' || c >= 0x80)
        return CLASS_WORD;
    return CLASS_PUNCT;
}

static size_t length_of(const TextEdit *e) {
    return text_buffer_length(e->buffer);
}

static int at(const TextEdit *e, size_t position) {
    return text_buffer_at(e->buffer, position);
}

TextEdit *text_edit_create(void) {
    TextEdit *e = calloc(1, sizeof(*e));
    if (!e) return 0;
    e->buffer = text_buffer_create();
    if (!e->buffer) {
        free(e);
        return 0;
    }
    return e;
}

void text_edit_destroy(TextEdit *e) {
    if (!e) return;
    text_buffer_destroy(e->buffer);
    free(e);
}

int text_edit_load(TextEdit *e, const char *data, size_t length) {
    int rc = text_buffer_load(e->buffer, data, length);
    if (rc != 0) return rc;
    e->caret = 0;
    e->anchor = 0;
    e->goal_valid = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Selection and columns                                               */
/* ------------------------------------------------------------------ */

int text_edit_has_selection(const TextEdit *e) {
    return e->caret != e->anchor;
}

void text_edit_selection(const TextEdit *e, size_t *start, size_t *end) {
    *start = e->caret < e->anchor ? e->caret : e->anchor;
    *end = e->caret < e->anchor ? e->anchor : e->caret;
}

static size_t column_after(size_t column, int c) {
    return c == '\t' ? column + TEXT_EDIT_TAB_WIDTH - column % TEXT_EDIT_TAB_WIDTH : column + 1;
}

size_t text_edit_column_of(const TextEdit *e, size_t position) {
    size_t line = text_buffer_line_of(e->buffer, position);
    size_t column = 0;
    for (size_t p = text_buffer_line_start(e->buffer, line); p < position; ++p)
        column = column_after(column, at(e, p));
    return column;
}

size_t text_edit_position_at(const TextEdit *e, size_t line, size_t column) {
    size_t last = text_buffer_line_count(e->buffer) - 1;
    if (line > last) line = last;
    size_t end = text_buffer_line_end(e->buffer, line);
    size_t position = text_buffer_line_start(e->buffer, line);
    size_t current = 0;
    while (position < end && current < column) {
        size_t next = column_after(current, at(e, position));
        /* Inside a wide character the nearer edge wins. */
        if (next > column && (column - current) * 2 < next - current) break;
        current = next;
        ++position;
    }
    return position;
}

/* ------------------------------------------------------------------ */
/* Movement                                                            */
/* ------------------------------------------------------------------ */

static void finish_move(TextEdit *e, size_t position, int extend) {
    e->caret = position;
    if (!extend) e->anchor = position;
    text_buffer_seal(e->buffer);
}

static size_t word_right(const TextEdit *e, size_t p) {
    size_t length = length_of(e);
    if (p >= length) return length;
    if (at(e, p) == '\n') {
        ++p;
    } else {
        CharClass c = class_of(at(e, p));
        if (c != CLASS_BLANK)
            while (p < length && class_of(at(e, p)) == c) ++p;
    }
    while (p < length && class_of(at(e, p)) == CLASS_BLANK) ++p;
    return p;
}

static size_t word_left(const TextEdit *e, size_t p) {
    while (p > 0 && class_of(at(e, p - 1)) == CLASS_BLANK) --p;
    if (p == 0) return 0;
    CharClass c = class_of(at(e, p - 1));
    if (c == CLASS_NEWLINE) return p - 1;
    while (p > 0 && class_of(at(e, p - 1)) == c) --p;
    return p;
}

static size_t first_non_blank(const TextEdit *e, size_t line) {
    size_t p = text_buffer_line_start(e->buffer, line);
    size_t end = text_buffer_line_end(e->buffer, line);
    while (p < end && class_of(at(e, p)) == CLASS_BLANK) ++p;
    return p;
}

void text_edit_move(TextEdit *e, TextMove move, int extend, size_t page_lines) {
    size_t line = text_buffer_line_of(e->buffer, e->caret);
    size_t last_line = text_buffer_line_count(e->buffer) - 1;
    size_t target = e->caret;
    int vertical = 0;
    size_t start, end;
    text_edit_selection(e, &start, &end);

    switch (move) {
    case TEXT_MOVE_LEFT:
        if (!extend && start != end) target = start;
        else if (e->caret > 0) target = e->caret - 1;
        break;
    case TEXT_MOVE_RIGHT:
        if (!extend && start != end) target = end;
        else if (e->caret < length_of(e)) target = e->caret + 1;
        break;
    case TEXT_MOVE_UP:
    case TEXT_MOVE_DOWN:
    case TEXT_MOVE_PAGE_UP:
    case TEXT_MOVE_PAGE_DOWN: {
        size_t step = (move == TEXT_MOVE_UP || move == TEXT_MOVE_DOWN) ? 1 : (page_lines ? page_lines : 1);
        int up = move == TEXT_MOVE_UP || move == TEXT_MOVE_PAGE_UP;
        size_t column = e->goal_valid ? e->goal_column : text_edit_column_of(e, e->caret);
        vertical = 1;
        e->goal_column = column;
        if (up) {
            target = line == 0 ? 0 : text_edit_position_at(e, line > step ? line - step : 0, column);
        } else {
            target = line == last_line ? length_of(e)
                                       : text_edit_position_at(e, line + step > last_line ? last_line : line + step, column);
        }
        break;
    }
    case TEXT_MOVE_LINE_START: {
        size_t smart = first_non_blank(e, line);
        target = e->caret != smart ? smart : text_buffer_line_start(e->buffer, line);
        break;
    }
    case TEXT_MOVE_LINE_END:
        target = text_buffer_line_end(e->buffer, line);
        break;
    case TEXT_MOVE_WORD_LEFT:
        target = word_left(e, e->caret);
        break;
    case TEXT_MOVE_WORD_RIGHT:
        target = word_right(e, e->caret);
        break;
    case TEXT_MOVE_TEXT_START:
        target = 0;
        break;
    case TEXT_MOVE_TEXT_END:
        target = length_of(e);
        break;
    }
    e->goal_valid = vertical;
    finish_move(e, target, extend);
}

void text_edit_set_caret(TextEdit *e, size_t position, int extend) {
    if (position > length_of(e)) position = length_of(e);
    e->goal_valid = 0;
    finish_move(e, position, extend);
}

void text_edit_select_all(TextEdit *e) {
    e->anchor = 0;
    e->caret = length_of(e);
    e->goal_valid = 0;
    text_buffer_seal(e->buffer);
}

void text_edit_select_word(TextEdit *e, size_t position) {
    size_t length = length_of(e);
    if (position > length) position = length;
    if (position == length || at(e, position) == '\n') {
        if (position > 0 && at(e, position - 1) != '\n') --position;
        else {
            e->anchor = e->caret = position;
            return;
        }
    }
    CharClass c = class_of(at(e, position));
    size_t start = position, end = position + 1;
    while (start > 0 && class_of(at(e, start - 1)) == c) --start;
    while (end < length && class_of(at(e, end)) == c) ++end;
    e->anchor = start;
    e->caret = end;
    e->goal_valid = 0;
    text_buffer_seal(e->buffer);
}

void text_edit_select_line(TextEdit *e, size_t position) {
    size_t line = text_buffer_line_of(e->buffer, position);
    e->anchor = text_buffer_line_start(e->buffer, line);
    e->caret = text_buffer_line_end(e->buffer, line);
    if (line + 1 < text_buffer_line_count(e->buffer)) ++e->caret;
    e->goal_valid = 0;
    text_buffer_seal(e->buffer);
}

/* ------------------------------------------------------------------ */
/* Editing                                                             */
/* ------------------------------------------------------------------ */

/* Replaces [start, end) with data as one undo step; the caret ends up
   caret_offset bytes after start. */
static int replace(TextEdit *e, size_t start, size_t end, const char *data, size_t length,
                   size_t caret_offset) {
    size_t removed = end - start;
    if (length > removed && length - removed > TEXT_BUFFER_MAX_BYTES - length_of(e))
        return -E2BIG;

    text_buffer_group_begin(e->buffer);
    int rc = removed > 0 ? text_buffer_erase(e->buffer, start, removed) : 0;
    if (rc == 0) {
        rc = text_buffer_insert(e->buffer, start, data, length);
        if (rc != 0 && removed > 0) {
            size_t unused;
            text_buffer_undo(e->buffer, &unused);
        }
    }
    text_buffer_group_end(e->buffer);
    if (rc != 0) return rc;

    e->caret = e->anchor = start + caret_offset;
    e->goal_valid = 0;
    return 0;
}

static int erase_selection(TextEdit *e) {
    size_t start, end;
    text_edit_selection(e, &start, &end);
    text_buffer_seal(e->buffer);
    int rc = text_buffer_erase(e->buffer, start, end - start);
    text_buffer_seal(e->buffer);
    if (rc != 0) return rc;
    e->caret = e->anchor = start;
    e->goal_valid = 0;
    return 0;
}

int text_edit_insert(TextEdit *e, const char *data, size_t length) {
    if (length == 0) return 0;
    size_t start, end;
    text_edit_selection(e, &start, &end);
    int typed = length == 1 && data[0] != '\n';

    if (start != end) return replace(e, start, end, data, length, length);

    if (typed && e->overwrite && start < length_of(e) && at(e, start) != '\n')
        return replace(e, start, start + 1, data, length, length);

    /* A closing brace on an otherwise blank line steps back one indent level. */
    if (typed && data[0] == '}') {
        size_t line_start = text_buffer_line_start(e->buffer, text_buffer_line_of(e->buffer, start));
        size_t blank = 0;
        while (line_start + blank < start && class_of(at(e, line_start + blank)) == CLASS_BLANK) ++blank;
        if (line_start + blank == start && blank > 0) {
            size_t back = 0;
            if (at(e, start - 1) == '\t') back = 1;
            else
                while (back < blank && back < TEXT_EDIT_TAB_WIDTH && at(e, start - back - 1) == ' ') ++back;
            return replace(e, start - back, start, data, length, length);
        }
    }

    if (!typed) text_buffer_seal(e->buffer);
    int rc = text_buffer_insert(e->buffer, start, data, length);
    if (!typed) text_buffer_seal(e->buffer);
    if (rc != 0) return rc;
    e->caret = e->anchor = start + length;
    e->goal_valid = 0;
    return 0;
}

int text_edit_newline(TextEdit *e) {
    size_t start, end;
    text_edit_selection(e, &start, &end);

    size_t line_start = text_buffer_line_start(e->buffer, text_buffer_line_of(e->buffer, start));
    size_t indent = 0;
    while (indent < INDENT_MAX && line_start + indent < start &&
           class_of(at(e, line_start + indent)) == CLASS_BLANK)
        ++indent;

    size_t before = start;
    while (before > line_start && class_of(at(e, before - 1)) == CLASS_BLANK) --before;
    int opens_block = before > line_start && at(e, before - 1) == '{';
    int closes_block = opens_block && at(e, end) == '}';

    char text[2 * (INDENT_MAX + TEXT_EDIT_TAB_WIDTH) + 2];
    size_t n = 0;
    text[n++] = '\n';
    n += text_buffer_read(e->buffer, line_start, text + n, indent);
    if (opens_block)
        for (int i = 0; i < TEXT_EDIT_TAB_WIDTH; ++i) text[n++] = ' ';
    size_t caret_offset = n;
    if (closes_block) {
        text[n++] = '\n';
        n += text_buffer_read(e->buffer, line_start, text + n, indent);
    }
    return replace(e, start, end, text, n, caret_offset);
}

int text_edit_tab(TextEdit *e) {
    size_t start, end;
    text_edit_selection(e, &start, &end);
    size_t count = TEXT_EDIT_TAB_WIDTH - text_edit_column_of(e, start) % TEXT_EDIT_TAB_WIDTH;
    char spaces[TEXT_EDIT_TAB_WIDTH];
    memset(spaces, ' ', sizeof(spaces));
    return text_edit_insert(e, spaces, count);
}

int text_edit_backspace(TextEdit *e) {
    if (text_edit_has_selection(e)) return erase_selection(e);
    if (e->caret == 0) return 0;
    int rc = text_buffer_erase(e->buffer, e->caret - 1, 1);
    if (rc != 0) return rc;
    e->caret = e->anchor = e->caret - 1;
    e->goal_valid = 0;
    return 0;
}

int text_edit_delete(TextEdit *e) {
    if (text_edit_has_selection(e)) return erase_selection(e);
    if (e->caret >= length_of(e)) return 0;
    e->goal_valid = 0;
    return text_buffer_erase(e->buffer, e->caret, 1);
}

static int history_step(TextEdit *e, int undo) {
    size_t caret = e->caret;
    int rc = undo ? text_buffer_undo(e->buffer, &caret) : text_buffer_redo(e->buffer, &caret);
    if (rc > 0) {
        e->caret = e->anchor = caret;
        e->goal_valid = 0;
    }
    return rc;
}

int text_edit_undo(TextEdit *e) {
    return history_step(e, 1);
}

int text_edit_redo(TextEdit *e) {
    return history_step(e, 0);
}

/* ------------------------------------------------------------------ */
/* Clipboard                                                           */
/* ------------------------------------------------------------------ */

int text_edit_copy(TextEdit *e) {
    size_t start, end;
    text_edit_selection(e, &start, &end);
    if (start == end) return 0;

    char *copy = malloc(end - start);
    if (!copy) return -ENOMEM;
    text_buffer_read(e->buffer, start, copy, end - start);
    int rc = clipboard_set(copy, end - start);
    free(copy);
    return rc;
}

int text_edit_cut(TextEdit *e) {
    if (!text_edit_has_selection(e)) return 0;
    int rc = text_edit_copy(e);
    return rc != 0 ? rc : erase_selection(e);
}

int text_edit_paste(TextEdit *e) {
    size_t length;
    const char *data = clipboard_data(&length);
    if (length == 0) return 0;
    text_buffer_seal(e->buffer);
    size_t start, end;
    text_edit_selection(e, &start, &end);
    return replace(e, start, end, data, length, length);
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

static int fold(int c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int matches_at(const TextEdit *e, size_t position, const char *needle, size_t length,
                      int ignore_case) {
    for (size_t i = 0; i < length; ++i) {
        int c = at(e, position + i);
        int n = (unsigned char)needle[i];
        if (ignore_case ? fold(c) != n : c != n) return 0;
    }
    return 1;
}

long text_edit_find(const TextEdit *e, const char *needle, size_t length, size_t from, int forward) {
    size_t total = length_of(e);
    if (length == 0 || length > total) return -1;

    int ignore_case = 1;
    for (size_t i = 0; i < length; ++i)
        if (needle[i] >= 'A' && needle[i] <= 'Z') ignore_case = 0;

    /* Every start in [0, last] visited once, beginning at from (or just
       before it when searching backwards) and wrapping around. */
    size_t last = total - length;
    size_t candidates = last + 1;
    if (from > candidates) from = forward ? 0 : candidates;
    size_t position = forward ? from % candidates : (from + candidates - 1) % candidates;

    for (size_t visited = 0; visited < candidates; ++visited) {
        if (matches_at(e, position, needle, length, ignore_case)) return (long)position;
        if (forward) position = position == last ? 0 : position + 1;
        else position = position == 0 ? last : position - 1;
    }
    return -1;
}
