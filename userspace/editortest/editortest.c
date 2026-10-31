#include "mini_libc.h"
#include "clipboard.h"
#include "errno.h"
#include "textbuffer.h"
#include "textedit.h"

#define CHECK(cond)                                    \
    do {                                               \
        if (!(cond))                                   \
            return fail(#cond, __LINE__);              \
    } while (0)

static int fail(const char *expression, int line) {
    dprintf(2, "EDITOR FAIL: %s (line %d)\n", expression, line);
    return 1;
}

/* Deterministic pseudo-random numbers for the differential tests. */
static unsigned long rng_state = 12345;

static unsigned long rng(unsigned long bound) {
    rng_state = rng_state * 6364136223846793005UL + 1442695040888963407UL;
    return (rng_state >> 33) % bound;
}

static int text_is(const TextBuffer *buffer, const char *expected) {
    size_t length = strlen(expected);
    if (text_buffer_length(buffer) != length) return 0;
    char copy[1024];
    if (length >= sizeof(copy)) return 0;
    return text_buffer_read(buffer, 0, copy, length) == length && memcmp(copy, expected, length) == 0;
}

static int edit_is(const TextEdit *edit, const char *expected) {
    return text_is(edit->buffer, expected);
}

/* The line index must agree with a plain scan of the text. */
static int lines_consistent(const TextBuffer *buffer) {
    size_t length = text_buffer_length(buffer);
    size_t line = 0, start = 0;
    if (text_buffer_line_start(buffer, 0) != 0) return 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || text_buffer_at(buffer, i) == '\n') {
            if (text_buffer_line_start(buffer, line) != start) return 0;
            if (text_buffer_line_end(buffer, line) != i) return 0;
            if (text_buffer_line_of(buffer, start) != line || text_buffer_line_of(buffer, i) != line) return 0;
            ++line;
            start = i + 1;
        }
    }
    return text_buffer_line_count(buffer) == line;
}

static int insert_text(TextBuffer *buffer, size_t position, const char *text) {
    return text_buffer_insert(buffer, position, text, strlen(text));
}

/* ------------------------------------------------------------------ */
/* TextBuffer                                                          */
/* ------------------------------------------------------------------ */

static int check_buffer_basics(void) {
    TextBuffer *b = text_buffer_create();
    CHECK(b);
    CHECK(text_buffer_length(b) == 0 && text_buffer_line_count(b) == 1);
    CHECK(text_buffer_line_end(b, 0) == 0 && text_buffer_at(b, 0) == -1);
    CHECK(!text_buffer_modified(b) && !text_buffer_can_undo(b) && !text_buffer_can_redo(b));

    CHECK(insert_text(b, 0, "world") == 0);
    CHECK(insert_text(b, 0, "hello ") == 0);
    CHECK(insert_text(b, 11, "!") == 0);
    CHECK(insert_text(b, 5, ",") == 0);
    CHECK(text_is(b, "hello, world!"));
    CHECK(text_buffer_at(b, 0) == 'h' && text_buffer_at(b, 12) == '!' && text_buffer_at(b, 13) == -1);

    /* Reads straddle the gap wherever it sits. */
    char out[32];
    for (size_t from = 0; from <= 13; ++from) {
        for (size_t count = 0; from + count <= 13; ++count) {
            CHECK(text_buffer_read(b, from, out, count) == count);
            CHECK(memcmp(out, "hello, world!" + from, count) == 0);
        }
    }
    CHECK(text_buffer_read(b, 10, out, 100) == 3 && text_buffer_read(b, 13, out, 1) == 0);

    CHECK(text_buffer_erase(b, 5, 1) == 0 && text_is(b, "hello world!"));
    CHECK(text_buffer_erase(b, 11, 1) == 0 && text_is(b, "hello world"));
    CHECK(text_buffer_erase(b, 0, 6) == 0 && text_is(b, "world"));
    CHECK(text_buffer_erase(b, 0, 5) == 0 && text_is(b, "") && text_buffer_line_count(b) == 1);

    CHECK(text_buffer_insert(b, 1, "x", 1) == -EINVAL);
    CHECK(text_buffer_erase(b, 0, 1) == -EINVAL);
    CHECK(text_buffer_insert(b, 0, "", 0) == 0 && text_buffer_erase(b, 0, 0) == 0);

    text_buffer_destroy(b);
    return 0;
}

