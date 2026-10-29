#include "theme.h"

static Theme *theme_cached_default(void) {
    static Theme *instance = 0;
    if (!instance) {
        instance = (Theme *)malloc(sizeof(Theme));
        if (instance) {
            memset(instance, 0, sizeof(Theme));

            instance->primary = 0x3B82F6u;
            instance->primary_color = instance->primary;
            instance->color_primary = instance->primary;

            instance->secondary = 0x64748Bu;
            instance->secondary_color = instance->secondary;
            instance->color_secondary = instance->secondary;

            instance->bg = 0x0F172Au;
            instance->background = instance->bg;
            instance->bg_color = instance->bg;
            instance->background_color = instance->bg;
            instance->color_bg = instance->bg;

            instance->fg = 0xF8FAFCu;
            instance->foreground = instance->fg;
            instance->fg_color = instance->fg;
            instance->foreground_color = instance->fg;
            instance->text_color = instance->fg;
            instance->color_fg = instance->fg;

            instance->border = 0x334155u;
            instance->border_color = instance->border;
            instance->color_border = instance->border;

            instance->accent = 0xF59E0Bu;
            instance->accent_color = instance->accent;
            instance->color_accent = instance->accent;

            instance->spacing = 8;
            instance->spacing_scale = 8;
            instance->spacing_unit = 8;
            instance->space = 8;
            instance->gap = 8;
            instance->spacing_xs = 2;
            instance->spacing_sm = 4;
            instance->spacing_md = 8;
            instance->spacing_lg = 16;
            instance->spacing_xl = 32;
            instance->spacing_small = 4;
            instance->spacing_medium = 8;
            instance->spacing_large = 16;
            instance->spacings[0] = 2;
            instance->spacings[1] = 4;
            instance->spacings[2] = 8;
            instance->spacings[3] = 16;
            instance->spacings[4] = 32;
            instance->spacings[5] = 64;

            instance->spacing_scale_struct.xs = 2;
            instance->spacing_scale_struct.sm = 4;
            instance->spacing_scale_struct.md = 8;
            instance->spacing_scale_struct.lg = 16;
            instance->spacing_scale_struct.xl = 32;
            instance->spacing_tokens = instance->spacing_scale_struct;
            instance->spacing_values = instance->spacing_scale_struct;

            instance->font = 16;
            instance->font_token = 16;
            instance->font_size = 16;
            instance->font_id = 1;

            instance->font_token_struct.size = 16;
            instance->font_token_struct.token = 16;
            instance->font_token_struct.id = 1;
            instance->font_info = instance->font_token_struct;
        }
    }
    return instance;
}

Theme *theme_default(void) {
    return theme_cached_default();
}

static void theme_apply_label(Theme *theme, Label *label) {
    if (!theme || !label) return;
    label->color = theme->fg;
    label->fg = theme->fg;
    label->fg_color = theme->fg;
    label->foreground = theme->fg;
    label->foreground_color = theme->fg;
    label->text_color = theme->fg;
}

static void theme_apply_button(Theme *theme, Button *button) {
    if (!theme || !button) return;
    button->bg_color = theme->primary;
    button->background = theme->primary;
    button->background_color = theme->primary;

    button->color = theme->fg;
    button->fg = theme->fg;
    button->fg_color = theme->fg;
}

static void theme_apply_textinput(Theme *theme, TextInput *input) {
    if (!theme || !input) return;
    input->bg_color = theme->secondary;
    input->background = theme->secondary;
    input->background_color = theme->secondary;

    input->color = theme->fg;
    input->fg = theme->fg;
    input->fg_color = theme->fg;
    input->text_color = theme->fg;
}

static void theme_apply_container(Theme *theme, Container *container);

static void theme_apply_widget_node(Theme *theme, Widget *widget) {
    if (!theme || !widget) return;
    switch (widget->type) {
        case WIDGET_LABEL:
            theme_apply_label(theme, (Label *)widget);
            break;
        case WIDGET_BUTTON:
            theme_apply_button(theme, (Button *)widget);
            break;
        case WIDGET_TEXTINPUT:
            theme_apply_textinput(theme, (TextInput *)widget);
            break;
        case WIDGET_CONTAINER:
            theme_apply_container(theme, (Container *)widget);
            break;
        default:
            break;
    }
}

static void theme_apply_container(Theme *theme, Container *container) {
    if (!theme || !container) return;
    container->bg_color = theme->bg;
    container->background = theme->bg;
    container->background_color = theme->bg;

    if (container->children) {
        for (int i = 0; i < container->count; ++i) {
            theme_apply_widget_node(theme, (Widget *)container->children[i]);
        }
    }
}

int theme_apply_theme(Theme *theme, void *widget) {
    theme_apply_widget_node(theme, (Widget *)widget);
    return 0;
}

int theme_apply_widget(void *widget) {
    return theme_apply_theme(theme_cached_default(), widget);
}

int theme_apply_to(Theme *theme, void *widget) {
    return theme_apply_theme(theme, widget);
}

int theme_apply_to_widget(Theme *theme, void *widget) {
    return theme_apply_theme(theme, widget);
}