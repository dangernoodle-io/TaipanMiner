#pragma once

#include <stdint.h>
#include <stddef.h>
#include "bb_core.h"

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

// Initial hash values (exposed for mining optimization)
extern const uint32_t sha256_H0[8];

// Builds the 64-byte SHA-256 input block for the "abc" known-answer vector,
// suitable for direct peripheral block-fill (raw bytes, not bswapped).
void sha256_build_abc_block(uint8_t out[64]);

// Known-vector self-test: SHA-256("abc")
// Returns BB_OK on PASS, BB_ERR_INVALID_STATE on FAIL
bb_err_t sha256_sw_self_test(void);

// Compare a 32-byte digest against the SHA-256("abc") NIST vector and log
// PASS/FAIL with backend_tag (e.g. "sw", "ahb", "dport"). HW backends call
// this with the digest they read from peripheral registers.
bb_err_t sha256_check_abc_vector(const char *backend_tag, const uint8_t hash[32]);

// TA-33: SHA benchmark result type. Used by both sw and hw bench helpers.
typedef struct {
    int64_t  total_us;             // wall time of ALL iters
    double   us_per_op;            // per-SHA-block-op — steady-state only when settled=true
    bool     settled;              // true if adaptive convergence was reached
    uint32_t settled_after_iters;  // 0 if not settled; else iters elapsed when steady-state began
    int64_t  settled_total_us;     // wall time of settled-only buckets (0 if not settled)
    uint32_t settled_iters;        // count of iters in settled portion (0 if not settled)
} sha_bench_result_t;

// TA-33: SW-path bench for on-demand benchmark endpoint.
// Runs iterations of sha256_transform (the mining hot-path compression function).
// Portable: works on host, C3, S2, wroom32 SW fallback, and device tests.
// out may be NULL (results only logged).
void sha256_sw_bench_pass2(uint32_t iterations, sha_bench_result_t *out);
