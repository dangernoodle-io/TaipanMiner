#pragma once

#ifdef ESP_PLATFORM

#include <stdint.h>
#include "esp_attr.h"

// Enable SHA-256 hardware peripheral clock. Call once at startup.
void sha256_hw_init(void);

// Drop-in hardware replacement for sha256_transform.
// Writes state to SHA_H, bswaps block to SHA_TEXT, SHA_CONTINUE, polls, reads back.
IRAM_ATTR void sha256_hw_transform(uint32_t state[8], const uint8_t block[64]);

// Like sha256_hw_transform but starts fresh (SHA_START auto-seeds H0).
// Ignores incoming state, writes result to state.
IRAM_ATTR void sha256_hw_transform_start(uint32_t state[8], const uint8_t block[64]);

// --- Phase 2: Optimized mining functions ---

// Once per job: prime SHA_TEXT with block2 constants and persistent zeros.
void sha256_hw_init_job(const uint8_t block2[64]);

// Per nonce first hash: write midstate to SHA_H, repair damaged SHA_TEXT words,
// write nonce, SHA_CONTINUE, poll, read result to state.
IRAM_ATTR void sha256_hw_mine_first(uint32_t state[8],
                                     const uint32_t midstate[8],
                                     const uint8_t block2[64],
                                     uint32_t nonce);

// Per nonce second hash: write state[0-7] to SHA_TEXT[0-7], set word 8+15,
// SHA_START, poll. Returns SHA_H[0] for early reject.
// If SHA_H[0] <= target_word0, reads full digest into state[0-7].
IRAM_ATTR uint32_t sha256_hw_mine_second(uint32_t state[8], uint32_t target_word0);

// --- Phase 3: Optimized zero-bswap HW-format pipeline ---

// Compute midstate in HW-native format (no bswap on readback).
// Call once per job. Writes midstate in HW word order to midstate_hw.
IRAM_ATTR void sha256_hw_midstate(const uint8_t header_block1[64],
                                   uint32_t midstate_hw[8]);

// Optimized per-nonce SHA-256d with inline assembly.
// midstate_hw[]: midstate in HW format (from sha256_hw_midstate).
// block2_words[3]: header tail words (block2 bytes 0-11 as uint32_t[3]).
// nonce: nonce to test.
// digest_hw[8]: written only on potential hit (upper 16 bits of SHA_H[7] == 0).
// Returns raw SHA_H_REG[7] value; caller checks (h7_raw >> 16) == 0 for hit.
IRAM_ATTR uint32_t sha256_hw_mine_nonce(const uint32_t midstate_hw[8],
                                         const uint32_t block2_words[3],
                                         uint32_t nonce,
                                         uint32_t digest_hw[8]);

// --- Debug: Verify SHA_TEXT preservation ---

#ifdef STICKMINER_DEBUG
#include <stdbool.h>

// Verify that SHA_TEXT registers preserve their contents after SHA_START.
// Returns true if all 16 words are preserved, false if any are modified.
// (Empirical testing shows they are NOT preserved on ESP32-S3.)
bool sha256_hw_verify_text_preserved(void);
#endif

#endif // ESP_PLATFORM
