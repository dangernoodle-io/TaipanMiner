#pragma once

#include <stdint.h>
#include <stddef.h>

// Platform compatibility
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

typedef struct {
    uint32_t state[8];
    uint64_t count;       // total bytes processed
    uint8_t  buf[64];     // partial block buffer
} sha256_ctx_t;

// Standard SHA-256
void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]);

// Convenience: single-shot SHA-256
void sha256(const uint8_t *data, size_t len, uint8_t hash[32]);

// Double SHA-256 (Bitcoin standard)
void sha256d(const uint8_t *data, size_t len, uint8_t hash[32]);

// Midstate operations for mining optimization
// Process exactly 64 bytes through SHA-256 and save internal state
void sha256_process_block(sha256_ctx_t *ctx, const uint8_t block[64]);

// Clone a context (save midstate for reuse)
void sha256_clone(sha256_ctx_t *dst, const sha256_ctx_t *src);

// Raw compression function (exposed for optimized mining loop)
void sha256_transform(uint32_t state[8], const uint8_t block[64]);

// Variant that takes pre-built word array (skips load_be32 for W[0-15])
void sha256_transform_words(uint32_t state[8], const uint32_t words[16]);
