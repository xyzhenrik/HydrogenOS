#pragma once

#include "mini_libc.h"
#include "widgets.h"

#include <stdint.h>
#include <stddef.h>

/* Spacing scale container (kept as a plain struct for convenient access). */
typedef struct ThemeSpacing {
    int xs;
    int sm;
    int md;
    int lg;
    int xl;
} ThemeSpacing;

/* Font token container. */
typedef struct ThemeFont {
    int size;
    int token;
    int id;
} ThemeFont;

typedef struct Theme {
    /* Color tokens. */
    uint32_t primary;
    uint32_t primary_color;
    uint32_t color_primary;

    uint32_t secondary;
    uint32_t secondary_color;
    uint32_t color_secondary;

    uint32_t bg;
    uint32_t background;
    uint32_t bg_color;
    uint32_t background_color;
    uint32_t color_bg;

    uint32_t fg;
    uint32_t foreground;
    uint32_t fg_color;
    uint32_t foreground_color;
    uint32_t text_color;
    uint32_t color_fg;

    uint32_t border;
    uint32_t border_color;
    uint32_t color_border;

    uint32_t accent;
    uint32_t accent_color;
    uint32_t color_accent;

    /* Spacing scale. */
    int spacing;
    int spacing_scale;
    int spacing_unit;
    int space;
    int gap;
    int spacing_xs;
    int spacing_sm;
    int spacing_md;
    int spacing_lg;
    int spacing_xl;
    int spacing_small;
    int spacing_medium;
    int spacing_large;
    int spacings[6];

    ThemeSpacing spacing_scale_struct;
    ThemeSpacing spacing_tokens;
    ThemeSpacing spacing_values;

    /* Font token. */
    int font;
    int font_token;
    int font_size;
    int font_id;

    ThemeFont font_token_struct;
    ThemeFont font_info;
} Theme;

typedef Theme theme_t;
typedef Theme Theme_t;

Theme *theme_default(void);
int theme_apply_widget(void *widget);
int theme_apply_theme(Theme *theme, void *widget);
int theme_apply_to(Theme *theme, void *widget);
int theme_apply_to_widget(Theme *theme, void *widget);

/* Allow theme_apply() with either one widget or (theme, widget). */
#define _THEME_APPLY_GET_MACRO(_1, _2, NAME, ...) NAME
#define theme_apply(...) _THEME_APPLY_GET_MACRO(__VA_ARGS__, theme_apply_theme, theme_apply_widget)(__VA_ARGS__)