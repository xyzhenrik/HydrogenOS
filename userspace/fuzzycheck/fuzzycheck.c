#include "fuzzy.h"
#include "launcher_model.h"
#include "mini_libc.h"

static int failures;

static void expect(int condition, const char *what) {
    if (condition) return;
    dprintf(2, "fuzzycheck: %s\n", what);
    ++failures;
}

static const LauncherApp apps[] = {
    { 0, "Terminal", "shell console command prompt" },
    { 1, "Editor", "text notes write" },
    { 2, "Files", "file manager folder browser" },
    { 3, "Settings", "preferences appearance theme" },
    { 4, "About HydrogenOS", "version system information" },
};
#define APP_COUNT ((int)(sizeof(apps) / sizeof(apps[0])))

static const char *const program_names[] = {
    "echo", "edit", "sh", "shell", "sleep", "sigtest", "schedtest", "syslogd",
    "stdiocheck", "seekcheck", "ps", "ping", "date", "cat",
};
#define PROGRAM_COUNT ((int)(sizeof(program_names) / sizeof(program_names[0])))

static LauncherEntry programs[PROGRAM_COUNT];

static void fill_programs(void) {
    for (int i = 0; i < PROGRAM_COUNT; ++i) {
        programs[i].app_id = LAUNCHER_ENTRY_PROGRAM;
        snprintf(programs[i].name, sizeof(programs[i].name), "%s", program_names[i]);
        snprintf(programs[i].path, sizeof(programs[i].path), "/%s", program_names[i]);
    }
}

static int has_item(const LauncherResults *results, const char *name) {
    for (int i = 0; i < results->count; ++i)
        if (strcmp(results->items[i].entry.name, name) == 0) return 1;
    return 0;
}

static int sections_contiguous(const LauncherResults *results) {
    int changes = 0;
    for (int i = 1; i < results->count; ++i)
        if (results->items[i].section != results->items[i - 1].section) ++changes;
    return changes <= 1;
}

static int count_section(const LauncherResults *results, int section) {
    int n = 0;
    for (int i = 0; i < results->count; ++i)
        if (results->items[i].section == section) ++n;
    return n;
}

static void check_search(void) {
    LauncherResults results;

    /* Programs starting with the letter outnumber the rows, yet the matching
       application must stay reachable and the list stays within its limit. */
    launcher_search("s", apps, APP_COUNT, programs, PROGRAM_COUNT, &results);
    expect(has_item(&results, "Settings"), "a crowd of program matches must not push out a matching application");
    expect(results.count == LAUNCHER_MAX_ITEMS, "a full result list fills its rows");
    expect(sections_contiguous(&results), "results of one group stay together");

    launcher_search("e", apps, APP_COUNT, programs, PROGRAM_COUNT, &results);
    expect(has_item(&results, "Editor"), "an application matching by prefix stays reachable among programs");
    expect(count_section(&results, LAUNCHER_SECTION_PROGRAMS) >= LAUNCHER_GROUP_RESERVE,
           "the programs keep their reserved rows");
    expect(sections_contiguous(&results), "results of one group stay together for 'e'");

    launcher_search("sleep", apps, APP_COUNT, programs, PROGRAM_COUNT, &results);
    expect(results.count >= 1 && strcmp(results.items[0].entry.name, "sleep") == 0 &&
           results.items[0].section == LAUNCHER_SECTION_PROGRAMS &&
           results.items[0].entry.app_id == LAUNCHER_ENTRY_PROGRAM, "an exact program name comes first");

    launcher_search("term", apps, APP_COUNT, programs, PROGRAM_COUNT, &results);
    expect(results.count == 1 && results.items[0].entry.app_id == 0 &&
           results.items[0].section == LAUNCHER_SECTION_APPS, "an application is found by its name");

    launcher_search("console", apps, APP_COUNT, programs, PROGRAM_COUNT, &results);
    expect(results.count == 1 && results.items[0].entry.app_id == 0, "an application is found by a keyword");

    launcher_search("zzz", apps, APP_COUNT, programs, PROGRAM_COUNT, &results);
    expect(results.count == 0, "an unmatched query has no results");

    launcher_search("ssl", apps, APP_COUNT, programs, PROGRAM_COUNT, &results);
    expect(has_item(&results, "syslogd"), "three letters match as a subsequence");
    launcher_search("sg", apps, APP_COUNT, programs, PROGRAM_COUNT, &results);
    expect(results.count == 0, "two letters only match literally");

    launcher_search("s", apps, 0, programs, 0, &results);
    expect(results.count == 0, "nothing to search yields nothing");
}

