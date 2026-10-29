#pragma once

#include "mini_libc.h"
#include "widgets.h"

#define FM_MAX_ENTRIES 512
#define FM_PATH_CAP 512

typedef struct FmEntry {
    char name[256];
    int is_dir;
} FmEntry;

int fm_init(const char *path);
int fm_navigate(const char *path);
int fm_draw(Surface *surface);
int fm_handle_click(int x, int y);

const char *fm_get_path(void);
const char *fm_get_selected_name(void);
int fm_get_count(void);
const FmEntry *fm_get_entry(int index);