static int check_buffer_lines(void) {
    TextBuffer *b = text_buffer_create();
    CHECK(b);
    CHECK(insert_text(b, 0, "one\ntwo\nthree") == 0);
    CHECK(text_buffer_line_count(b) == 3 && lines_consistent(b));
    CHECK(text_buffer_line_start(b, 1) == 4 && text_buffer_line_end(b, 1) == 7);
    CHECK(text_buffer_line_of(b, 3) == 0 && text_buffer_line_of(b, 4) == 1 && text_buffer_line_of(b, 13) == 2);

    CHECK(insert_text(b, 4, "\n\n") == 0 && lines_consistent(b));
    CHECK(text_buffer_line_count(b) == 5 && text_buffer_line_end(b, 1) == 4);
    CHECK(insert_text(b, 13, "\n") == 0 && text_buffer_line_count(b) == 6 && lines_consistent(b));
    CHECK(text_buffer_line_start(b, 5) == 14 && text_buffer_line_end(b, 5) == 16);

    CHECK(text_buffer_erase(b, 3, 3) == 0 && lines_consistent(b));
    CHECK(text_is(b, "onetwo\nthr\nee") && text_buffer_line_count(b) == 3);
    CHECK(text_buffer_erase(b, 0, text_buffer_length(b)) == 0);
    CHECK(text_buffer_line_count(b) == 1 && lines_consistent(b));

    /* Text that ends in a newline has an empty last line. */
    CHECK(insert_text(b, 0, "a\n") == 0);
    CHECK(text_buffer_line_count(b) == 2 && text_buffer_line_start(b, 1) == 2 &&
          text_buffer_line_end(b, 1) == 2 && lines_consistent(b));

    CHECK(text_buffer_load(b, "x\ny\n\nz", 6) == 0);
    CHECK(text_buffer_line_count(b) == 4 && lines_consistent(b) && !text_buffer_modified(b));
    CHECK(!text_buffer_can_undo(b));

    size_t length;
    const char *flat = text_buffer_contiguous(b, &length);
    CHECK(length == 6 && memcmp(flat, "x\ny\n\nz", 6) == 0);
    CHECK(insert_text(b, 2, "Q") == 0 && text_is(b, "x\nQy\n\nz") && lines_consistent(b));

    text_buffer_destroy(b);
    return 0;
}

/* Random inserts and erases against a plain array. */
static int check_buffer_random(void) {
    TextBuffer *b = text_buffer_create();
    CHECK(b);
    static char model[4096];
    size_t model_length = 0;
    const char alphabet[] = "ab\n cd\n";

    for (int step = 0; step < 4000; ++step) {
        if (rng(3) != 0 || model_length == 0) {
            size_t count = 1 + rng(12);
            char chunk[16];
            for (size_t i = 0; i < count; ++i) chunk[i] = alphabet[rng(sizeof(alphabet) - 1)];
            size_t position = rng(model_length + 1);
            if (model_length + count > 2000) continue;
            CHECK(text_buffer_insert(b, position, chunk, count) == 0);
            memmove(model + position + count, model + position, model_length - position);
            memcpy(model + position, chunk, count);
            model_length += count;
        } else {
            size_t position = rng(model_length);
            size_t count = 1 + rng(model_length - position < 20 ? model_length - position : 20);
            CHECK(text_buffer_erase(b, position, count) == 0);
            memmove(model + position, model + position + count, model_length - position - count);
            model_length -= count;
        }
        if (step % 50 == 0) {
            CHECK(text_buffer_length(b) == model_length);
            for (size_t i = 0; i < model_length; ++i) CHECK(text_buffer_at(b, i) == (unsigned char)model[i]);
            CHECK(lines_consistent(b));
        }
    }
    CHECK(text_buffer_length(b) == model_length && lines_consistent(b));

    /* Everything unwinds through undo and comes back through redo. */
    size_t undone = 0;
    size_t caret;
    while (text_buffer_undo(b, &caret) == 1) ++undone;
    CHECK(undone > 100);
    CHECK(text_buffer_length(b) == 0 && lines_consistent(b));
    size_t redone = 0;
    while (text_buffer_redo(b, &caret) == 1) ++redone;
    CHECK(redone == undone);
    CHECK(text_buffer_length(b) == model_length && lines_consistent(b));
    for (size_t i = 0; i < model_length; ++i) CHECK(text_buffer_at(b, i) == (unsigned char)model[i]);

    text_buffer_destroy(b);
    return 0;
}

