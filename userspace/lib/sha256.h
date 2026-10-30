#pragma once

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
/* Characters of the lower-case hex form, without the terminating NUL. */
#define SHA256_HEX_LENGTH 64

typedef struct Sha256Context {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    size_t block_fill;
} Sha256Context;

void sha256_init(Sha256Context *context);
void sha256_update(Sha256Context *context, const void *data, size_t length);
/* Ends the computation; the context must be re-initialised before reuse. */
void sha256_final(Sha256Context *context, uint8_t digest[SHA256_DIGEST_SIZE]);

/* Writes SHA256_HEX_LENGTH characters and a NUL to hex. */
void sha256_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE],
                   char hex[SHA256_HEX_LENGTH + 1]);
/* One-shot digest of a buffer, as hex. */
void sha256_hex(const void *data, size_t length, char hex[SHA256_HEX_LENGTH + 1]);
