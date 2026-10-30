#include "clipboard.h"
#include "errno.h"
#include "mini_libc.h"

static char *contents;
static size_t contents_length;

int clipboard_set(const char *data, size_t length) {
    if (length > CLIPBOARD_MAX_BYTES) return -E2BIG;
    if (length > 0 && !data) return -EINVAL;

    char *copy = 0;
    if (length > 0) {
        copy = malloc(length);
        if (!copy) return -ENOMEM;
        memcpy(copy, data, length);
    }
    free(contents);
    contents = copy;
    contents_length = length;
    return 0;
}

const char *clipboard_data(size_t *length) {
    if (length) *length = contents_length;
    return contents ? contents : "";
}
