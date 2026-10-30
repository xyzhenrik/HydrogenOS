#include "app_terminal.h"

#include "mini_libc.h"

/* The glyphs are 8x16; the text area keeps a 12px margin at the sides and 8px
 * above and below, so the first cell sits on the same grid as the window
 * title. */
#define CELL_W 8
#define CELL_H 16
#define MARGIN_X 12
#define MARGIN_Y 8

void app_terminal_theme(const SettingsPalette *palette, AppTerminalTheme *theme) {
    static const uint32_t fixed_colors[TERMINAL_COLORS] = {
        0,
        0x566270u, 0xFF6B6Bu, 0, 0xE5C07Bu, 0x61AFEFu, 0xC678DDu, 0x56B6C2u, 0,
        0, 0xFF8E8Eu, 0x8AE08Au, 0xF0D58Eu, 0x7DC2FFu, 0xD99BF0u, 0x6FD0DCu, 0xFFFFFFu,
    };
    theme->background = palette->terminal_bg;
    theme->text = palette->terminal_text;
    for (int i = 0; i < TERMINAL_COLORS; ++i)
        theme->colors[i] = fixed_colors[i];
    theme->colors[0] = palette->terminal_text;
    theme->colors[3] = palette->terminal_prompt;
    theme->colors[8] = palette->terminal_text;
    theme->colors[9] = palette->terminal_dim;
}

static void grid_size(const Window *window, int title_height, int *cols, int *rows) {
    int width = (window->w - 2 * MARGIN_X) / CELL_W;
    int height = (window->h - title_height - 2 * MARGIN_Y) / CELL_H;
    *cols = width < 1 ? 1 : width;
    *rows = height < 1 ? 1 : height;
}

Terminal *app_terminal_open(const Window *window, int title_height) {
    int cols;
    int rows;
    Terminal *terminal;
    grid_size(window, title_height, &cols, &rows);
    terminal = terminal_create(cols, rows);
    if (terminal)
        terminal_start(terminal);
    return terminal;
}

static void fill_block(Window *window, int x, int y, int w, int h, uint32_t color) {
    uint32_t *pixels = (uint32_t *)window->surface;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > window->w) w = window->w - x;
    if (y + h > window->h) h = window->h - y;
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            pixels[row * window->w + col] = color;
}

/* Text is drawn in runs of one colour, so a line costs a handful of calls. */
static void draw_row(Window *window, const TerminalFrame *frame, int row, int y,
                     const AppTerminalTheme *theme) {
    const TerminalCell *cells = &frame->cells[row * frame->cols];
    char run[TERMINAL_COLS_MAX + 1];
    int col = 0;
    while (col < frame->cols) {
        int start = col;
        int length = 0;
        int blank = 1;
        uint8_t color = cells[col].color;
        while (col < frame->cols && cells[col].color == color) {
            run[length++] = (char)cells[col].ch;
            if (cells[col].ch != ' ')
                blank = 0;
            col++;
        }
        run[length] = 0;
        if (!blank)
            comp_text_render(window, MARGIN_X + start * CELL_W, y, run,
                             theme->colors[color < TERMINAL_COLORS ? color : 0]);
    }
}

/* A solid block while the window has the keyboard, an outline otherwise. */
static void draw_cursor(Window *window, const TerminalFrame *frame, int title_height,
                        int focused, const AppTerminalTheme *theme) {
    int x = MARGIN_X + frame->cursor_col * CELL_W;
    int y = title_height + MARGIN_Y + frame->cursor_row * CELL_H;
    if (focused) {
        char glyph[2] = { (char)frame->cells[frame->cursor_row * frame->cols +
                                             frame->cursor_col].ch, 0 };
        fill_block(window, x, y, CELL_W, CELL_H, theme->text);
        if (glyph[0] != ' ')
            comp_text_render(window, x, y, glyph, theme->background);
        return;
    }
    fill_block(window, x, y, CELL_W, 1, theme->text);
    fill_block(window, x, y + CELL_H - 1, CELL_W, 1, theme->text);
    fill_block(window, x, y, 1, CELL_H, theme->text);
    fill_block(window, x + CELL_W - 1, y, 1, CELL_H, theme->text);
}

void app_terminal_draw(Terminal *terminal, Window *window, int title_height,
                       int focused, const AppTerminalTheme *theme) {
    const TerminalFrame *frame;
    int cols;
    int rows;
    if (!terminal || !window->surface)
        return;
    grid_size(window, title_height, &cols, &rows);
    terminal_resize(terminal, cols, rows);
    frame = terminal_render(terminal);
    for (int row = 0; row < frame->rows; ++row)
        draw_row(window, frame, row, title_height + MARGIN_Y + row * CELL_H, theme);
    if (frame->cursor_row >= 0)
        draw_cursor(window, frame, title_height, focused, theme);
}