static int check_buffer_growth(void) {
    TextBuffer *b = text_buffer_create();
    CHECK(b);
    static char block[1000];
    memset(block, 'x', sizeof(block));
    block[99] = '\n';
    for (int i = 0; i < 300; ++i) CHECK(text_buffer_insert(b, (size_t)i * 1000 / 2, block, sizeof(block)) == 0);
    CHECK(text_buffer_length(b) == 300000 && lines_consistent(b));

    char *big = malloc(200000);
    CHECK(big);
    memset(big, 'y', 200000);
    int rc = text_buffer_insert(b, 150, big, 200000);
    free(big);
    CHECK(rc == 0);
    CHECK(text_buffer_length(b) == 500000 && lines_consistent(b));
    CHECK(text_buffer_at(b, 150) == 'y' && text_buffer_at(b, 150 + 199999) == 'y');
    CHECK(text_buffer_erase(b, 0, 500000) == 0 && text_buffer_length(b) == 0);

    char *limit = malloc(TEXT_BUFFER_MAX_BYTES);
    CHECK(limit);
    memset(limit, 'z', TEXT_BUFFER_MAX_BYTES);
    CHECK(text_buffer_load(b, limit, TEXT_BUFFER_MAX_BYTES) == 0);
    CHECK(text_buffer_insert(b, 5, "q", 1) == -E2BIG);
    CHECK(text_buffer_length(b) == TEXT_BUFFER_MAX_BYTES && text_buffer_at(b, 5) == 'z');
    CHECK(text_buffer_load(b, limit, TEXT_BUFFER_MAX_BYTES + 1) == -E2BIG);
    CHECK(text_buffer_erase(b, 0, 1) == 0 && text_buffer_insert(b, 0, "q", 1) == 0);
    free(limit);

    text_buffer_destroy(b);
    return 0;
}

