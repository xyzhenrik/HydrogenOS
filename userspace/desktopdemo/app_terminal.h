#pragma once

#include "compositor.h"
#include "settings.h"
#include "terminal.h"

/* The Terminal app: a window onto a Terminal whose shell runs behind pipes.
 * The window keeps the grid in step with its size, so resizing a window
 * reflows the text. */

typedef struct AppTerminalTheme {
    uint32_t background;
    uint32_t text;
    /* Indexed by TerminalCell.color; entry 0 is the default text colour. */
    uint32_t colors[TERMINAL_COLORS];
} AppTerminalTheme;

void app_terminal_theme(const SettingsPalette *palette, AppTerminalTheme *theme);

/* Creates the terminal sized to the window below its title bar and starts the
 * shell. Returns null when the terminal cannot be allocated; a shell that
 * fails to start is reported in the window itself. */
Terminal *app_terminal_open(const Window *window, int title_height);

/* Draws the text area (everything below the title bar). The caller paints the
 * background. */
void app_terminal_draw(Terminal *terminal, Window *window, int title_height,
                       int focused, const AppTerminalTheme *theme);
