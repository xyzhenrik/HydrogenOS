#include "fuzzy.h"
#include "mini_libc.h"

static int failures;

static void expect(int condition, const char *what) {
    if (condition) return;
    dprintf(2, "fuzzycheck: %s\n", what);
    ++failures;
}

int main(void) {
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
