#include "textbuffer.h"
#include "errno.h"
#include "mini_libc.h"

#define INITIAL_CAPACITY      4096u
#define INITIAL_LINE_CAPACITY 128u
#define MIN_GROW              4096u

#define HISTORY_MAX_OPS   4096u
#define HISTORY_MAX_BYTES (8u * 1024u * 1024u)
/* Typing and deleting merge only in runs of single characters. */
#define COALESCE_MAX_BYTES 16u

typedef enum { OP_INSERT, OP_ERASE } OpKind;

/* One reversible edit: kind says what was done at position to data. */
typedef struct Op {
    OpKind kind;
    size_t position;
    size_t length;
    size_t capacity;
    char *data;
    unsigned group;
} Op;

struct TextBuffer {
    /* [gap_start, gap_end) is unused space inside data[0, capacity). */
    char *data;
    size_t capacity;
    size_t gap_start;
    size_t gap_end;

    /* lines[i] is the position where line i starts; lines[0] is always 0. */
    size_t *lines;
    size_t line_count;
    size_t line_capacity;

    /* ops[0, applied) are done, ops[applied, op_count) can be redone. */
    Op *ops;
    size_t op_count;
    size_t op_capacity;
    size_t applied;
    size_t history_bytes;
    unsigned next_group;
    unsigned compound_group;
    int compound_depth;
    /* The last op may still absorb the next typed or deleted character. */
    int mergeable;
    /* Value of applied at the last clean state; -1 once it is unreachable. */
    long clean_applied;
};

static size_t text_size(const TextBuffer *b) {
    return b->capacity - (b->gap_end - b->gap_start);
}

static size_t count_newlines(const char *data, size_t length) {
    size_t count = 0;
    for (size_t i = 0; i < length; ++i)
        if (data[i] == '\n') ++count;
    return count;
}

/* ------------------------------------------------------------------ */
/* Gap buffer                                                          */
/* ------------------------------------------------------------------ */

static void move_gap(TextBuffer *b, size_t position) {
    size_t gap = b->gap_end - b->gap_start;
    if (position < b->gap_start) {
        size_t count = b->gap_start - position;
        memmove(b->data + b->gap_end - count, b->data + position, count);
        b->gap_start = position;
        b->gap_end = position + gap;
    } else if (position > b->gap_start) {
        size_t count = position - b->gap_start;
        memmove(b->data + b->gap_start, b->data + b->gap_end, count);
        b->gap_start = position;
        b->gap_end = position + gap;
    }
}

