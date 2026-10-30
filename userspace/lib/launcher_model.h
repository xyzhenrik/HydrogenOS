#pragma once

/* What the launcher lists: the applications of the session and the programs
   found on disk, ranked against a query, or pinned and recent entries while
   there is no query. Pure data in, data out, so it is testable on its own. */

#define LAUNCHER_NAME_CAP    48
#define LAUNCHER_PATH_CAP    96
#define LAUNCHER_MAX_ITEMS   8
#define LAUNCHER_MAX_RECENT  3
/* Each group that matches anything is guaranteed this many rows; the group
   with the better best match gets the rows beyond that. */
#define LAUNCHER_GROUP_RESERVE 3

#define LAUNCHER_ENTRY_PROGRAM (-1)

typedef struct LauncherApp {
    int id;                 /* host-defined, non-negative */
    const char *name;
    const char *keywords;   /* extra search terms, space separated */
} LauncherApp;

typedef struct LauncherEntry {
    int app_id;             /* host app id, or LAUNCHER_ENTRY_PROGRAM */
    char name[LAUNCHER_NAME_CAP];
    char path[LAUNCHER_PATH_CAP];   /* programs only */
} LauncherEntry;

/* While searching the groups are applications and programs; otherwise the
   pinned applications come first and the recent programs after them. */
enum { LAUNCHER_SECTION_APPS = 0, LAUNCHER_SECTION_PROGRAMS = 1, LAUNCHER_SECTION_COUNT };

typedef struct LauncherItem {
    LauncherEntry entry;
    int section;
} LauncherItem;

/* Items of one section are contiguous; the sections appear in list order. */
typedef struct LauncherResults {
    LauncherItem items[LAUNCHER_MAX_ITEMS];
    int count;
} LauncherResults;

/* Most recently started first. */
typedef struct LauncherRecents {
    LauncherEntry entries[LAUNCHER_MAX_RECENT];
    int count;
} LauncherRecents;

void launcher_search(const char *query, const LauncherApp *apps, int app_count,
                     const LauncherEntry *programs, int program_count,
                     LauncherResults *results);

/* The view for an empty query: every application, then the recent programs. */
void launcher_suggest(const LauncherApp *apps, int app_count,
                      const LauncherRecents *recents, LauncherResults *results);

/* Moves a started program to the front. Applications are ignored: they are
   always pinned, so repeating them under "Recent" would only duplicate rows. */
void launcher_recents_add(LauncherRecents *recents, const LauncherEntry *entry);
