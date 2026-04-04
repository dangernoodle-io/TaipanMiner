#ifdef ESP_PLATFORM

#include "sha256_hw.h"
#include "soc/hwcrypto_reg.h"
#include "soc/soc.h"
#include "soc/periph_defs.h"
#include "esp_private/periph_ctrl.h"
#include "esp_attr.h"

// Volatile pointers for direct register access
#define SHA_H_REG   ((volatile uint32_t *)SHA_H_BASE)
#define SHA_TEXT_REG ((volatile uint32_t *)SHA_TEXT_BASE)

// ESP32-S3 SHA hardware stores registers as raw bytes in memory-mapped IO.
// On the LE Xtensa core, reading/writing uint32_t gives the native LE
// representation.  SHA_TEXT receives message words as raw LE casts of the
// byte stream (the hardware handles BE conversion internally).  SHA_H
// stores the hash state the same way — memcpy of SHA_H to a byte buffer
// yields correct BE hash bytes (this is how ESP-IDF's mbedTLS port works).
//
// Our software SHA and mining code use *standard* SHA-256 word values
// (H0 = 0x6a09e667, etc.).  To convert between standard and HW format
// we bswap32 on every SHA_H read/write.  SHA_TEXT needs no swapping —
// we cast byte buffers to uint32_t* and write directly, matching ESP-IDF.
//
// NOTE: The ESP32-S3 SHA peripheral overwrites SHA_TEXT registers during
// message schedule expansion (W[] computation). SHA_TEXT contents are NOT
// preserved after SHA_START or SHA_CONTINUE. This was verified empirically
// via sha256_hw_verify_text_preserved(). As a result, all 16 SHA_TEXT words
// must be written before each SHA operation — no per-nonce write reduction
// is possible.

void sha256_hw_init(void)
{
    periph_module_enable(PERIPH_SHA_MODULE);
    REG_WRITE(SHA_MODE_REG, 2);  // SHA-256

#ifdef STICKMINER_DEBUG
    sha256_hw_verify_text_preserved();
#endif
}

IRAM_ATTR void sha256_hw_transform(uint32_t state[8], const uint8_t block[64])
{
    // Write state to SHA_H (bswap: standard → HW format)
    for (int i = 0; i < 8; i++) {
        SHA_H_REG[i] = __builtin_bswap32(state[i]);
    }

    // Write block to SHA_TEXT (no bswap — raw LE cast matches HW expectation)
    const uint32_t *w = (const uint32_t *)block;
    for (int i = 0; i < 16; i++) {
        SHA_TEXT_REG[i] = w[i];
    }

    // Continue from existing state
    REG_WRITE(SHA_CONTINUE_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}

    // Read result (bswap: HW format → standard)
    for (int i = 0; i < 8; i++) {
        state[i] = __builtin_bswap32(SHA_H_REG[i]);
    }
}

IRAM_ATTR void sha256_hw_transform_start(uint32_t state[8], const uint8_t block[64])
{
    // Write block to SHA_TEXT (no bswap)
    const uint32_t *w = (const uint32_t *)block;
    for (int i = 0; i < 16; i++) {
        SHA_TEXT_REG[i] = w[i];
    }

    // Start fresh (H0 auto-seeded by hardware)
    REG_WRITE(SHA_START_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}

    // Read result (bswap: HW format → standard)
    for (int i = 0; i < 8; i++) {
        state[i] = __builtin_bswap32(SHA_H_REG[i]);
    }
}

// --- Phase 2: Optimized mining functions ---

void sha256_hw_init_job(const uint8_t block2[64])
{
    // Prime SHA_TEXT with block2 (no bswap — raw LE cast)
    const uint32_t *w = (const uint32_t *)block2;
    for (int i = 0; i < 16; i++) {
        SHA_TEXT_REG[i] = w[i];
    }
}

IRAM_ATTR void sha256_hw_mine_first(uint32_t state[8],
                                     const uint32_t midstate[8],
                                     const uint8_t block2[64],
                                     uint32_t nonce)
{
    const uint32_t *w = (const uint32_t *)block2;

    // Write midstate to SHA_H (bswap: standard → HW format)
    for (int i = 0; i < 8; i++) {
        SHA_H_REG[i] = __builtin_bswap32(midstate[i]);
    }

    // Rewrite all 16 SHA_TEXT words (hardware overwrites during schedule expansion).
    // Words 0-2: header tail constants (no bswap — raw LE cast from block2)
    SHA_TEXT_REG[0] = w[0];
    SHA_TEXT_REG[1] = w[1];
    SHA_TEXT_REG[2] = w[2];

    // Word 3: nonce — block2 stores nonce LE at bytes 12-15, so the raw
    // LE uint32 cast of those bytes equals the nonce value.
    SHA_TEXT_REG[3] = nonce;

    // Word 4: 0x80 padding byte at position 16 → LE uint32 = 0x00000080
    SHA_TEXT_REG[4] = 0x00000080;

    // Words 5-14: zeros
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

    // Word 15: bit length 640 = 0x280 → bytes [0x00,0x00,0x02,0x80] → LE uint32
    SHA_TEXT_REG[15] = 0x80020000;

    // Continue from midstate
    REG_WRITE(SHA_CONTINUE_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}

    // Read first-hash result (bswap: HW format → standard)
    for (int i = 0; i < 8; i++) {
        state[i] = __builtin_bswap32(SHA_H_REG[i]);
    }
}

