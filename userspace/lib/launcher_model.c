#include "launcher_model.h"

#include "fuzzy.h"
#include "mini_libc.h"

/* Shorter queries must occur literally: two letters are a subsequence of
   nearly every name. */
#define LOOSE_QUERY_MIN 3

typedef struct Candidate {
    int score;
    LauncherEntry entry;
} Candidate;

typedef struct Group {
    Candidate candidates[LAUNCHER_MAX_ITEMS];
    int count;
    int section;
} Group;

static void copy_text(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

static int has_text(const char *s) { return s && s[0]; }

static void app_entry(const LauncherApp *app, LauncherEntry *entry) {
    entry->app_id = app->id;
    copy_text(entry->name, sizeof(entry->name), app->name);
    entry->path[0] = '\0';
}

static void push_item(LauncherResults *results, const LauncherEntry *entry, int section) {
    if (results->count >= LAUNCHER_MAX_ITEMS) return;
    results->items[results->count].entry = *entry;
    results->items[results->count].section = section;
    ++results->count;
}

/* Keeps the best candidates in descending order; ties keep arrival order. */
static void group_insert(Group *group, const Candidate *candidate) {
    int at = group->count;
    if (at == LAUNCHER_MAX_ITEMS && candidate->score <= group->candidates[at - 1].score) return;
    if (at == LAUNCHER_MAX_ITEMS) --at;
    while (at > 0 && group->candidates[at - 1].score < candidate->score) {
        group->candidates[at] = group->candidates[at - 1];
        --at;
    }
    group->candidates[at] = *candidate;
    if (group->count < LAUNCHER_MAX_ITEMS) ++group->count;
}

static void emit_group(const Group *group, int take, LauncherResults *results) {
    for (int i = 0; i < take; ++i) push_item(results, &group->candidates[i].entry, group->section);
}

static int min_int(int a, int b) { return a < b ? a : b; }

/* Splits the rows between two groups: each keeps its reserve, the leading
   group takes what it can use of the rest, and the other group gets whatever
   is still free. */
static void split_rows(int leading_matches, int trailing_matches, int *leading_take, int *trailing_take) {
    int leading = min_int(leading_matches, LAUNCHER_GROUP_RESERVE);
    int trailing = min_int(trailing_matches, LAUNCHER_GROUP_RESERVE);
    leading = min_int(leading_matches, leading + (LAUNCHER_MAX_ITEMS - leading - trailing));
    trailing = min_int(trailing_matches, LAUNCHER_MAX_ITEMS - leading);
    *leading_take = leading;
    *trailing_take = trailing;
}

void launcher_search(const char *query, const LauncherApp *apps, int app_count,
                     const LauncherEntry *programs, int program_count,
                     LauncherResults *results) {
    unsigned int mode = strlen(query) >= LOOSE_QUERY_MIN ? FUZZY_LOOSE : FUZZY_STRICT;
    Group app_group = { .section = LAUNCHER_SECTION_APPS };
    Group program_group = { .section = LAUNCHER_SECTION_PROGRAMS };

    results->count = 0;
    for (int i = 0; i < app_count; ++i) {
        int score = fuzzy_score(query, apps[i].name, mode);
        int keyword_score = has_text(apps[i].keywords)
            ? fuzzy_score(query, apps[i].keywords, FUZZY_STRICT) / 2 : 0;
        if (keyword_score > score) score = keyword_score;
        if (score == 0) continue;
        Candidate candidate = { .score = score };
        app_entry(&apps[i], &candidate.entry);
        group_insert(&app_group, &candidate);
    }
    for (int i = 0; i < program_count; ++i) {
        int score = fuzzy_score(query, programs[i].name, mode);
        if (score == 0) continue;
        Candidate candidate = { score, programs[i] };
        group_insert(&program_group, &candidate);
    }

    int programs_lead = program_group.count > 0 &&
        (app_group.count == 0 || program_group.candidates[0].score > app_group.candidates[0].score);
    const Group *leading = programs_lead ? &program_group : &app_group;
    const Group *trailing = programs_lead ? &app_group : &program_group;
    int leading_take;
    int trailing_take;
    split_rows(leading->count, trailing->count, &leading_take, &trailing_take);
    emit_group(leading, leading_take, results);
    emit_group(trailing, trailing_take, results);
}

void launcher_suggest(const LauncherApp *apps, int app_count,
                      const LauncherRecents *recents, LauncherResults *results) {
    results->count = 0;
    for (int i = 0; i < app_count; ++i) {
        LauncherEntry entry;
        app_entry(&apps[i], &entry);
        push_item(results, &entry, LAUNCHER_SECTION_APPS);
    }
    for (int i = 0; i < recents->count; ++i)
        push_item(results, &recents->entries[i], LAUNCHER_SECTION_PROGRAMS);
}

void launcher_recents_add(LauncherRecents *recents, const LauncherEntry *entry) {
    if (entry->app_id != LAUNCHER_ENTRY_PROGRAM) return;

    int at = 0;
    while (at < recents->count && strcmp(recents->entries[at].path, entry->path) != 0) ++at;
    if (at == recents->count) {
        if (recents->count < LAUNCHER_MAX_RECENT) ++recents->count;
        at = recents->count - 1;
    }
    for (; at > 0; --at) recents->entries[at] = recents->entries[at - 1];
    recents->entries[0] = *entry;
}
