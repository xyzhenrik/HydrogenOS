#pragma once

/* Case-insensitive ranking of a search query against a name. */

/* Substring matches only: the query must occur in the text as typed. */
#define FUZZY_STRICT 0
/* Also accept the query's characters appearing in order, with gaps. */
#define FUZZY_LOOSE  1

/* Returns 0 when text does not match query, otherwise a score where higher is
   better: an exact match beats a prefix, a prefix beats the start of a later
   word, that beats a plain substring, and any in-order match ranks last.
   Within a tier, shorter text and earlier matches win. An empty query matches
   everything with the lowest positive score. */
int fuzzy_score(const char *query, const char *text, unsigned int flags);