/* Makes room for extra more bytes of text and extra_lines more line starts. */
static int reserve(TextBuffer *b, size_t extra, size_t extra_lines) {
    size_t length = text_size(b);
    if (extra > TEXT_BUFFER_MAX_BYTES - length) return -E2BIG;

    if (b->gap_end - b->gap_start < extra) {
        size_t needed = length + extra;
        size_t grown = b->capacity * 2;
        if (grown < needed + MIN_GROW) grown = needed + MIN_GROW;
        move_gap(b, length);
        char *data = realloc(b->data, grown);
        if (!data) return -ENOMEM;
        b->data = data;
        b->capacity = grown;
        b->gap_end = grown;
    }

    size_t lines_needed = b->line_count + extra_lines;
    if (lines_needed > b->line_capacity) {
        size_t grown = b->line_capacity * 2;
        if (grown < lines_needed) grown = lines_needed;
        size_t *lines = realloc(b->lines, grown * sizeof(size_t));
        if (!lines) return -ENOMEM;
        b->lines = lines;
        b->line_capacity = grown;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Line index                                                          */
/* ------------------------------------------------------------------ */

size_t text_buffer_line_of(const TextBuffer *b, size_t position) {
    size_t low = 0, high = b->line_count;
    while (high - low > 1) {
        size_t mid = low + (high - low) / 2;
        if (b->lines[mid] <= position) low = mid;
        else high = mid;
    }
    return low;
}

/* Callers guarantee that reserve() made room. */
static void apply_insert(TextBuffer *b, size_t position, const char *data, size_t length) {
    size_t line = text_buffer_line_of(b, position);
    size_t added = count_newlines(data, length);

    move_gap(b, position);
    memcpy(b->data + b->gap_start, data, length);
    b->gap_start += length;

    for (size_t i = line + 1; i < b->line_count; ++i) b->lines[i] += length;
    if (added > 0) {
        memmove(b->lines + line + 1 + added, b->lines + line + 1,
                (b->line_count - line - 1) * sizeof(size_t));
        size_t next = line + 1;
        for (size_t i = 0; i < length; ++i)
            if (data[i] == '\n') b->lines[next++] = position + i + 1;
        b->line_count += added;
    }
}

static void apply_erase(TextBuffer *b, size_t position, size_t length) {
    size_t first = text_buffer_line_of(b, position) + 1;
    size_t end = first;
    while (end < b->line_count && b->lines[end] <= position + length) ++end;

    move_gap(b, position);
    b->gap_end += length;

    size_t removed = end - first;
    if (removed > 0) {
        memmove(b->lines + first, b->lines + end, (b->line_count - end) * sizeof(size_t));
        b->line_count -= removed;
    }
    for (size_t i = first; i < b->line_count; ++i) b->lines[i] -= length;
}

/* ------------------------------------------------------------------ */
/* Lifetime and reading                                                */
/* ------------------------------------------------------------------ */

TextBuffer *text_buffer_create(void) {
    TextBuffer *b = calloc(1, sizeof(*b));
    if (!b) return 0;
    b->data = malloc(INITIAL_CAPACITY);
    b->lines = malloc(INITIAL_LINE_CAPACITY * sizeof(size_t));
    if (!b->data || !b->lines) {
        free(b->data);
        free(b->lines);
        free(b);
        return 0;
    }
    b->capacity = INITIAL_CAPACITY;
    b->gap_end = INITIAL_CAPACITY;
    b->line_capacity = INITIAL_LINE_CAPACITY;
    b->line_count = 1;
    b->lines[0] = 0;
    return b;
}

static void clear_history(TextBuffer *b) {
    for (size_t i = 0; i < b->op_count; ++i) free(b->ops[i].data);
    b->op_count = 0;
    b->applied = 0;
    b->history_bytes = 0;
    b->mergeable = 0;
    b->clean_applied = 0;
}

void text_buffer_destroy(TextBuffer *b) {
    if (!b) return;
    clear_history(b);
    free(b->ops);
    free(b->data);
    free(b->lines);
    free(b);
}

size_t text_buffer_length(const TextBuffer *b) {
    return text_size(b);
}

int text_buffer_at(const TextBuffer *b, size_t position) {
    if (position >= text_size(b)) return -1;
    return (unsigned char)(position < b->gap_start ? b->data[position]
                                                   : b->data[position + (b->gap_end - b->gap_start)]);
}

size_t text_buffer_read(const TextBuffer *b, size_t position, char *out, size_t length) {
    size_t total = text_size(b);
    if (position >= total) return 0;
    if (length > total - position) length = total - position;

    size_t copied = 0;
    if (position < b->gap_start) {
        size_t head = b->gap_start - position;
        if (head > length) head = length;
        memcpy(out, b->data + position, head);
        copied = head;
        position += head;
    }
    if (copied < length) {
        size_t gap = b->gap_end - b->gap_start;
        memcpy(out + copied, b->data + position + gap, length - copied);
    }
    return length;
}

const char *text_buffer_contiguous(TextBuffer *b, size_t *length) {
    move_gap(b, text_size(b));
    if (length) *length = text_size(b);
    return b->data;
}

size_t text_buffer_line_count(const TextBuffer *b) {
    return b->line_count;
}

size_t text_buffer_line_start(const TextBuffer *b, size_t line) {
    return line < b->line_count ? b->lines[line] : text_size(b);
}

size_t text_buffer_line_end(const TextBuffer *b, size_t line) {
    if (line + 1 < b->line_count) return b->lines[line + 1] - 1;
    return text_size(b);
}

/* ------------------------------------------------------------------ */
/* History                                                             */
/* ------------------------------------------------------------------ */

/* Drops the oldest step. Never touches steps that can only be redone. */
static int drop_oldest_group(TextBuffer *b) {
    if (b->applied == 0) return 0;
    unsigned group = b->ops[0].group;
    size_t count = 0;
    while (count < b->applied && b->ops[count].group == group) ++count;

    for (size_t i = 0; i < count; ++i) {
        b->history_bytes -= b->ops[i].length;
        free(b->ops[i].data);
    }
    memmove(b->ops, b->ops + count, (b->op_count - count) * sizeof(Op));
    b->op_count -= count;
    b->applied -= count;
    if (b->clean_applied >= (long)count) b->clean_applied -= (long)count;
    else b->clean_applied = -1;
    return 1;
}

static void trim_history(TextBuffer *b) {
    while (b->op_count > HISTORY_MAX_OPS || b->history_bytes > HISTORY_MAX_BYTES) {
        /* The step just recorded always survives, even when it alone is large. */
        if (b->applied <= 1 || b->ops[0].group == b->ops[b->applied - 1].group) break;
        if (!drop_oldest_group(b)) break;
    }
}

static void discard_redo(TextBuffer *b) {
    if (b->applied == b->op_count) return;
    for (size_t i = b->applied; i < b->op_count; ++i) {
        b->history_bytes -= b->ops[i].length;
        free(b->ops[i].data);
    }
    b->op_count = b->applied;
    if (b->clean_applied > (long)b->applied) b->clean_applied = -1;
}

/* Tries to extend the previous op with this edit. */
static int merge_into_last(TextBuffer *b, OpKind kind, size_t position,
                           const char *data, size_t length) {
    if (!b->mergeable || b->compound_depth > 0 || b->op_count == 0 ||
        length > COALESCE_MAX_BYTES)
        return 0;
    Op *last = &b->ops[b->op_count - 1];
    if (last->kind != kind) return 0;

    int prepend = 0;
    if (kind == OP_INSERT) {
        if (position != last->position + last->length) return 0;
    } else if (position == last->position) {
        /* Forward delete: the next character joins at the end. */
    } else if (position + length == last->position) {
        prepend = 1;
    } else {
        return 0;
    }

    if (last->length + length > last->capacity) {
        size_t grown = last->capacity * 2;
        if (grown < last->length + length) grown = last->length + length;
        if (grown < 32) grown = 32;
        char *bigger = realloc(last->data, grown);
        if (!bigger) return 0;
        last->data = bigger;
        last->capacity = grown;
    }
    if (prepend) {
        memmove(last->data + length, last->data, last->length);
        memcpy(last->data, data, length);
        last->position = position;
    } else {
        memcpy(last->data + last->length, data, length);
    }
    last->length += length;
    b->history_bytes += length;
    return 1;
}

/* Records an edit that is about to be applied. On failure nothing changed. */
static int record(TextBuffer *b, OpKind kind, size_t position, const char *data, size_t length) {
    if (b->applied == b->op_count && merge_into_last(b, kind, position, data, length))
        return 0;

    if (b->applied + 1 > b->op_capacity) {
        size_t grown = b->op_capacity ? b->op_capacity * 2 : 64;
        Op *ops = realloc(b->ops, grown * sizeof(Op));
        if (!ops) return -ENOMEM;
        b->ops = ops;
        b->op_capacity = grown;
    }
    char *copy = malloc(length);
    if (!copy) return -ENOMEM;
    memcpy(copy, data, length);

    discard_redo(b);
    Op *op = &b->ops[b->op_count++];
    op->kind = kind;
    op->position = position;
    op->length = length;
    op->capacity = length;
    op->data = copy;
    op->group = b->compound_depth > 0 ? b->compound_group : ++b->next_group;
    b->applied = b->op_count;
    b->history_bytes += length;
    b->mergeable = b->compound_depth == 0;
    trim_history(b);
    return 0;
}

void text_buffer_seal(TextBuffer *b) {
    b->mergeable = 0;
}

void text_buffer_group_begin(TextBuffer *b) {
    if (b->compound_depth++ == 0) {
        b->compound_group = ++b->next_group;
        b->mergeable = 0;
    }
}

void text_buffer_group_end(TextBuffer *b) {
    if (b->compound_depth > 0) --b->compound_depth;
    b->mergeable = 0;
}

int text_buffer_can_undo(const TextBuffer *b) {
    return b->applied > 0;
}

int text_buffer_can_redo(const TextBuffer *b) {
    return b->applied < b->op_count;
}

int text_buffer_modified(const TextBuffer *b) {
    return b->clean_applied < 0 || (size_t)b->clean_applied != b->applied;
}

void text_buffer_mark_clean(TextBuffer *b) {
    b->mergeable = 0;
    b->clean_applied = (long)b->applied;
}

/* Applies the ops of one step in the given direction, all or nothing. */
static int step(TextBuffer *b, int undo, size_t *caret) {
    size_t first, last; /* inclusive range of op indices in the step */
    if (undo) {
        if (b->applied == 0) return 0;
        last = b->applied - 1;
        first = last;
        while (first > 0 && b->ops[first - 1].group == b->ops[last].group) --first;
    } else {
        if (b->applied == b->op_count) return 0;
        first = b->applied;
        last = first;
        while (last + 1 < b->op_count && b->ops[last + 1].group == b->ops[first].group) ++last;
    }

    size_t insert_bytes = 0, insert_lines = 0;
    for (size_t i = first; i <= last; ++i) {
        const Op *op = &b->ops[i];
        if ((op->kind == OP_ERASE) == (undo != 0)) {
            insert_bytes += op->length;
            insert_lines += count_newlines(op->data, op->length);
        }
    }
    int rc = reserve(b, insert_bytes, insert_lines);
    if (rc != 0) return rc;

    for (size_t n = 0; n <= last - first; ++n) {
        size_t i = undo ? last - n : first + n;
        const Op *op = &b->ops[i];
        int insert = (op->kind == OP_ERASE) == (undo != 0);
        if (insert) {
            apply_insert(b, op->position, op->data, op->length);
            *caret = op->position + op->length;
        } else {
            apply_erase(b, op->position, op->length);
            *caret = op->position;
        }
    }
    b->applied = undo ? first : last + 1;
    b->mergeable = 0;
    return 1;
}

int text_buffer_undo(TextBuffer *b, size_t *caret) {
    size_t unused;
    return step(b, 1, caret ? caret : &unused);
}

int text_buffer_redo(TextBuffer *b, size_t *caret) {
    size_t unused;
    return step(b, 0, caret ? caret : &unused);
}

/* ------------------------------------------------------------------ */
/* Edits                                                               */
/* ------------------------------------------------------------------ */

int text_buffer_load(TextBuffer *b, const char *data, size_t length) {
    if (length > 0 && !data) return -EINVAL;
    if (length > TEXT_BUFFER_MAX_BYTES) return -E2BIG;

    size_t newlines = count_newlines(data, length);
    size_t capacity = length + INITIAL_CAPACITY;
    size_t line_capacity = newlines + 1 + INITIAL_LINE_CAPACITY;
    char *text = malloc(capacity);
    size_t *lines = malloc(line_capacity * sizeof(size_t));
    if (!text || !lines) {
        free(text);
        free(lines);
        return -ENOMEM;
    }
    if (length > 0) memcpy(text, data, length);
    lines[0] = 0;
    size_t line = 1;
    for (size_t i = 0; i < length; ++i)
        if (data[i] == '\n') lines[line++] = i + 1;

    free(b->data);
    free(b->lines);
    b->data = text;
    b->capacity = capacity;
    b->gap_start = length;
    b->gap_end = capacity;
    b->lines = lines;
    b->line_count = line;
    b->line_capacity = line_capacity;
    clear_history(b);
    return 0;
}

int text_buffer_insert(TextBuffer *b, size_t position, const char *data, size_t length) {
    if (position > text_size(b) || (length > 0 && !data)) return -EINVAL;
    if (length == 0) return 0;

    int rc = reserve(b, length, count_newlines(data, length));
    if (rc != 0) return rc;
    rc = record(b, OP_INSERT, position, data, length);
    if (rc != 0) return rc;
    apply_insert(b, position, data, length);
    return 0;
}

int text_buffer_erase(TextBuffer *b, size_t position, size_t length) {
    if (position > text_size(b) || length > text_size(b) - position) return -EINVAL;
    if (length == 0) return 0;

    char small[COALESCE_MAX_BYTES];
    char *removed = small;
    if (length > COALESCE_MAX_BYTES) {
        removed = malloc(length);
        if (!removed) return -ENOMEM;
    }
    text_buffer_read(b, position, removed, length);
    int rc = record(b, OP_ERASE, position, removed, length);
    if (removed != small) free(removed);
    if (rc != 0) return rc;
    apply_erase(b, position, length);
    return 0;
}
