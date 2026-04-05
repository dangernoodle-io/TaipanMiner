#pragma once

#ifdef ESP_PLATFORM

#include <stdint.h>
#include "esp_attr.h"
#include "soc/hwcrypto_reg.h"
#include "soc/soc.h"

// Volatile pointers for direct register access
#define SHA_H_REG   ((volatile uint32_t *)SHA_H_BASE)
#define SHA_TEXT_REG ((volatile uint32_t *)SHA_TEXT_BASE)

// Enable SHA-256 hardware peripheral clock. Call once at startup.
void sha256_hw_init(void);

// Drop-in hardware replacement for sha256_transform.
// Writes state to SHA_H, bswaps block to SHA_TEXT, SHA_CONTINUE, polls, reads back.
void sha256_hw_transform(uint32_t state[8], const uint8_t block[64]);

// Like sha256_hw_transform but starts fresh (SHA_START auto-seeds H0).
// Ignores incoming state, writes result to state.
void sha256_hw_transform_start(uint32_t state[8], const uint8_t block[64]);

// --- Phase 2: Optimized mining functions ---

// Once per job: prime SHA_TEXT with block2 constants and persistent zeros.
void sha256_hw_init_job(const uint8_t block2[64]);

// Per nonce first hash: write midstate to SHA_H, repair damaged SHA_TEXT words,
// write nonce, SHA_CONTINUE, poll, read result to state.
void sha256_hw_mine_first(uint32_t state[8],
                          const uint32_t midstate[8],
                          const uint8_t block2[64],
                          uint32_t nonce);

// Per nonce second hash: write state[0-7] to SHA_TEXT[0-7], set word 8+15,
// SHA_START, poll. Returns SHA_H[0] for early reject.
// If SHA_H[0] <= target_word0, reads full digest into state[0-7].
uint32_t sha256_hw_mine_second(uint32_t state[8], uint32_t target_word0);

// --- Phase 3: Optimized zero-bswap HW-format pipeline ---

// Compute midstate in HW-native format (no bswap on readback).
// Call once per job. Writes midstate in HW word order to midstate_hw.
void sha256_hw_midstate(const uint8_t header_block1[64],
                        uint32_t midstate_hw[8]);

// Optimized per-nonce SHA-256d with zero-bswap pipeline.
// midstate_hw[]: midstate in HW format (from sha256_hw_midstate).
// block2_words[3]: header tail words (block2 bytes 0-11 as uint32_t[3]).
// nonce: nonce to test.
// digest_hw[8]: written only on potential hit (upper 16 bits of h7_raw are zero).
// Returns raw SHA_H_REG[7] value; caller performs full target comparison.
static inline __attribute__((always_inline)) IRAM_ATTR uint32_t
sha256_hw_mine_nonce(const uint32_t midstate_hw[8],
                     const uint32_t block2_words[3],
                     uint32_t nonce,
                     uint32_t digest_hw[8])
{
    uint32_t h7_raw;

    // --- Pass 1: midstate + block2 tail + nonce → SHA_CONTINUE ---
    // Write midstate_hw to SHA_H (already in HW format, no bswap)
    for (int i = 0; i < 8; i++) {
        SHA_H_REG[i] = midstate_hw[i];
    }

    // Write SHA_TEXT: block2_words[0-2], nonce, padding, bit-length
    SHA_TEXT_REG[0] = block2_words[0];
    SHA_TEXT_REG[1] = block2_words[1];
    SHA_TEXT_REG[2] = block2_words[2];
    SHA_TEXT_REG[3] = nonce;
    SHA_TEXT_REG[4] = 0x00000080;
    SHA_TEXT_REG[5] = 0;
    SHA_TEXT_REG[6] = 0;
    SHA_TEXT_REG[7] = 0;
    SHA_TEXT_REG[8] = 0;
    SHA_TEXT_REG[9] = 0;
    SHA_TEXT_REG[10] = 0;
    SHA_TEXT_REG[11] = 0;
    SHA_TEXT_REG[12] = 0;
    SHA_TEXT_REG[13] = 0;
    SHA_TEXT_REG[14] = 0;
    SHA_TEXT_REG[15] = 0x80020000;

    REG_WRITE(SHA_CONTINUE_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}

    // --- Pass 2: copy SHA_H → SHA_TEXT directly (no bswap!) ---
    // This is the key optimization: SHA_H and SHA_TEXT are both in HW format
    SHA_TEXT_REG[0] = SHA_H_REG[0];
    SHA_TEXT_REG[1] = SHA_H_REG[1];
    SHA_TEXT_REG[2] = SHA_H_REG[2];
    SHA_TEXT_REG[3] = SHA_H_REG[3];
    SHA_TEXT_REG[4] = SHA_H_REG[4];
    SHA_TEXT_REG[5] = SHA_H_REG[5];
    SHA_TEXT_REG[6] = SHA_H_REG[6];
    SHA_TEXT_REG[7] = SHA_H_REG[7];
    SHA_TEXT_REG[8] = 0x00000080;
    SHA_TEXT_REG[9] = 0;
    SHA_TEXT_REG[10] = 0;
    SHA_TEXT_REG[11] = 0;
    SHA_TEXT_REG[12] = 0;
    SHA_TEXT_REG[13] = 0;
    SHA_TEXT_REG[14] = 0;
    SHA_TEXT_REG[15] = 0x00010000;

    REG_WRITE(SHA_START_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}

    // Early reject: if upper 16 bits of h7_raw are nonzero, hash can't meet
    // any pool difficulty >= ~6e-8 (covers all real-world mining scenarios)
    h7_raw = SHA_H_REG[7];
    if ((h7_raw >> 16) != 0) {
        return h7_raw;
    }

    // Potential hit — read full digest in HW format
    for (int i = 0; i < 7; i++) {
        digest_hw[i] = SHA_H_REG[i];
    }
    digest_hw[7] = h7_raw;
    return h7_raw;
}

// --- Debug utilities ---

#ifdef TAIPANMINER_DEBUG
#include <stdbool.h>

// Verify that SHA_TEXT registers preserve their contents after SHA_START.
// Returns true if all 16 words are preserved, false if any are modified.
// (Empirical testing shows they are NOT preserved on ESP32-S3.)
bool sha256_hw_verify_text_preserved(void);

// Debug benchmark comparing SHA_START vs SHA_CONTINUE+H0 for second hash pass.
// Runs iterations times for each approach and logs timing results.
void sha256_hw_bench_pass2(uint32_t iterations);
#endif

#endif // ESP_PLATFORM