static int check_buffer_history(void) {
    TextBuffer *b = text_buffer_create();
    CHECK(b);
    size_t caret = 99;

    /* Typing and backspacing merge into one step each. */
    CHECK(insert_text(b, 0, "a") == 0 && insert_text(b, 1, "b") == 0 && insert_text(b, 2, "c") == 0);
    CHECK(text_buffer_erase(b, 2, 1) == 0 && text_buffer_erase(b, 1, 1) == 0);
    CHECK(text_is(b, "a"));
    CHECK(text_buffer_undo(b, &caret) == 1 && text_is(b, "abc") && caret == 3);
    CHECK(text_buffer_undo(b, &caret) == 1 && text_is(b, "") && caret == 0);
    CHECK(text_buffer_undo(b, &caret) == 0);
    CHECK(text_buffer_redo(b, &caret) == 1 && text_is(b, "abc") && caret == 3);
    CHECK(text_buffer_redo(b, &caret) == 1 && text_is(b, "a") && caret == 1);
    CHECK(text_buffer_redo(b, &caret) == 0);

    /* Sealing splits runs; a new edit forgets what could be redone. */
    text_buffer_seal(b);
    CHECK(insert_text(b, 1, "x") == 0);
    text_buffer_seal(b);
    CHECK(insert_text(b, 2, "y") == 0);
    CHECK(text_buffer_undo(b, &caret) == 1 && text_is(b, "ax"));
    CHECK(text_buffer_can_redo(b));
    CHECK(insert_text(b, 2, "z") == 0 && !text_buffer_can_redo(b));
    CHECK(text_buffer_undo(b, &caret) == 1 && text_is(b, "ax"));
    CHECK(text_buffer_undo(b, &caret) == 1 && text_is(b, "a"));

    /* Forward delete runs merge too. */
    CHECK(text_buffer_load(b, "12345", 5) == 0);
    CHECK(text_buffer_erase(b, 1, 1) == 0 && text_buffer_erase(b, 1, 1) == 0 && text_is(b, "145"));
    CHECK(text_buffer_undo(b, &caret) == 1 && text_is(b, "12345") && caret == 3);

    /* A group is one step however many calls it holds. */
    CHECK(text_buffer_load(b, "hello world", 11) == 0);
    text_buffer_group_begin(b);
    CHECK(text_buffer_erase(b, 0, 5) == 0 && insert_text(b, 0, "goodbye") == 0);
    text_buffer_group_end(b);
    CHECK(text_is(b, "goodbye world"));
    CHECK(text_buffer_undo(b, &caret) == 1 && text_is(b, "hello world") && caret == 5);
    CHECK(text_buffer_redo(b, &caret) == 1 && text_is(b, "goodbye world") && caret == 7);

    /* Modified follows the history. */
    CHECK(text_buffer_load(b, "clean", 5) == 0 && !text_buffer_modified(b));
    CHECK(insert_text(b, 5, "!") == 0 && text_buffer_modified(b));
    CHECK(text_buffer_undo(b, &caret) == 1 && !text_buffer_modified(b));
    CHECK(text_buffer_redo(b, &caret) == 1 && text_buffer_modified(b));
    text_buffer_mark_clean(b);
    CHECK(!text_buffer_modified(b));
    CHECK(insert_text(b, 6, "?") == 0 && text_buffer_modified(b));
    CHECK(text_buffer_undo(b, &caret) == 1 && !text_buffer_modified(b));
    CHECK(text_buffer_undo(b, &caret) == 1 && text_buffer_modified(b));
    CHECK(insert_text(b, 0, ">") == 0 && text_buffer_modified(b));
    CHECK(text_buffer_undo(b, &caret) == 1 && text_buffer_modified(b));

    /* The history is bounded: the oldest steps fall out. */
    CHECK(text_buffer_load(b, "", 0) == 0);
    for (int i = 0; i < 6000; ++i) {
        CHECK(insert_text(b, text_buffer_length(b), "x") == 0);
        text_buffer_seal(b);
    }
    size_t steps = 0;
    while (text_buffer_undo(b, &caret) == 1) ++steps;
    CHECK(steps >= 1000 && steps < 6000);
    CHECK(text_buffer_length(b) == 6000 - steps && text_buffer_modified(b));

    text_buffer_destroy(b);
    return 0;
}

/* ------------------------------------------------------------------ */
/* TextEdit                                                            */
/* ------------------------------------------------------------------ */

static int type(TextEdit *e, const char *text) {
    for (; *text; ++text) {
        int rc = *text == '\n' ? text_edit_newline(e) : text_edit_insert(e, text, 1);
        if (rc != 0) return rc;
    }
    return 0;
}

