#pragma once

#include "mini_libc.h"
#include <stdint.h>
#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);

typedef struct Surface {
    uint32_t *pixels;
    uint32_t *buffer;
    uint32_t *data;
    int width;
    int height;
    int stride;
    int pitch;
    int w;
    int h;
} Surface;
typedef Surface surface_t;
typedef Surface Surface_t;

Surface *surface_create(int width, int height);
Surface *surface_new(int width, int height);
void surface_destroy(Surface *surface);
void surface_free(Surface *surface);
void surface_clear(Surface *surface, uint32_t color);
void surface_put_pixel(Surface *surface, int x, int y, uint32_t color);
void surface_fill_rect(Surface *surface, int x, int y, int w, int h, uint32_t color);
void surface_draw_rect(Surface *surface, int x, int y, int w, int h, uint32_t color);
void surface_draw_text(Surface *surface, int x, int y, const char *text, uint32_t color);
int  surface_text_width(const char *text);
int  surface_text_height(void);

#define WIDGET_LABEL      0
#define WIDGET_BUTTON     1
#define WIDGET_TEXTINPUT  2
#define WIDGET_CONTAINER  3
#define WIDGET_TEXT_INPUT WIDGET_TEXTINPUT

typedef struct Widget {
    int type;
    int x;
    int y;
    int w;
    int h;
    int width;
    int height;
    int visible;
} Widget;
typedef Widget widget_t;
typedef Widget Widget_t;

typedef struct Label {
    int type;
    int x;
    int y;
    int w;
    int h;
    int width;
    int height;
    int visible;

    char *text;
    char *string;
    char *str;
    char *caption;

    uint32_t color;
    uint32_t fg;
    uint32_t fg_color;
    uint32_t foreground;
    uint32_t foreground_color;
    uint32_t text_color;
} Label;
typedef Label label_t;
typedef Label Label_t;

Label *label_create(const char *text, uint32_t color);
Label *label_new(const char *text, uint32_t color);
void label_set_text(Label *label, const char *text);
void label_set_color(Label *label, uint32_t color);
void label_set_pos(Label *label, int x, int y);
void label_render(Label *label, Surface *surface);
void label_draw(Label *label, Surface *surface);
void label_paint(Label *label, Surface *surface);
void render_label(Label *label, Surface *surface);
void draw_label(Label *label, Surface *surface);
void paint_label(Label *label, Surface *surface);

typedef struct Button {
    int type;
    int x;
    int y;
    int w;
    int h;
    int width;
    int height;
    int visible;

    char *label;
    char *text;
    char *caption;
    char *title;

    uint32_t color;
    uint32_t bg_color;
    uint32_t background;
    uint32_t background_color;
    uint32_t fg;
    uint32_t fg_color;

    void (*on_click)();
    void *userdata;
    void *user_data;
    void *context;

    int hovered;
    int pressed;
    int corner_radius;
} Button;
typedef Button button_t;
typedef Button Button_t;

Button *button_create_impl(const char *label, void (*on_click)());
Button *button_new_impl(const char *label, void (*on_click)());
void button_set_label(Button *button, const char *label);
void button_set_on_click_impl(Button *button, void (*on_click)());
void button_click(Button *button);
void button_render(Button *button, Surface *surface);
void button_draw(Button *button, Surface *surface);
void button_paint(Button *button, Surface *surface);
void render_button(Button *button, Surface *surface);
void draw_button(Button *button, Surface *surface);
void paint_button(Button *button, Surface *surface);

typedef struct TextInput {
    int type;
    int x;
    int y;
    int w;
    int h;
    int width;
    int height;
    int visible;

    char *buffer;
    char *text;
    char *str;
    char *value;

    size_t capacity;
    size_t size;
    size_t len;
    size_t length;

    int cursor;
    int cursor_pos;
    int pos;
    int caret;

    uint32_t color;
    uint32_t text_color;
    uint32_t fg;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t background;
    uint32_t background_color;

    int focused;
} TextInput;
typedef TextInput textinput_t;
typedef TextInput text_input_t;
typedef TextInput TextInput_t;

