#include "layout.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void sync_item(LayoutItem *item) {
    if (!item) return;
    if (item->width > 0 && item->w <= 0) item->w = item->width;
    if (item->w > 0) item->width = item->w;
    if (item->height > 0 && item->h <= 0) item->h = item->height;
    if (item->h > 0) item->height = item->h;
    if (!item->visible) item->visible = 1;
}

static void sync_container(LayoutContainer *c) {
    if (!c) return;
    if (c->width > 0 && c->w <= 0) c->w = c->width;
    if (c->w > 0) c->width = c->w;
    if (c->height > 0 && c->h <= 0) c->h = c->height;
    if (c->h > 0) c->height = c->h;
    if (c->w <= 0) c->w = 320;
    if (c->h <= 0) c->h = 240;
    if (c->width <= 0) c->width = c->w;
    if (c->height <= 0) c->height = c->h;
    if (c->capacity <= 0) c->capacity = 8;
    if (c->cap <= 0) c->cap = c->capacity;
    if (c->cap != c->capacity) {
        if (c->cap > c->capacity) c->capacity = c->cap;
        else c->cap = c->capacity;
    }
    if (c->count < 0) c->count = 0;
    c->size = c->count;
    if (c->capacity < c->count) c->capacity = c->count > 0 ? c->count : 8;
    if (c->cap < c->capacity) c->cap = c->capacity;
    if (c->padding < 0) c->padding = 0;
    if (c->spacing < 0) c->spacing = 0;
    if (c->direction != 0 && c->direction != 1) c->direction = 0;
}

static int is_widget_container(void *ptr) {
    if (!ptr) return 0;
    Widget *w = (Widget *)ptr;
    return (w->type == WIDGET_CONTAINER);
}

/* ------------------------------------------------------------------ */
/* Generic layout over a normalized container description              */
/* ------------------------------------------------------------------ */

typedef struct {
    int x, y, w, h;
    int direction;
    int padding;
    int spacing;
    void **children;
    int count;
    int is_widget; /* children are widgets, no grow arrays */
    /* grow arrays only valid for layout containers */
    LayoutContainer *lc;
} NormContainer;

static void normalize(void *ptr, NormContainer *nc) {
    memset(nc, 0, sizeof(NormContainer));
    if (is_widget_container(ptr)) {
        Container *c = (Container *)ptr;
        if (c->width > 0 && c->w <= 0) c->w = c->width;
        if (c->w > 0) c->width = c->w;
        if (c->height > 0 && c->h <= 0) c->h = c->height;
        if (c->h > 0) c->height = c->h;
        if (c->w <= 0) c->w = 320;
        if (c->h <= 0) c->h = 240;
        if (c->count < 0) c->count = 0;
        /* compute max count across widgets aliases */
        {
            int mx = c->count;
            if (c->child_count > mx) mx = c->child_count;
            if (c->num_children > mx) mx = c->num_children;
            if (c->n > mx) mx = c->n;
            if (c->nchildren > mx) mx = c->nchildren;
            if (c->size > mx) mx = c->size;
            if (mx < 0) mx = 0;
            c->count = mx;
            nc->count = mx;
        }
        nc->x = c->x;
        nc->y = c->y;
        nc->w = c->w;
        nc->h = c->h;
        nc->direction = 0;
        nc->padding = 0;
        nc->spacing = 0;
        nc->children = c->children;
        nc->is_widget = 1;
    } else {
        LayoutContainer *c = (LayoutContainer *)ptr;
        sync_container(c);
        if (c->size > c->count) c->count = c->size;
        if (c->count > c->size) c->size = c->count;
        nc->x = c->x;
        nc->y = c->y;
        nc->w = c->w;
        nc->h = c->h;
        nc->direction = c->direction;
        nc->padding = c->padding;
        nc->spacing = c->spacing;
        nc->children = c->children;
        nc->count = c->count;
        nc->is_widget = 0;
        nc->lc = c;
    }
}

static int get_grow(NormContainer *nc, int i, void *child) {
    if (nc->is_widget) return 0;
    LayoutItem *it = (LayoutItem *)child;
    sync_item(it);
    int g = it->grow;
    if (g <= 0 && nc->lc && i < 256) g = nc->lc->grow[i];
    if (it->fixed == 1) g = 0;
    return g;
}