static int check_edit_typing(void) {
    TextEdit *e = text_edit_create();
    CHECK(e);

    CHECK(type(e, "hello") == 0 && edit_is(e, "hello") && e->caret == 5);
    CHECK(text_edit_backspace(e) == 0 && edit_is(e, "hell") && e->caret == 4);
    text_edit_move(e, TEXT_MOVE_LEFT, 0, 0);
    CHECK(text_edit_delete(e) == 0 && edit_is(e, "hel") && e->caret == 3);
    CHECK(text_edit_delete(e) == 0 && edit_is(e, "hel"));
    text_edit_move(e, TEXT_MOVE_TEXT_START, 0, 0);
    CHECK(text_edit_backspace(e) == 0 && edit_is(e, "hel") && e->caret == 0);

    /* Backspace and Delete join lines. */
    CHECK(text_edit_load(e, "ab\ncd\nef", 8) == 0);
    text_edit_set_caret(e, 3, 0);
    CHECK(text_edit_backspace(e) == 0 && edit_is(e, "abcd\nef") && e->caret == 2);
    text_edit_set_caret(e, 4, 0);
    CHECK(text_edit_delete(e) == 0 && edit_is(e, "abcdef") && text_buffer_line_count(e->buffer) == 1);

    /* Tab goes to the next stop. */
    CHECK(text_edit_load(e, "", 0) == 0);
    CHECK(text_edit_tab(e) == 0 && edit_is(e, "    "));
    CHECK(type(e, "ab") == 0 && text_edit_tab(e) == 0 && edit_is(e, "    ab  ") && e->caret == 8);
    CHECK(text_edit_column_of(e, 8) == 8);

    /* Overwrite replaces the character under the caret, not a newline. */
    CHECK(text_edit_load(e, "abc\nd", 5) == 0);
    e->overwrite = 1;
    CHECK(text_edit_insert(e, "X", 1) == 0 && edit_is(e, "Xbc\nd") && e->caret == 1);
    text_edit_move(e, TEXT_MOVE_LINE_END, 0, 0);
    CHECK(text_edit_insert(e, "Y", 1) == 0 && edit_is(e, "XbcY\nd"));
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "Xbc\nd"));
    e->overwrite = 0;

    text_edit_destroy(e);
    return 0;
}

static int check_edit_newline(void) {
    TextEdit *e = text_edit_create();
    CHECK(e);

    CHECK(type(e, "    foo\nbar") == 0);
    CHECK(edit_is(e, "    foo\n    bar") && e->caret == 15);
    CHECK(text_edit_undo(e) == 1);

    CHECK(text_edit_load(e, "if (x) {", 8) == 0);
    text_edit_move(e, TEXT_MOVE_TEXT_END, 0, 0);
    CHECK(text_edit_newline(e) == 0 && edit_is(e, "if (x) {\n    ") && e->caret == 13);

    CHECK(text_edit_load(e, "  {}", 4) == 0);
    text_edit_set_caret(e, 3, 0);
    CHECK(text_edit_newline(e) == 0 && edit_is(e, "  {\n      \n  }"));
    CHECK(e->caret == 3 + 1 + 6);
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "  {}") && e->caret == 3);

    /* A selection is replaced by the line break. */
    CHECK(text_edit_load(e, "a bc d", 6) == 0);
    text_edit_set_caret(e, 2, 0);
    text_edit_set_caret(e, 4, 1);
    CHECK(text_edit_newline(e) == 0 && edit_is(e, "a \n d") && !text_edit_has_selection(e));

    /* A closing brace on a blank line dedents; elsewhere it is plain text. */
    CHECK(text_edit_load(e, "", 0) == 0);
    CHECK(type(e, "if (x) {\n}") == 0 && edit_is(e, "if (x) {\n}") && e->caret == 10);
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "if (x) {\n    ") && e->caret == 13);
    CHECK(text_edit_load(e, "a }", 3) == 0);
    text_edit_move(e, TEXT_MOVE_TEXT_END, 0, 0);
    CHECK(type(e, "}") == 0 && edit_is(e, "a }}"));

    /* Only the indentation before the caret is inherited. */
    CHECK(text_edit_load(e, "    x", 5) == 0);
    text_edit_set_caret(e, 2, 0);
    CHECK(text_edit_newline(e) == 0 && edit_is(e, "  \n  " "  x") && e->caret == 5);

    text_edit_destroy(e);
    return 0;
}

