#include "fuzzy.h"

#include <stddef.h>

#define SCORE_EXACT     1000
#define SCORE_PREFIX     900
#define SCORE_WORD       700
#define SCORE_SUBSTRING  500
#define SCORE_LOOSE      100
#define SCORE_LOOSE_MAX  400

#define LOOSE_CHAR         10
#define LOOSE_ADJACENT     15
#define LOOSE_WORD_START   20

static char fold(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int is_separator(char c) {
    return c == ' ' || c == '-' || c == '_' || c == '.' || c == '/';
}

/* True when the query occurs at text[at]; the caller guarantees that the
   whole query fits. */
static int occurs_at(const char *query, const char *text, size_t at) {
    for (size_t i = 0; query[i]; ++i) {
        if (fold(text[at + i]) != fold(query[i]))
            return 0;
    }
    return 1;
}

/* Scores the in-order match of the query's characters, taking each at its
   first opportunity; adjacent characters and word starts earn a bonus and
   every skipped character costs a point. */
static int loose_score(const char *query, const char *text) {
    int score = SCORE_LOOSE;
    size_t t = 0;
    size_t previous = 0;
    int have_previous = 0;

    for (size_t q = 0; query[q]; ++q) {
        while (text[t] && fold(text[t]) != fold(query[q]))
            ++t;
        if (!text[t])
            return 0;
        score += LOOSE_CHAR;
        if (have_previous && t == previous + 1)
            score += LOOSE_ADJACENT;
        if (t == 0 || is_separator(text[t - 1]))
            score += LOOSE_WORD_START;
        if (have_previous)
            score -= (int)(t - previous - 1);
        previous = t++;
        have_previous = 1;
    }
    if (score > SCORE_LOOSE_MAX) return SCORE_LOOSE_MAX;
    return score < 2 ? 2 : score;
}

int fuzzy_score(const char *query, const char *text, unsigned int flags) {
    if (!query || !text)
        return 0;
    if (!query[0])
        return 1;

    size_t text_length = 0;
    while (text[text_length])
        ++text_length;
    size_t query_length = 0;
    while (query[query_length])
        ++query_length;
    if (query_length > text_length)
        return 0;

    int word_start = -1;
    int substring = -1;
    for (size_t at = 0; at + query_length <= text_length; ++at) {
        if (!occurs_at(query, text, at))
            continue;
        if (at == 0)
            return query_length == text_length
                       ? SCORE_EXACT
                       : SCORE_PREFIX - (int)(text_length - query_length);
        if (word_start < 0 && is_separator(text[at - 1]))
            word_start = (int)at;
        if (substring < 0)
            substring = (int)at;
    }
    if (word_start >= 0)
        return SCORE_WORD - word_start - (int)(text_length - query_length);
    if (substring >= 0)
        return SCORE_SUBSTRING - substring - (int)(text_length - query_length);
    return (flags & FUZZY_LOOSE) ? loose_score(query, text) : 0;
}
