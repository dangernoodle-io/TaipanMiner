#include "sha256.h"
#include <string.h>

// Platform compatibility
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

// Initial hash values (H0 - H7)
const uint32_t sha256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

// Rotate right
static inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

// SHA-256 logical functions
static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t Sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static inline uint32_t Sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

// Load 32-bit big-endian value
static inline uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Store 32-bit big-endian value
static inline void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}

// Store 64-bit big-endian value
static inline void store_be64(uint8_t *p, uint64_t v) {
    store_be32(p, (uint32_t)(v >> 32));
    store_be32(p + 4, (uint32_t)v);
}

// SHA-256 round macro for unrolled loop (inlined K constants)
#define SHA256_ROUND(a, b, c, d, e, f, g, h, w, k) do { \
    uint32_t T1 = (h) + Sigma1(e) + Ch((e),(f),(g)) + (k) + (w); \
    (d) += T1; \
    (h) = T1 + Sigma0(a) + Maj((a),(b),(c)); \
} while(0)

// Core SHA-256 compression function (hot path, marked IRAM_ATTR)
IRAM_ATTR void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    // Load message schedule (first 16 words from block)
    for (i = 0; i < 16; i++) {
        W[i] = load_be32(block + i * 4);
    }

    // Expand message schedule (words 16-63)
    for (i = 16; i < 64; i++) {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }

    // Initialize working variables
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    // 64 unrolled rounds with inlined K constants
    // Rounds 0-7
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[0],  0x428a2f98);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[1],  0x71374491);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[2],  0xb5c0fbcf);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[3],  0xe9b5dba5);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[4],  0x3956c25b);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[5],  0x59f111f1);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[6],  0x923f82a4);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[7],  0xab1c5ed5);
    // Rounds 8-15
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[8],  0xd807aa98);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[9],  0x12835b01);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[10], 0x243185be);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[11], 0x550c7dc3);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[12], 0x72be5d74);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[13], 0x80deb1fe);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[14], 0x9bdc06a7);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[15], 0xc19bf174);
    // Rounds 16-23
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[16], 0xe49b69c1);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[17], 0xefbe4786);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[18], 0x0fc19dc6);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[19], 0x240ca1cc);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[20], 0x2de92c6f);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[21], 0x4a7484aa);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[22], 0x5cb0a9dc);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[23], 0x76f988da);
    // Rounds 24-31
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[24], 0x983e5152);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[25], 0xa831c66d);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[26], 0xb00327c8);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[27], 0xbf597fc7);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[28], 0xc6e00bf3);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[29], 0xd5a79147);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[30], 0x06ca6351);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[31], 0x14292967);
    // Rounds 32-39
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[32], 0x27b70a85);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[33], 0x2e1b2138);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[34], 0x4d2c6dfc);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[35], 0x53380d13);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[36], 0x650a7354);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[37], 0x766a0abb);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[38], 0x81c2c92e);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[39], 0x92722c85);
    // Rounds 40-47
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[40], 0xa2bfe8a1);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[41], 0xa81a664b);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[42], 0xc24b8b70);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[43], 0xc76c51a3);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[44], 0xd192e819);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[45], 0xd6990624);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[46], 0xf40e3585);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[47], 0x106aa070);
    // Rounds 48-55
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[48], 0x19a4c116);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[49], 0x1e376c08);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[50], 0x2748774c);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[51], 0x34b0bcb5);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[52], 0x391c0cb3);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[53], 0x4ed8aa4a);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[54], 0x5b9cca4f);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[55], 0x682e6ff3);
    // Rounds 56-63
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[56], 0x748f82ee);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[57], 0x78a5636f);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[58], 0x84c87814);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[59], 0x8cc70208);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[60], 0x90befffa);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[61], 0xa4506ceb);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[62], 0xbef9a3f7);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[63], 0xc67178f2);

    // Add compressed chunk to current hash value
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

