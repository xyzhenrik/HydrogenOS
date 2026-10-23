#include "font8x16.h"

// Minimal font: only ASCII 32-126 defined, others zero.
const uint8_t font8x16[256][16] = {
#include "font8x16_data.inc"
};