static void check_suggestions(void) {
    LauncherResults results;
    LauncherRecents recents = { .count = 0 };

    launcher_suggest(apps, APP_COUNT, &recents, &results);
    expect(results.count == APP_COUNT && count_section(&results, LAUNCHER_SECTION_APPS) == APP_COUNT,
           "without recents only the pinned applications show");
    expect(strcmp(results.items[0].entry.name, "Terminal") == 0, "pinned applications keep the host's order");

    LauncherEntry app = { 1, "Editor", "" };
    launcher_recents_add(&recents, &app);
    expect(recents.count == 0, "applications are not remembered, they are pinned already");

    launcher_recents_add(&recents, &programs[0]);
    launcher_recents_add(&recents, &programs[1]);
    launcher_recents_add(&recents, &programs[0]);
    expect(recents.count == 2 && strcmp(recents.entries[0].name, "echo") == 0 &&
           strcmp(recents.entries[1].name, "edit") == 0,
           "starting a program again moves it to the front without duplicating it");

    launcher_recents_add(&recents, &programs[2]);
    launcher_recents_add(&recents, &programs[3]);
    expect(recents.count == LAUNCHER_MAX_RECENT && strcmp(recents.entries[0].name, "shell") == 0 &&
           strcmp(recents.entries[LAUNCHER_MAX_RECENT - 1].name, "echo") == 0,
           "the oldest recent program is dropped when the list is full");

    launcher_suggest(apps, APP_COUNT, &recents, &results);
    expect(results.count == APP_COUNT + LAUNCHER_MAX_RECENT &&
           count_section(&results, LAUNCHER_SECTION_PROGRAMS) == LAUNCHER_MAX_RECENT &&
           results.items[APP_COUNT].section == LAUNCHER_SECTION_PROGRAMS &&
           results.items[APP_COUNT].entry.app_id == LAUNCHER_ENTRY_PROGRAM,
           "recent programs follow the pinned applications");
}

int main(void) {
    fill_programs();
    check_search();
    check_suggestions();

    int exact = fuzzy_score("shell", "shell", FUZZY_LOOSE);
    int prefix = fuzzy_score("she", "shell", FUZZY_LOOSE);
    int word = fuzzy_score("test", "shell test", FUZZY_LOOSE);
    int substring = fuzzy_score("ell", "shell", FUZZY_LOOSE);
    int loose = fuzzy_score("shl", "shell", FUZZY_LOOSE);

    expect(exact > prefix && prefix > word && word > substring && substring > loose && loose > 0,
           "tiers must rank exact > prefix > word start > substring > subsequence");
    expect(fuzzy_score("SHELL", "shell", FUZZY_STRICT) == exact, "matching ignores case");
    expect(fuzzy_score("shl", "shell", FUZZY_STRICT) == 0, "strict mode rejects subsequences");
    expect(fuzzy_score("xyz", "shell", FUZZY_LOOSE) == 0, "unrelated text does not match");
    expect(fuzzy_score("shells", "shell", FUZZY_LOOSE) == 0, "a query longer than the text does not match");
    expect(fuzzy_score("lh", "shell", FUZZY_LOOSE) == 0, "subsequence characters must keep their order");
    expect(fuzzy_score("", "shell", FUZZY_STRICT) > 0, "an empty query matches everything");
    expect(fuzzy_score("sh", "shell", FUZZY_STRICT) > fuzzy_score("sh", "shelltest", FUZZY_STRICT),
           "the shorter name wins among prefix matches");
    expect(fuzzy_score("e", "shell", FUZZY_STRICT) > fuzzy_score("l", "shell", FUZZY_STRICT),
           "an earlier occurrence wins among substring matches");
    expect(fuzzy_score("ms", "my_shell", FUZZY_LOOSE) > fuzzy_score("ms", "mess", FUZZY_LOOSE),
           "subsequence hits on word starts beat hits inside words");
    expect(fuzzy_score("he", "hello", FUZZY_LOOSE) > fuzzy_score("he", "shell", FUZZY_LOOSE),
           "a prefix beats a plain substring");
    expect(fuzzy_score(0, "shell", FUZZY_LOOSE) == 0 && fuzzy_score("a", 0, FUZZY_LOOSE) == 0,
           "null arguments do not match");

    if (failures) return 1;
    printf("FUZZY PASS\n");
    return 0;
}