// Variant that takes pre-built word array (skips load_be32 for W[0-15])
IRAM_ATTR void sha256_transform_words(uint32_t state[8], const uint32_t words[16]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    // Words already in SHA-256 big-endian format
    for (i = 0; i < 16; i++) {
        W[i] = words[i];
    }

    // Expand message schedule (words 16-63)
    for (i = 16; i < 64; i++) {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    // 64 unrolled rounds with inlined K constants
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[0],  0x428a2f98);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[1],  0x71374491);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[2],  0xb5c0fbcf);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[3],  0xe9b5dba5);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[4],  0x3956c25b);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[5],  0x59f111f1);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[6],  0x923f82a4);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[7],  0xab1c5ed5);
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[8],  0xd807aa98);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[9],  0x12835b01);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[10], 0x243185be);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[11], 0x550c7dc3);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[12], 0x72be5d74);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[13], 0x80deb1fe);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[14], 0x9bdc06a7);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[15], 0xc19bf174);
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[16], 0xe49b69c1);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[17], 0xefbe4786);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[18], 0x0fc19dc6);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[19], 0x240ca1cc);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[20], 0x2de92c6f);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[21], 0x4a7484aa);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[22], 0x5cb0a9dc);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[23], 0x76f988da);
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[24], 0x983e5152);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[25], 0xa831c66d);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[26], 0xb00327c8);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[27], 0xbf597fc7);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[28], 0xc6e00bf3);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[29], 0xd5a79147);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[30], 0x06ca6351);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[31], 0x14292967);
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[32], 0x27b70a85);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[33], 0x2e1b2138);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[34], 0x4d2c6dfc);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[35], 0x53380d13);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[36], 0x650a7354);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[37], 0x766a0abb);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[38], 0x81c2c92e);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[39], 0x92722c85);
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[40], 0xa2bfe8a1);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[41], 0xa81a664b);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[42], 0xc24b8b70);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[43], 0xc76c51a3);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[44], 0xd192e819);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[45], 0xd6990624);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[46], 0xf40e3585);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[47], 0x106aa070);
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[48], 0x19a4c116);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[49], 0x1e376c08);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[50], 0x2748774c);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[51], 0x34b0bcb5);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[52], 0x391c0cb3);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[53], 0x4ed8aa4a);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[54], 0x5b9cca4f);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[55], 0x682e6ff3);
    SHA256_ROUND(a, b, c, d, e, f, g, h, W[56], 0x748f82ee);
    SHA256_ROUND(h, a, b, c, d, e, f, g, W[57], 0x78a5636f);
    SHA256_ROUND(g, h, a, b, c, d, e, f, W[58], 0x84c87814);
    SHA256_ROUND(f, g, h, a, b, c, d, e, W[59], 0x8cc70208);
    SHA256_ROUND(e, f, g, h, a, b, c, d, W[60], 0x90befffa);
    SHA256_ROUND(d, e, f, g, h, a, b, c, W[61], 0xa4506ceb);
    SHA256_ROUND(c, d, e, f, g, h, a, b, W[62], 0xbef9a3f7);
    SHA256_ROUND(b, c, d, e, f, g, h, a, W[63], 0xc67178f2);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx) {
    memcpy(ctx->state, sha256_H0, sizeof(sha256_H0));
    ctx->count = 0;
}

void sha256_process_block(sha256_ctx_t *ctx, const uint8_t block[64]) {
    sha256_transform(ctx->state, block);
    ctx->count += 64;
}

void sha256_clone(sha256_ctx_t *dst, const sha256_ctx_t *src) {
    memcpy(dst, src, sizeof(sha256_ctx_t));
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    if (len == 0) return;

    size_t index = ctx->count % 64;
    size_t space = 64 - index;

    ctx->count += len;

    // Fill buffer if we have partial data
    if (len < space) {
        memcpy(ctx->buf + index, data, len);
        return;
    }

    // Process buffered data + start of input
    if (index != 0) {
        memcpy(ctx->buf + index, data, space);
        sha256_transform(ctx->state, ctx->buf);
        data += space;
        len -= space;
    }

    // Process complete 64-byte blocks
    while (len >= 64) {
        sha256_transform(ctx->state, data);
        data += 64;
        len -= 64;
    }

    // Buffer remaining data
    if (len > 0) {
        memcpy(ctx->buf, data, len);
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]) {
    uint8_t msglen[8];
    size_t index = ctx->count % 64;
    size_t padlen;

    // Store total bit count (big-endian)
    store_be64(msglen, ctx->count * 8);

    // Determine padding length (must reach 56 bytes mod 64)
    padlen = (index < 56) ? (56 - index) : (120 - index);

    // Append 0x80 (first padding byte)
    uint8_t padbuf[128];
    padbuf[0] = 0x80;
    memset(padbuf + 1, 0, padlen - 1);
    memcpy(padbuf + padlen, msglen, 8);

    sha256_update(ctx, padbuf, padlen + 8);

    // Output final hash (big-endian)
    for (int i = 0; i < 8; i++) {
        store_be32(hash + i * 4, ctx->state[i]);
    }
}

void sha256(const uint8_t *data, size_t len, uint8_t hash[32]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
}

void sha256d(const uint8_t *data, size_t len, uint8_t hash[32]) {
    uint8_t first_hash[32];
    sha256(data, len, first_hash);
    sha256(first_hash, 32, hash);
}