static int check_edit_movement(void) {
    TextEdit *e = text_edit_create();
    CHECK(e);
    CHECK(text_edit_load(e, "alpha beta\n\n  gamma_1, delta\nlast", 33) == 0);

    /* Vertical moves keep their column over short lines. */
    text_edit_set_caret(e, 8, 0);
    text_edit_move(e, TEXT_MOVE_DOWN, 0, 0);
    CHECK(e->caret == 11);
    text_edit_move(e, TEXT_MOVE_DOWN, 0, 0);
    CHECK(e->caret == 12 + 8);
    text_edit_move(e, TEXT_MOVE_UP, 0, 0);
    text_edit_move(e, TEXT_MOVE_UP, 0, 0);
    CHECK(e->caret == 8);
    text_edit_move(e, TEXT_MOVE_UP, 0, 0);
    CHECK(e->caret == 0);
    text_edit_move(e, TEXT_MOVE_DOWN, 0, 0);
    text_edit_move(e, TEXT_MOVE_DOWN, 0, 0);
    text_edit_move(e, TEXT_MOVE_DOWN, 0, 0);
    text_edit_move(e, TEXT_MOVE_DOWN, 0, 0);
    CHECK(e->caret == text_buffer_length(e->buffer));

    /* Home goes to the first non-blank, then to the line start. */
    text_edit_set_caret(e, 20, 0);
    text_edit_move(e, TEXT_MOVE_LINE_START, 0, 0);
    CHECK(e->caret == 14);
    text_edit_move(e, TEXT_MOVE_LINE_START, 0, 0);
    CHECK(e->caret == 12);
    text_edit_move(e, TEXT_MOVE_LINE_START, 0, 0);
    CHECK(e->caret == 14);
    text_edit_move(e, TEXT_MOVE_LINE_END, 0, 0);
    CHECK(e->caret == 28);

    /* Words: runs of letters, of punctuation, newlines count as one stop. */
    text_edit_set_caret(e, 0, 0);
    text_edit_move(e, TEXT_MOVE_WORD_RIGHT, 0, 0);
    CHECK(e->caret == 6);
    text_edit_move(e, TEXT_MOVE_WORD_RIGHT, 0, 0);
    CHECK(e->caret == 10);
    text_edit_move(e, TEXT_MOVE_WORD_RIGHT, 0, 0);
    CHECK(e->caret == 11);
    text_edit_move(e, TEXT_MOVE_WORD_RIGHT, 0, 0);
    CHECK(e->caret == 14);
    text_edit_move(e, TEXT_MOVE_WORD_RIGHT, 0, 0);
    CHECK(e->caret == 21);
    text_edit_move(e, TEXT_MOVE_WORD_LEFT, 0, 0);
    CHECK(e->caret == 14);
    text_edit_move(e, TEXT_MOVE_WORD_LEFT, 0, 0);
    CHECK(e->caret == 11);
    text_edit_set_caret(e, 0, 0);
    text_edit_move(e, TEXT_MOVE_WORD_LEFT, 0, 0);
    CHECK(e->caret == 0);

    /* Extending keeps the anchor; plain moves collapse towards an edge. */
    text_edit_set_caret(e, 3, 0);
    text_edit_move(e, TEXT_MOVE_RIGHT, 1, 0);
    text_edit_move(e, TEXT_MOVE_RIGHT, 1, 0);
    size_t start, end;
    text_edit_selection(e, &start, &end);
    CHECK(start == 3 && end == 5 && e->anchor == 3 && e->caret == 5);
    text_edit_move(e, TEXT_MOVE_LEFT, 0, 0);
    CHECK(!text_edit_has_selection(e) && e->caret == 3);
    text_edit_move(e, TEXT_MOVE_LEFT, 1, 0);
    text_edit_move(e, TEXT_MOVE_RIGHT, 0, 0);
    CHECK(!text_edit_has_selection(e) && e->caret == 3);

    /* Pages. */
    text_edit_set_caret(e, 0, 0);
    text_edit_move(e, TEXT_MOVE_PAGE_DOWN, 0, 2);
    CHECK(text_buffer_line_of(e->buffer, e->caret) == 2);
    text_edit_move(e, TEXT_MOVE_PAGE_DOWN, 0, 10);
    CHECK(text_buffer_line_of(e->buffer, e->caret) == 3 && e->caret == 29);
    text_edit_move(e, TEXT_MOVE_PAGE_UP, 0, 3);
    CHECK(text_buffer_line_of(e->buffer, e->caret) == 0);

    /* Columns count tabs to the next stop. */
    CHECK(text_edit_load(e, "\tab\tc", 5) == 0);
    CHECK(text_edit_column_of(e, 1) == 4 && text_edit_column_of(e, 3) == 6 && text_edit_column_of(e, 4) == 8);
    CHECK(text_edit_position_at(e, 0, 0) == 0 && text_edit_position_at(e, 0, 4) == 1);
    CHECK(text_edit_position_at(e, 0, 1) == 0 && text_edit_position_at(e, 0, 3) == 1);
    CHECK(text_edit_position_at(e, 0, 7) == 4 && text_edit_position_at(e, 0, 99) == 5);
    CHECK(text_edit_position_at(e, 9, 0) == 0);

    text_edit_destroy(e);
    return 0;
}