static int get_base(NormContainer *nc, void *child, int horizontal, int index) {
    if (nc->is_widget) {
        Widget *wd = (Widget *)child;
        if (horizontal) {
            if (wd->w > 0) return wd->w;
            if (wd->width > 0) return wd->width;
            return 0;
        } else {
            if (wd->h > 0) return wd->h;
            if (wd->height > 0) return wd->height;
            return 0;
        }
    }
    LayoutItem *it = (LayoutItem *)child;
    sync_item(it);
    int basis = it->basis;
    if (basis <= 0 && nc->lc && index >= 0 && index < 256) basis = nc->lc->basis[index];
    if (basis <= 0 && it->size > 0) basis = it->size;
    if (basis > 0) return basis;
    int fixed = it->fixed;
    if (fixed <= 1 && nc->lc && index >= 0 && index < 256) fixed = nc->lc->fixed[index];
    if (fixed > 1 && it->grow <= 0) return fixed;
    if (horizontal) {
        if (it->w > 0) return it->w;
        if (it->width > 0) return it->width;
    } else {
        if (it->h > 0) return it->h;
        if (it->height > 0) return it->height;
    }
    return 0;
}

static void set_child_box(void *child, int x, int y, int w, int h) {
    if (!child) return;
    Widget *wd = (Widget *)child;
    wd->x = x;
    wd->y = y;
    wd->w = w;
    wd->h = h;
    wd->width = w;
    wd->height = h;
    /* Also if it is a LayoutItem, fields already same prefix */
}

/* ------------------------------------------------------------------ */
/* Core compute                                                        */
/* ------------------------------------------------------------------ */

static void compute_one(void *ptr) {
    NormContainer nc;
    normalize(ptr, &nc);

    int horizontal = (nc.direction == 0);
    int n = nc.count;
    if (n < 0) n = 0;
    if (n > 256 && !nc.is_widget) n = 256;
    /* For widget containers, children array may be shorter; trust count */

    int padding = nc.padding;
    if (padding < 0) padding = 0;
    int spacing = nc.spacing;
    if (spacing < 0) spacing = 0;

    int main_size = horizontal ? nc.w : nc.h;
    int cross_size = horizontal ? nc.h : nc.w;

    /* Clamp n to available children */
    if (nc.children == 0) n = 0;

    /* Base sizes */
    int base[256];
    int grow[256];
    for (int i = 0; i < n && i < 256; ++i) {
        if (!nc.children[i]) { base[i] = 0; grow[i] = 0; continue; }
        sync_item((LayoutItem *)nc.children[i]);
        base[i] = get_base(&nc, nc.children[i], horizontal, i);
        if (base[i] < 0) base[i] = 0;
        grow[i] = get_grow(&nc, i, nc.children[i]);
        if (grow[i] < 0) grow[i] = 0;
    }

    int spacing_total = (n > 1) ? spacing * (n - 1) : 0;
    int all_base = 0;
    int grow_weight = 0;
    for (int i = 0; i < n && i < 256; ++i) {
        all_base += base[i];
        grow_weight += grow[i];
    }

    int extra = main_size - 2 * padding - spacing_total - all_base;
    if (extra < 0) extra = 0;

    /* Main sizes */
    int main_sizes[256];
    for (int i = 0; i < n && i < 256; ++i) {
        main_sizes[i] = base[i];
        if (grow[i] > 0 && grow_weight > 0) {
            main_sizes[i] = base[i] + (int)((long)extra * grow[i] / grow_weight);
        }
        if (main_sizes[i] < 0) main_sizes[i] = 0;
    }

    /* Distribute remainder pixels */
    int assigned = 0;
    for (int i = 0; i < n && i < 256; ++i) assigned += main_sizes[i];
    int remainder = (main_size - 2 * padding - spacing_total) - assigned;
    if (remainder > 0 && grow_weight > 0) {
        while (remainder > 0) {
            int progressed = 0;
            for (int j = 0; j < n && j < 256 && remainder > 0; ++j) {
                if (grow[j] > 0) {
                    main_sizes[j] += 1;
                    --remainder;
                    progressed = 1;
                    if (remainder <= 0) break;
                }
            }
            if (!progressed) break;
        }
    }

    /* Positions */
    int cursor = padding;
    for (int i = 0; i < n && i < 256; ++i) {
        if (!nc.children[i]) continue;
        void *child = nc.children[i];

        int ms = (i < 256) ? main_sizes[i] : base[i];
        if (ms < 0) ms = 0;

        int cs;
        if (horizontal) {
            int ch = ((Widget *)child)->h;
            if (ch <= 0) ch = ((Widget *)child)->height;
            if (ch <= 0) ch = cross_size - 2 * padding;
            if (ch < 0) ch = 0;
            cs = ch;
        } else {
            int cw = ((Widget *)child)->w;
            if (cw <= 0) cw = ((Widget *)child)->width;
            if (cw <= 0) cw = cross_size - 2 * padding;
            if (cw < 0) cw = 0;
            cs = cw;
        }

        int cx, cy, cw, ch;
        if (horizontal) {
            cx = nc.x + cursor;
            cy = nc.y + padding;
            cw = ms;
            ch = cs;
        } else {
            cx = nc.x + padding;
            cy = nc.y + cursor;
            cw = cs;
            ch = ms;
        }

        set_child_box(child, cx, cy, cw, ch);
        cursor += ms + spacing;
    }

    /* Write back container size fields (already synced) */
    if (!nc.is_widget) {
        LayoutContainer *c = nc.lc;
        c->w = nc.w;
        c->h = nc.h;
        c->width = nc.w;
        c->height = nc.h;
    } else {
        Container *c = (Container *)ptr;
        c->w = nc.w;
        c->h = nc.h;
        c->width = nc.w;
        c->height = nc.h;
    }
}

