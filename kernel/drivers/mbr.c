#include "mbr.h"
#include "block.h"

int mbr_read(mbr_t *out) {
    if (!out) return -1;
    return block_read_cached(0, 1, out);
}

int mbr_valid(const mbr_t *m) {
    if (!m) return 0;
    return m->signature == 0xAA55;
}