static int check_edit_selection(void) {
    TextEdit *e = text_edit_create();
    CHECK(e);
    CHECK(text_edit_load(e, "one two three", 13) == 0);

    text_edit_select_word(e, 5);
    CHECK(e->anchor == 4 && e->caret == 7);
    text_edit_select_word(e, 2);
    CHECK(e->anchor == 0 && e->caret == 3);
    text_edit_select_word(e, 3);
    CHECK(e->anchor == 3 && e->caret == 4);
    text_edit_select_word(e, 13);
    CHECK(e->anchor == 8 && e->caret == 13);
    text_edit_select_all(e);
    CHECK(e->anchor == 0 && e->caret == 13);

    /* Typing over a selection is one undo step. */
    text_edit_set_caret(e, 4, 0);
    text_edit_set_caret(e, 7, 1);
    CHECK(text_edit_insert(e, "2", 1) == 0 && edit_is(e, "one 2 three") && e->caret == 5);
    CHECK(!text_edit_has_selection(e));
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "one two three") && e->caret == 7);
    CHECK(text_edit_redo(e) == 1 && edit_is(e, "one 2 three"));
    CHECK(text_edit_undo(e) == 1);

    /* Backspace and Delete remove a selection, not a character. */
    text_edit_set_caret(e, 4, 0);
    text_edit_set_caret(e, 8, 1);
    CHECK(text_edit_backspace(e) == 0 && edit_is(e, "one three") && e->caret == 4);
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "one two three"));
    text_edit_set_caret(e, 8, 0);
    text_edit_set_caret(e, 4, 1);
    CHECK(text_edit_delete(e) == 0 && edit_is(e, "one three") && e->caret == 4);

    /* A backwards selection behaves like a forwards one. */
    CHECK(text_edit_load(e, "abcdef", 6) == 0);
    text_edit_set_caret(e, 5, 0);
    text_edit_set_caret(e, 2, 1);
    size_t start, end;
    text_edit_selection(e, &start, &end);
    CHECK(start == 2 && end == 5);

    text_edit_select_line(e, 3);
    CHECK(e->anchor == 0 && e->caret == 6);
    CHECK(text_edit_load(e, "ab\ncd\n", 6) == 0);
    text_edit_select_line(e, 4);
    CHECK(e->anchor == 3 && e->caret == 6);
    text_edit_select_line(e, 6);
    CHECK(e->anchor == 6 && e->caret == 6);

    text_edit_destroy(e);
    return 0;
}

