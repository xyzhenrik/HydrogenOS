#pragma once

#include "textbuffer.h"

#include <stddef.h>

#define TEXT_EDIT_TAB_WIDTH 4

/*
 * Caret, selection and editing commands on top of a TextBuffer; no drawing
 * and no input devices, so the whole behaviour can be tested headless.
 *
 * The selection is the range between anchor and caret; with both equal there
 * is none. Columns are display columns: a tab advances to the next multiple of
 * TEXT_EDIT_TAB_WIDTH, every other byte takes one column.
 *
 * Commands that change text return 0 or a negative errno and leave caret,
 * selection and text unchanged when they fail.
 */
typedef struct TextEdit {
    TextBuffer *buffer;
    size_t caret;
    size_t anchor;
    /* Column that vertical moves try to keep; valid while goal_valid. */
    size_t goal_column;
    int goal_valid;
    int overwrite;
} TextEdit;

typedef enum TextMove {
    TEXT_MOVE_LEFT,
    TEXT_MOVE_RIGHT,
    TEXT_MOVE_UP,
    TEXT_MOVE_DOWN,
    TEXT_MOVE_LINE_START,   /* first non-blank, then column 0 */
    TEXT_MOVE_LINE_END,
    TEXT_MOVE_WORD_LEFT,
    TEXT_MOVE_WORD_RIGHT,
    TEXT_MOVE_TEXT_START,
    TEXT_MOVE_TEXT_END,
    TEXT_MOVE_PAGE_UP,
    TEXT_MOVE_PAGE_DOWN
} TextMove;

TextEdit *text_edit_create(void);
void text_edit_destroy(TextEdit *edit);

/* Replaces the text (caret at the start, no selection, no history). */
int text_edit_load(TextEdit *edit, const char *data, size_t length);

int text_edit_has_selection(const TextEdit *edit);
/* The selected range; start == end when nothing is selected. */
void text_edit_selection(const TextEdit *edit, size_t *start, size_t *end);

size_t text_edit_column_of(const TextEdit *edit, size_t position);
/* The position on line closest to the display column. */
size_t text_edit_position_at(const TextEdit *edit, size_t line, size_t column);

/* Moves the caret; with extend the anchor stays and the selection grows.
   page_lines is the distance of the page moves. */
void text_edit_move(TextEdit *edit, TextMove move, int extend, size_t page_lines);
void text_edit_set_caret(TextEdit *edit, size_t position, int extend);
void text_edit_select_all(TextEdit *edit);
void text_edit_select_word(TextEdit *edit, size_t position);
void text_edit_select_line(TextEdit *edit, size_t position);

/* Types text over the selection (or in overwrite mode over what follows the
   caret). Several bytes, or any newline, are one undo step. A '}' typed on an
   otherwise blank line first removes one indent level. */
int text_edit_insert(TextEdit *edit, const char *data, size_t length);
/* Splits the line, repeating its indentation; after '{' the new line is
   indented one more level, and a '}' that follows moves to its own line. */
int text_edit_newline(TextEdit *edit);
/* Inserts spaces up to the next tab stop. */
int text_edit_tab(TextEdit *edit);
int text_edit_backspace(TextEdit *edit);
int text_edit_delete(TextEdit *edit);

/* Undo and redo return 1 when a step was taken, 0 when there is none, or a
   negative errno. */
int text_edit_undo(TextEdit *edit);
int text_edit_redo(TextEdit *edit);

/* Clipboard commands; copy and cut with nothing selected do nothing. */
int text_edit_copy(TextEdit *edit);
int text_edit_cut(TextEdit *edit);
int text_edit_paste(TextEdit *edit);

/*
 * Finds the first occurrence of needle starting at from and wrapping around;
 * returns its position or -1. The search ignores case when needle has no
 * uppercase letter.
 */
long text_edit_find(const TextEdit *edit, const char *needle, size_t length,
                    size_t from, int forward);