TextInput *textinput_create(size_t capacity);
TextInput *text_input_create(size_t capacity);
TextInput *textinput_new(size_t capacity);
void textinput_set_text(TextInput *input, const char *text);
void textinput_insert_char(TextInput *input, char c);
void textinput_backspace(TextInput *input);
void textinput_delete(TextInput *input);
void textinput_move_left(TextInput *input);
void textinput_move_right(TextInput *input);
void textinput_move_home(TextInput *input);
void textinput_move_end(TextInput *input);
void textinput_render(TextInput *input, Surface *surface);
void textinput_draw(TextInput *input, Surface *surface);
void textinput_paint(TextInput *input, Surface *surface);
void render_textinput(TextInput *input, Surface *surface);
void render_text_input(TextInput *input, Surface *surface);
void draw_textinput(TextInput *input, Surface *surface);
void draw_text_input(TextInput *input, Surface *surface);
void paint_textinput(TextInput *input, Surface *surface);
void paint_text_input(TextInput *input, Surface *surface);

typedef struct Container {
    int type;
    int x;
    int y;
    int w;
    int h;
    int width;
    int height;
    int visible;

    void **children;
    int count;
    int child_count;
    int num_children;
    int n;
    int nchildren;
    int capacity;
    int cap;
    int size;

    uint32_t bg_color;
    uint32_t background;
    uint32_t background_color;
} Container;
typedef Container container_t;
typedef Container Container_t;

Container *container_create(void);
Container *container_new(void);
void container_add(Container *container, void *child);
void container_add_child(Container *container, void *child);
void container_append(Container *container, void *child);
void container_remove(Container *container, void *child);
void container_clear(Container *container);
void container_render(Container *container, Surface *surface);
void container_draw(Container *container, Surface *surface);
void container_paint(Container *container, Surface *surface);
void render_container(Container *container, Surface *surface);
void draw_container(Container *container, Surface *surface);
void paint_container(Container *container, Surface *surface);

Widget *widget_create(int type, int x, int y, int w, int h);
Widget *widget_create_label(const char *text, uint32_t color);
Widget *widget_create_button(const char *label, void (*on_click)());
Widget *widget_create_textinput(size_t capacity);
Widget *widget_create_container(void);

void widget_set_pos(Widget *widget, int x, int y);
void widget_set_size(Widget *widget, int w, int h);
void widget_render(Widget *widget, Surface *surface);
void widget_draw(Widget *widget, Surface *surface);
void widget_paint(Widget *widget, Surface *surface);
void render_widget(Widget *widget, Surface *surface);
void draw_widget(Widget *widget, Surface *surface);
void paint_widget(Widget *widget, Surface *surface);

/* macro aliases */
#define surface_free surface_destroy
#define destroy_surface surface_destroy
#define free_surface surface_destroy
#define create_surface surface_create
#define new_surface surface_create
#define surface_rect surface_fill_rect

#define label_create_text label_create
#define create_label label_create
#define new_label label_create
#define make_label label_create

#define create_button button_create
#define new_button button_create
#define make_button button_create

#define create_text_input text_input_create
#define new_text_input text_input_create
#define make_text_input text_input_create
#define text_input_insert_char textinput_insert_char
#define text_input_backspace textinput_backspace
#define text_input_render textinput_render

#define create_container container_create
#define new_container container_create
#define make_container container_create
#define container_add_widget container_add

#define create_widget widget_create

#define button_create(label, cb) button_create_impl(label, (void (*)())(cb))
#define button_new(label, cb) button_new_impl(label, (void (*)())(cb))
#define button_set_on_click(btn, cb) button_set_on_click_impl(btn, (void (*)())(cb))