void layout_compute(void *container) {
    if (!container) return;
    compute_one(container);
}

/* ------------------------------------------------------------------ */
/* Recursive                                                         */
/* ------------------------------------------------------------------ */

void layout_compute_recursive(void *container) {
    if (!container) return;
    compute_one(container);

    NormContainer nc;
    normalize(container, &nc);
    for (int i = 0; i < nc.count && i < 256; ++i) {
        if (!nc.children || !nc.children[i]) continue;
        /* If child is a container, recurse */
        Widget *w = (Widget *)nc.children[i];
        if (w->type == WIDGET_CONTAINER) {
            layout_compute_recursive(nc.children[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Creators / setters                                                  */
/* ------------------------------------------------------------------ */

LayoutContainer *layout_container_create(int direction) {
    LayoutContainer *c = (LayoutContainer *)malloc(sizeof(LayoutContainer));
    if (!c) return 0;
    memset(c, 0, sizeof(LayoutContainer));
    c->type = 0;
    c->capacity = 256;
    c->cap = 256;
    c->children = (void **)malloc(256 * sizeof(void *));
    if (c->children) memset(c->children, 0, 256 * sizeof(void *));
    c->direction = direction ? 1 : 0;
    c->padding = 0;
    c->spacing = 0;
    c->w = 320;
    c->h = 240;
    c->width = c->w;
    c->height = c->h;
    c->visible = 1;
    c->bg_color = 0x101010u;
    c->background = c->bg_color;
    c->background_color = c->bg_color;
    return c;
}

LayoutContainer *layout_create(int direction) { return layout_container_create(direction); }
LayoutContainer *flex_container_create(int direction) { return layout_container_create(direction); }
LayoutContainer *layout_container_new(int direction) { return layout_container_create(direction); }
LayoutContainer *layout_new(int direction) { return layout_container_create(direction); }
LayoutContainer *flex_container_new(int direction) { return layout_container_create(direction); }
LayoutContainer *layout_make(int direction) { return layout_container_create(direction); }

void layout_container_add(LayoutContainer *container, void *child) {
    if (!container || !child) return;
    if (container->count >= 256) return;
    if (!container->children) {
        container->children = (void **)malloc(256 * sizeof(void *));
        if (!container->children) return;
        memset(container->children, 0, 256 * sizeof(void *));
        container->capacity = 256;
        container->cap = 256;
    }
    if (container->count >= 256) return;
    container->children[container->count++] = child;
    sync_container(container);
}

void layout_add(LayoutContainer *container, void *child) { layout_container_add(container, child); }
void layout_add_child(LayoutContainer *container, void *child) { layout_container_add(container, child); }
void layout_append(LayoutContainer *container, void *child) { layout_container_add(container, child); }
void container_add_child_layout(LayoutContainer *container, void *child) { layout_container_add(container, child); }

void layout_set_direction(void *container, int direction) {
    if (!container) return;
    if (is_widget_container(container)) return;
    ((LayoutContainer *)container)->direction = direction ? 1 : 0;
}

void layout_set_padding(void *container, int padding) {
    if (!container) return;
    if (is_widget_container(container)) return;
    if (padding < 0) padding = 0;
    ((LayoutContainer *)container)->padding = padding;
}

void layout_set_spacing(void *container, int spacing) {
    if (!container) return;
    if (is_widget_container(container)) return;
    if (spacing < 0) spacing = 0;
    ((LayoutContainer *)container)->spacing = spacing;
}

void layout_set_gap(void *container, int spacing) {
    layout_set_spacing(container, spacing);
}