#pragma once

#include <stddef.h>

/* Largest payload the clipboard accepts. */
#define CLIPBOARD_MAX_BYTES (16u * 1024u * 1024u)

/*
 * One process-wide text clipboard shared by every app of a desktop session.
 * Contents are plain bytes; the clipboard keeps its own copy.
 */

/* Replaces the contents. Returns 0, -E2BIG for an oversized payload or
   -ENOMEM; the previous contents are kept on failure. */
int clipboard_set(const char *data, size_t length);

/* The current contents (length 0 when empty), valid until the next
   clipboard_set(). Never NULL. */
const char *clipboard_data(size_t *length);