static int check_edit_clipboard(void) {
    TextEdit *e = text_edit_create();
    TextEdit *other = text_edit_create();
    CHECK(e && other);
    CHECK(clipboard_set("", 0) == 0);
    CHECK(text_edit_load(e, "copy me, paste me", 17) == 0);

    CHECK(text_edit_copy(e) == 0);
    size_t length;
    clipboard_data(&length);
    CHECK(length == 0);

    text_edit_set_caret(e, 0, 0);
    text_edit_set_caret(e, 7, 1);
    CHECK(text_edit_copy(e) == 0 && edit_is(e, "copy me, paste me"));
    const char *clip = clipboard_data(&length);
    CHECK(length == 7 && memcmp(clip, "copy me", 7) == 0);

    text_edit_set_caret(e, 17, 0);
    CHECK(text_edit_paste(e) == 0 && edit_is(e, "copy me, paste mecopy me") && e->caret == 24);
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "copy me, paste me"));

    /* The clipboard is shared between editors. */
    CHECK(text_edit_paste(other) == 0 && edit_is(other, "copy me"));

    text_edit_set_caret(e, 0, 0);
    text_edit_set_caret(e, 9, 1);
    CHECK(text_edit_cut(e) == 0 && edit_is(e, "paste me") && e->caret == 0);
    clip = clipboard_data(&length);
    CHECK(length == 9 && memcmp(clip, "copy me, ", 9) == 0);
    CHECK(text_edit_paste(e) == 0 && edit_is(e, "copy me, paste me") && e->caret == 9);

    /* Paste replaces the selection. */
    text_edit_select_all(e);
    CHECK(text_edit_paste(e) == 0 && edit_is(e, "copy me, "));
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "copy me, paste me"));

    text_edit_destroy(other);
    text_edit_destroy(e);
    return 0;
}

static int check_edit_find(void) {
    TextEdit *e = text_edit_create();
    CHECK(e);
    CHECK(text_edit_load(e, "Foo foo FOO bar", 15) == 0);

    CHECK(text_edit_find(e, "foo", 3, 0, 1) == 0);
    CHECK(text_edit_find(e, "foo", 3, 1, 1) == 4);
    CHECK(text_edit_find(e, "foo", 3, 5, 1) == 8);
    CHECK(text_edit_find(e, "foo", 3, 9, 1) == 0);
    CHECK(text_edit_find(e, "foo", 3, 0, 0) == 8);
    CHECK(text_edit_find(e, "foo", 3, 8, 0) == 4);
    CHECK(text_edit_find(e, "foo", 3, 4, 0) == 0);
    CHECK(text_edit_find(e, "Foo", 3, 1, 1) == 0);
    CHECK(text_edit_find(e, "FOO", 3, 0, 1) == 8);
    CHECK(text_edit_find(e, "bar", 3, 12, 1) == 12);
    CHECK(text_edit_find(e, "bar", 3, 13, 1) == 12);
    CHECK(text_edit_find(e, "baz", 3, 0, 1) == -1);
    CHECK(text_edit_find(e, "", 0, 0, 1) == -1);
    CHECK(text_edit_find(e, "Foo foo FOO bar!", 16, 0, 1) == -1);
    CHECK(text_edit_find(e, "Foo foo FOO bar", 15, 15, 1) == 0);

    text_edit_destroy(e);
    return 0;
}

static int check_edit_history(void) {
    TextEdit *e = text_edit_create();
    CHECK(e);

    CHECK(type(e, "one two") == 0);
    text_edit_move(e, TEXT_MOVE_LEFT, 0, 0);
    CHECK(type(e, "XY") == 0 && edit_is(e, "one twXYo"));
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "one two") && e->caret == 6);
    CHECK(text_edit_undo(e) == 1 && edit_is(e, "") && e->caret == 0);
    CHECK(text_edit_undo(e) == 0);
    CHECK(text_edit_redo(e) == 1 && edit_is(e, "one two") && e->caret == 7);
    CHECK(text_edit_redo(e) == 1 && edit_is(e, "one twXYo") && e->caret == 8);
    CHECK(text_edit_redo(e) == 0);
    CHECK(!text_edit_has_selection(e));
    CHECK(text_buffer_modified(e->buffer));

    text_edit_destroy(e);
    return 0;
}

int main(void) {
    if (check_buffer_basics() || check_buffer_lines() || check_buffer_random() ||
        check_buffer_growth() || check_buffer_history() || check_edit_typing() ||
        check_edit_newline() || check_edit_movement() || check_edit_selection() ||
        check_edit_clipboard() || check_edit_find() || check_edit_history())
        return 1;
    printf("EDITOR PASS\n");
    return 0;
}
