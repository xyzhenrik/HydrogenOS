#pragma once

#include "mini_libc.h"
#include "widgets.h"

/* Direction values */
#define LAYOUT_HORIZONTAL 0
#define LAYOUT_VERTICAL 1
#define LAYOUT_DIR_HORIZONTAL 0
#define LAYOUT_DIR_VERTICAL 1
#define HORIZONTAL 0
#define VERTICAL 1
#define DIRECTION_HORIZONTAL 0
#define DIRECTION_VERTICAL 1
#define FLEX_HORIZONTAL 0
#define FLEX_VERTICAL 1
#define LAYOUT_ROW 0
#define LAYOUT_COLUMN 1

typedef struct LayoutItem LayoutItem;
typedef struct LayoutContainer LayoutContainer;

struct LayoutItem {
    int type;
    int x;
    int y;
    int w;
    int h;
    int width;
    int height;
    int visible;
    int grow;
    int fixed;
    int basis;
    int size;
};

struct LayoutContainer {
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
    int capacity;
    int cap;
    int size;

    uint32_t bg_color;
    uint32_t background;
    uint32_t background_color;

    int direction;
    int padding;
    int spacing;

    int grow[256];
    int fixed[256];
    int basis[256];
};

/* Type aliases */
typedef LayoutItem LayoutChild;
typedef LayoutItem FlexItem;
typedef LayoutItem LayoutBox;
typedef LayoutItem LayoutNode;
typedef LayoutItem Item;
typedef LayoutItem Box;

typedef LayoutContainer Layout;
typedef LayoutContainer FlexContainer;
typedef LayoutContainer FlexLayout;
typedef LayoutContainer ContainerLayout;
typedef LayoutContainer LayoutRoot;
typedef LayoutContainer LayoutBoxContainer;

/* Field aliases (container) */
#define child_count count
#define num_children count
#define n count
#define nchildren count
#define num count
#define children_count count
#define len count
#define length count
#define items children
#define kids children
#define elements children
#define nodes children
#define widgets_children children

/* Direction aliases */
#define dir direction
#define orientation direction
#define axis direction
#define flex_direction direction
#define layout_direction direction

/* Padding aliases */
#define pad padding
#define padding_px padding
#define p padding

/* Spacing aliases */
#define gap spacing
#define space spacing
#define gutter spacing
#define spacing_px spacing

/* Grow aliases */
#define weight grow
#define flex grow
#define flex_grow grow
#define ratio grow
#define weights grow

/* Fixed aliases */
#define fixed_size fixed
#define is_fixed fixed
#define rigid fixed

/* Basis aliases */
#define base basis
#define preferred basis
#define preferred_size basis

/* ---------- API ---------- */

LayoutContainer *layout_container_create(int direction);
LayoutContainer *layout_create(int direction);
LayoutContainer *flex_container_create(int direction);
LayoutContainer *layout_container_new(int direction);
LayoutContainer *layout_new(int direction);
LayoutContainer *flex_container_new(int direction);
LayoutContainer *layout_make(int direction);

void layout_container_add(LayoutContainer *container, void *child);
void layout_add(LayoutContainer *container, void *child);
void layout_add_child(LayoutContainer *container, void *child);
void layout_append(LayoutContainer *container, void *child);
void container_add_child_layout(LayoutContainer *container, void *child);

void layout_set_direction(void *container, int direction);
void layout_set_padding(void *container, int padding);
void layout_set_spacing(void *container, int spacing);
void layout_set_gap(void *container, int spacing);

void layout_compute(void *container);

/* Convenience aliases for compute */
#define layout_compute_container(c) layout_compute(c)
#define compute_layout(c) layout_compute(c)
#define do_layout(c) layout_compute(c)
#define run_layout(c) layout_compute(c)
#define apply_layout(c) layout_compute(c)
#define layout_run(c) layout_compute(c)
#define layout_apply(c) layout_compute(c)
#define container_layout(c) layout_compute(c)
#define flex_layout(c) layout_compute(c)

/* Recursive helper */
void layout_compute_recursive(void *container);