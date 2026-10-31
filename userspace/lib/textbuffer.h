#pragma once

#include <stddef.h>

/* Largest text a buffer holds. */
#define TEXT_BUFFER_MAX_BYTES (4u * 1024u * 1024u)

/*
 * Text storage of the editor: a gap buffer (edits near the previous edit cost
 * only the distance moved), a line index kept in step with every edit, and a
 * bounded undo history.
 *
 * Positions are byte offsets in [0, length]. Lines are separated by '\n'; a
 * buffer always has at least one line, and a trailing '\n' starts an empty
 * last line.
 *
 * Mutating calls return 0 or a negative errno (-EINVAL for a range outside
 * the text, -E2BIG when the text would exceed TEXT_BUFFER_MAX_BYTES, -ENOMEM).
 * A failed call leaves text and history untouched.
 *
 * History: consecutive single-character typing or deleting that touches
 * adjacent text is merged into one undo step until text_buffer_seal() is
 * called; everything else is one step per call, and text_buffer_group_begin()/
 * text_buffer_group_end() make several calls one step.
 */
typedef struct TextBuffer TextBuffer;

TextBuffer *text_buffer_create(void);
void text_buffer_destroy(TextBuffer *buffer);

size_t text_buffer_length(const TextBuffer *buffer);
/* The byte at position, or -1 at and beyond the end. */
int text_buffer_at(const TextBuffer *buffer, size_t position);
/* Copies up to length bytes starting at position; returns how many. */
size_t text_buffer_read(const TextBuffer *buffer, size_t position, char *out, size_t length);
/* The whole text as one run of bytes (not NUL-terminated), valid until the
   next edit. Moves the gap out of the way, so it costs at most one move. */
const char *text_buffer_contiguous(TextBuffer *buffer, size_t *length);

size_t text_buffer_line_count(const TextBuffer *buffer);
size_t text_buffer_line_start(const TextBuffer *buffer, size_t line);
/* Position of the '\n' that ends the line, or the length for the last line. */
size_t text_buffer_line_end(const TextBuffer *buffer, size_t line);
/* The line containing position (the end of text belongs to the last line). */
size_t text_buffer_line_of(const TextBuffer *buffer, size_t position);

/* Replaces the whole text; clears the history and marks the buffer clean. */
int text_buffer_load(TextBuffer *buffer, const char *data, size_t length);
int text_buffer_insert(TextBuffer *buffer, size_t position, const char *data, size_t length);
int text_buffer_erase(TextBuffer *buffer, size_t position, size_t length);

/* Ends the current undo step: the next edit starts a new one. */
void text_buffer_seal(TextBuffer *buffer);
void text_buffer_group_begin(TextBuffer *buffer);
void text_buffer_group_end(TextBuffer *buffer);

/* Reverts / reapplies one step. Returns 1 when a step was taken, 0 when there
   is none, or a negative errno. *caret receives where the caret belongs
   afterwards: the end of restored text, or the spot where text was removed. */
int text_buffer_undo(TextBuffer *buffer, size_t *caret);
int text_buffer_redo(TextBuffer *buffer, size_t *caret);
int text_buffer_can_undo(const TextBuffer *buffer);
int text_buffer_can_redo(const TextBuffer *buffer);

/* True when the text differs from the last load or text_buffer_mark_clean();
   undoing back to that state makes it clean again. */
int text_buffer_modified(const TextBuffer *buffer);
void text_buffer_mark_clean(TextBuffer *buffer);