IRAM_ATTR uint32_t sha256_hw_mine_second(uint32_t state[8], uint32_t target_word0)
{
    // Write first-hash state to SHA_TEXT as the 32-byte message for the second hash.
    // state[] is in standard format; bswap to get the raw LE byte representation
    // that SHA_TEXT expects (hash byte 0 in low byte of word 0, etc.).
    for (int i = 0; i < 8; i++) {
        SHA_TEXT_REG[i] = __builtin_bswap32(state[i]);
    }

    // Word 8: 0x80 padding byte at position 32 → LE uint32 = 0x00000080
    SHA_TEXT_REG[8] = 0x00000080;

    // Words 9-14: zeros
    SHA_TEXT_REG[9] = 0;
    SHA_TEXT_REG[10] = 0;
    SHA_TEXT_REG[11] = 0;
    SHA_TEXT_REG[12] = 0;
    SHA_TEXT_REG[13] = 0;
    SHA_TEXT_REG[14] = 0;

    // Word 15: bit length 256 = 0x100 → bytes [0x00,0x00,0x01,0x00] → LE uint32
    SHA_TEXT_REG[15] = 0x00010000;

    // Start fresh (H0 auto-seeded)
    REG_WRITE(SHA_START_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}

    // Early reject: read only SHA_H[7] (MSB word in LE convention) for target comparison
    uint32_t h7 = __builtin_bswap32(SHA_H_REG[7]);
    if (h7 > target_word0) {
        return h7;
    }

    // Potential hit — read full digest (bswap to standard)
    for (int i = 0; i < 7; i++) {
        state[i] = __builtin_bswap32(SHA_H_REG[i]);
    }
    state[7] = h7;
    return h7;
}

// --- Phase 3: Optimized zero-bswap HW-format pipeline ---

static const uint32_t H0_hw[8] = {
    0x67e6096a, 0x85ae67bb, 0x72f36e3c, 0x3af54fa5,
    0x7f527e51, 0x8c68059b, 0xabd9831f, 0x19cde05b,
};

IRAM_ATTR void sha256_hw_midstate(const uint8_t header_block1[64],
                                   uint32_t midstate_hw[8])
{
    // Write H0 in HW format (no bswap needed — H0_hw is already HW-native)
    for (int i = 0; i < 8; i++) {
        SHA_H_REG[i] = H0_hw[i];
    }

    // Write block1 to SHA_TEXT (raw LE cast, no bswap)
    const uint32_t *w = (const uint32_t *)header_block1;
    for (int i = 0; i < 16; i++) {
        SHA_TEXT_REG[i] = w[i];
    }

    // Start fresh (H0 auto-seeded but we just wrote it explicitly)
    REG_WRITE(SHA_START_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}

    // Read result WITHOUT bswap — midstate_hw stays in HW format
    for (int i = 0; i < 8; i++) {
        midstate_hw[i] = SHA_H_REG[i];
    }
}

IRAM_ATTR uint32_t sha256_hw_mine_nonce(const uint32_t midstate_hw[8],
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

    // Early reject: check upper 16 bits of SHA_H[7]
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

// --- Debug: Verify SHA_TEXT preservation ---

#ifdef STICKMINER_DEBUG
#include "esp_log.h"
#include <inttypes.h>

bool sha256_hw_verify_text_preserved(void)
{
    static const char *TAG = "sha256_hw";
    uint32_t original[16];
    bool preserved = true;

    // Write known pattern
    for (int i = 0; i < 16; i++) {
        original[i] = 0xDEAD0000 | i;
        SHA_TEXT_REG[i] = original[i];
    }

    // Trigger SHA operation
    REG_WRITE(SHA_START_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}

    // Check preservation
    for (int i = 0; i < 16; i++) {
        uint32_t actual = SHA_TEXT_REG[i];
        if (actual != original[i]) {
            ESP_LOGW(TAG, "SHA_TEXT[%d] modified: wrote 0x%08" PRIx32 ", read 0x%08" PRIx32,
                     i, original[i], actual);
            preserved = false;
        }
    }

    if (preserved) {
        ESP_LOGI(TAG, "SHA_TEXT registers preserved after SHA_START");
    } else {
        ESP_LOGW(TAG, "SHA_TEXT registers NOT preserved — per-nonce write reduction not possible");
    }

    return preserved;
}
#endif

#endif // ESP_PLATFORM
