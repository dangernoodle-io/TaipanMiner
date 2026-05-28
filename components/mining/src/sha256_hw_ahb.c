#include "sdkconfig.h"

#ifdef ESP_PLATFORM

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3

#include "sha256_hw_ahb.h"
#include "sha256.h"
#include "mining.h"
#include "work.h"
#include "bb_core.h"
#include "soc/hwcrypto_reg.h"
#include "soc/soc.h"
#include "esp_crypto_lock.h"
#include "esp_attr.h"
#include "bb_log.h"
#include "esp_crypto_periph_clk.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include <inttypes.h>

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
// NOTE: The ESP32-S3 SHA peripheral was historically assumed to overwrite
// SHA_TEXT during W[] schedule expansion. The TA-320 boot probe
// (sha256_hw_verify_text_preserved) shows SHA_TEXT IS preserved across
// SHA_START / SHA_CONTINUE on this silicon. TA-320b uses this to prime
// the persistent zero slots (M[9..14]) once per job in
// sha256_hw_pipeline_prep() and skip them in sha256_hw_mine_nonce —
// measured +20% mining hashrate on tdongle-S3 (220 → 264 kH/s).

static const char *TAG = "sha256_hw";

// Drain a stale SHA_BUSY=1 left by a soft-restart while another caller (typically
// mbedTLS HMAC during esp_https_ota TLS) was mid-operation. Lock state evaporates
// at esp_restart but the peripheral's BUSY latch persists; the first hot-loop spin
// then never returns. Bounded so a permanently-wedged peripheral surfaces as a
// recoverable error rather than a hang.
static void sha256_hw_drain_busy(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!REG_READ(SHA_BUSY_REG)) return;
    }
    bb_log_e(TAG, "SHA peripheral BUSY did not clear after 100k polls");
}

void sha256_hw_acquire(void)
{
    esp_crypto_sha_aes_lock_acquire();
    esp_crypto_sha_enable_periph_clk(true);
    sha256_hw_drain_busy();
    REG_WRITE(SHA_MODE_REG, 2);  // SHA-256
}

void sha256_hw_release(void)
{
    esp_crypto_sha_enable_periph_clk(false);
    esp_crypto_sha_aes_lock_release();
}

void sha256_hw_init(void)
{
    sha256_hw_acquire();
    // TA-320f: boot probes (verify_text_preserved, canaries, microbench,
    // profile_hotloop) moved to sha256_hw_ahb_boot_probes(), called from
    // mining_run_self_tests() in app_main so the boot log shows them
    // before "Returned from app_main()" rather than after, interleaved
    // with mining task startup.

#ifdef TAIPANMINER_DEBUG
    sha256_hw_bench_pass2(100000);
#endif

    sha256_hw_release();
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

// TA-320b: prime persistent SHA_TEXT zero-slots once per job. Indices
// [9..14] are 0 in both the pass1 (block2 tail) and pass2 (digest tail)
// padding layouts, and the SHA peripheral preserves them across
// SHA_START / SHA_CONTINUE (verified by sha256_hw_verify_text_preserved
// at boot). Skipping the per-nonce writes saves 12 stores per nonce.
void sha256_hw_pipeline_prep(void)
{
    // TA-320b: persistent zeros at TEXT[9..14] (block2/digest tail padding,
    // identical for both passes).
    SHA_TEXT_REG[9]  = 0;
    SHA_TEXT_REG[10] = 0;
    SHA_TEXT_REG[11] = 0;
    SHA_TEXT_REG[12] = 0;
    SHA_TEXT_REG[13] = 0;
    SHA_TEXT_REG[14] = 0;
    // TA-320f: seed pass1's TEXT[4..8, 15] for the first nonce. Subsequent
    // nonces have these restored during the prior nonce's pass2_wait
    // overlap window. Without this prime, the first nonce after a
    // prepare_job would compute against stale TEXT slots from whatever
    // ran on the peripheral before (mbedTLS, prior job, etc).
    SHA_TEXT_REG[4]  = 0x00000080;
    SHA_TEXT_REG[5]  = 0;
    SHA_TEXT_REG[6]  = 0;
    SHA_TEXT_REG[7]  = 0;
    SHA_TEXT_REG[8]  = 0;
    SHA_TEXT_REG[15] = 0x80020000;
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

/* ---------------------------------------------------------------------------
 * Known-vector self-test: SHA-256("abc")
 * Returns BB_OK on PASS, BB_ERR_INVALID_STATE on FAIL.
 * ---------------------------------------------------------------------------
 */
bb_err_t sha256_hw_ahb_self_test(void)
{
    /* Known-vector test: SHA-256("abc").
     * The abc_block has only one 512-bit block, so sha256_hw_midstate()
     * produces the complete digest. */
    uint8_t abc_block[64];
    sha256_build_abc_block(abc_block);

    /* sha256_hw_midstate writes/reads SHA peripheral registers; the periph
     * clock must be on and the AES/SHA lock held, otherwise AHB writes are
     * silently dropped and downstream code paths see undefined state. */
    sha256_hw_acquire();
    uint32_t digest_hw[8];
    sha256_hw_midstate(abc_block, digest_hw);
    sha256_hw_release();

    /* Convert HW-format digest (each word bswapped) to canonical byte form
     * for the shared comparison helper. */
    uint8_t digest_bytes[32];
    uint32_t state[8];
    for (int i = 0; i < 8; i++) state[i] = __builtin_bswap32(digest_hw[i]);
    mining_hash_from_state(state, digest_bytes);
    bb_err_t midstate_result = sha256_check_abc_vector("ahb", digest_bytes);
    if (midstate_result != BB_OK) {
        return midstate_result;
    }

    return BB_OK;
}

/* ---------------------------------------------------------------------------
 * SW-vs-HW lockstep self-test for the S3/S2/C3 SHA hot loop.
 *
 * Mirrors sha256_hw_dport_self_test_lockstep() structure. Runs N_LOCKSTEP_ITERS
 * nonces from a fixed starting point. For each nonce:
 *   - HW: sha256_hw_mine_nonce() on synthetic midstate + block2 tail.
 *   - SW: sha256d (sha256_transform twice) on the same logical inputs.
 *
 * Byte-order: both HW and SW compare h7_raw directly (both in HW-native format),
 * and full digest on potential hits. memcmp is not used — comparison is per-word.
 *
 * Caller MUST hold sha256_hw_acquire() — this function calls pipeline_prep
 * internally but assumes the lock is already held on entry.
 * Returns BB_OK on PASS, BB_ERR_INVALID_STATE on first mismatch.
 * ---------------------------------------------------------------------------
 */
#define AHB_LOCKSTEP_ITERS 1000

bb_err_t sha256_hw_ahb_self_test_lockstep(uint32_t iters)
{
    /* Synthetic but well-defined inputs. midstate must be a plausible
     * SHA-256 mid-state shape (any 8 words work for correctness tests
     * because we compare HW vs SW with the same inputs). */
    const uint32_t midstate_canonical[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    /* HW backend stores midstate in HW word order (bswap of canonical). */
    uint32_t midstate_hw[8];
    for (int i = 0; i < 8; i++) {
        midstate_hw[i] = __builtin_bswap32(midstate_canonical[i]);
    }
    /* block2 first 12 bytes = three uint32_t words; the rest is padding
     * + bit-length, handled by sha256_hw_mine_nonce internally. */
    const uint32_t block2_words[3] = { 0xdeadbeef, 0x12345678, 0xcafebabe };

    bb_log_i(TAG, "lockstep self-test: %lu nonces", (unsigned long)iters);

    sha256_hw_pipeline_prep();
    for (uint32_t nonce = 0x10000000; nonce < 0x10000000 + iters; nonce++) {
        uint32_t hw_digest[8] = {0};
        uint32_t hw_h7 = sha256_hw_mine_nonce(midstate_hw, block2_words,
                                               nonce, hw_digest);

        /* SW reference: rebuild block2 in the byte sequence the HW
         * peripheral actually sees. SHA_TEXT_REG[i] = X stores LE bytes
         * of X to MMIO; the peripheral reads each word position as
         * big-endian for SHA. So a word value X feeds SHA the byte
         * sequence (X & 0xff, (X>>8)&0xff, (X>>16)&0xff, (X>>24)&0xff).
         * That's a straight LE byte copy of the word — no bswap. */
        uint8_t block2_bytes[64] = {0};
        for (int i = 0; i < 3; i++) {
            uint32_t w = block2_words[i];
            block2_bytes[i*4 + 0] =  w        & 0xff;
            block2_bytes[i*4 + 1] = (w >> 8)  & 0xff;
            block2_bytes[i*4 + 2] = (w >> 16) & 0xff;
            block2_bytes[i*4 + 3] = (w >> 24) & 0xff;
        }
        block2_bytes[12] =  nonce        & 0xff;
        block2_bytes[13] = (nonce >> 8)  & 0xff;
        block2_bytes[14] = (nonce >> 16) & 0xff;
        block2_bytes[15] = (nonce >> 24) & 0xff;
        block2_bytes[16] = 0x80;       /* SHA padding */
        block2_bytes[62] = 0x02;       /* bit-length high byte (640 bits) */
        block2_bytes[63] = 0x80;       /* bit-length low byte */

        uint32_t sw_state[8];
        for (int i = 0; i < 8; i++) sw_state[i] = midstate_canonical[i];
        sha256_transform(sw_state, block2_bytes);

        /* Pack pass1 digest into block3 with SHA-256d padding. */
        uint8_t block3_bytes[64] = {0};
        for (int i = 0; i < 8; i++) {
            block3_bytes[i*4 + 0] = (sw_state[i] >> 24) & 0xff;
            block3_bytes[i*4 + 1] = (sw_state[i] >> 16) & 0xff;
            block3_bytes[i*4 + 2] = (sw_state[i] >> 8)  & 0xff;
            block3_bytes[i*4 + 3] =  sw_state[i]        & 0xff;
        }
        block3_bytes[32] = 0x80;
        block3_bytes[62] = 0x01;       /* bit-length high (256 bits) */
        block3_bytes[63] = 0x00;
        uint32_t sw_final[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
        };
        sha256_transform(sw_final, block3_bytes);
        uint32_t sw_h7 = __builtin_bswap32(sw_final[7]);

        if (hw_h7 != sw_h7) {
            bb_log_e(TAG, "lockstep mine_nonce mismatch @ nonce=0x%08" PRIx32
                     ": hw_h7=0x%08" PRIx32 " sw_h7=0x%08" PRIx32,
                     nonce, hw_h7, sw_h7);
            return BB_ERR_INVALID_STATE;
        }
        /* If candidate, also compare full digest. */
        if ((hw_h7 >> 16) == 0) {
            for (int i = 0; i < 7; i++) {
                uint32_t sw_w = __builtin_bswap32(sw_final[i]);
                if (hw_digest[i] != sw_w) {
                    bb_log_e(TAG, "lockstep mine_nonce digest mismatch @ nonce=0x%08"
                             PRIx32 " word=%d: hw=0x%08" PRIx32 " sw=0x%08"
                             PRIx32, nonce, i, hw_digest[i], sw_w);
                    return BB_ERR_INVALID_STATE;
                }
            }
        }
    }

    bb_log_i(TAG, "lockstep self-test: PASS (%lu nonces)", (unsigned long)iters);
    return BB_OK;
}

// SHA_TEXT-persistence probe: writes 0xDEAD000_i to M[0..15], fires SHA_START,
// reads back. If preserved, the per-nonce zero-padding writes in
// sha256_hw_mine_nonce could be skipped (TA-320 sub-task b). If not preserved
// (the historically empirically-observed case on S3), the rewrite cost is
// unavoidable. Run at boot so the assumption is visible in the log every time.
bool sha256_hw_verify_text_preserved(void)
{
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
    int first_modified = -1;
    for (int i = 0; i < 16; i++) {
        uint32_t actual = SHA_TEXT_REG[i];
        if (actual != original[i]) {
            if (first_modified < 0) first_modified = i;
            preserved = false;
        }
    }

    if (preserved) {
        bb_log_i(TAG, "SHA_TEXT preserved after SHA_START — pad-write reduction possible");
    } else {
        bb_log_i(TAG, "SHA_TEXT NOT preserved (first mismatch at M[%d]) — per-nonce pad-write required",
                 first_modified);
    }

    return preserved;
}

#include "esp_timer.h"
#include <inttypes.h>

// SHA TEXT-overlap canary (TA-320a): determines whether the SHA peripheral
// snapshots SHA_TEXT at trigger time (SHA_START) or reads it continuously
// during compute. If the former, writing the next pass's TEXT registers
// during the current pass's busy-wait window is safe — the largest TA-320
// optimization that doesn't require hand asm. NerdMiner-AxeHub does this
// via an equivalent canary (axehub_sha_fast_overlap_canary).
//
// Method: compute reference H(input). Reset, write input, trigger
// SHA_START, then while busy overwrite SHA_TEXT[0] with garbage. Wait,
// read H. If H == reference → snapshot at trigger → overlap safe.
//
// Run at boot with the result cached so /api/info exposes it for fleet
// comparison.
bool sha256_hw_overlap_canary(void)
{
    static const uint32_t test_msg[16] = {
        0xfeedface, 0xcafebabe, 0xdeadbeef, 0x01234567,
        0x89abcdef, 0x12345678, 0x9abcdef0, 0x0fedcba9,
        0x00000080, 0, 0, 0, 0, 0, 0, 0x00010000
    };
    uint32_t reference[8];
    uint32_t observed[8];

    // Reference: clean SHA_START with no mid-compute interference.
    for (int j = 0; j < 16; j++) SHA_TEXT_REG[j] = test_msg[j];
    REG_WRITE(SHA_START_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}
    for (int j = 0; j < 8; j++) reference[j] = SHA_H_REG[j];

    // Trial: SHA_START, then immediately corrupt SHA_TEXT[0] before busy clears.
    for (int j = 0; j < 16; j++) SHA_TEXT_REG[j] = test_msg[j];
    REG_WRITE(SHA_START_REG, 1);
    SHA_TEXT_REG[0] = 0xBADC0FFE;  // corrupt TEXT mid-compute
    while (REG_READ(SHA_BUSY_REG)) {}
    for (int j = 0; j < 8; j++) observed[j] = SHA_H_REG[j];

    bool safe = true;
    for (int j = 0; j < 8; j++) {
        if (observed[j] != reference[j]) { safe = false; break; }
    }

    if (safe) {
        bb_log_i(TAG, "SHA TEXT-overlap canary: SAFE — peripheral snapshots TEXT at trigger; overlap-during-busy-wait possible");
    } else {
        bb_log_i(TAG, "SHA TEXT-overlap canary: UNSAFE — peripheral reads TEXT during compute; cannot overlap");
    }

    mining_set_sha_overlap_safe(safe);
    return safe;
}

// SHA H-write-during-compute canary (TA-320a). Same idea as the TEXT
// overlap canary, but tests whether writing SHA_H mid-compute corrupts
// the digest. If safe, the next-pass H=midstate reload can move into the
// previous pass's busy-wait window — material for cross-nonce pipelining.
//
// Method: clean reference H(input). Then trigger SHA_START and overwrite
// SHA_H[0] with garbage during busy. Compare. NerdMiner-AxeHub uses the
// equivalent (axehub_sha_fast_hwrite_canary).
bool sha256_hw_hwrite_canary(void)
{
    static const uint32_t test_msg[16] = {
        0x01234567, 0x89abcdef, 0xfedcba98, 0x76543210,
        0x0a0b0c0d, 0x10203040, 0x55aa55aa, 0xa5a5a5a5,
        0x00000080, 0, 0, 0, 0, 0, 0, 0x00010000
    };
    uint32_t reference[8];
    uint32_t observed[8];

    for (int j = 0; j < 16; j++) SHA_TEXT_REG[j] = test_msg[j];
    REG_WRITE(SHA_START_REG, 1);
    while (REG_READ(SHA_BUSY_REG)) {}
    for (int j = 0; j < 8; j++) reference[j] = SHA_H_REG[j];

    for (int j = 0; j < 16; j++) SHA_TEXT_REG[j] = test_msg[j];
    REG_WRITE(SHA_START_REG, 1);
    SHA_H_REG[0] = 0xDEC0DE01;  // corrupt H mid-compute
    while (REG_READ(SHA_BUSY_REG)) {}
    for (int j = 0; j < 8; j++) observed[j] = SHA_H_REG[j];

    bool safe = true;
    for (int j = 0; j < 8; j++) {
        if (observed[j] != reference[j]) { safe = false; break; }
    }

    if (safe) {
        bb_log_i(TAG, "SHA H-write canary: SAFE — H reload during compute does not corrupt digest");
    } else {
        bb_log_i(TAG, "SHA H-write canary: UNSAFE — H reload during compute corrupts digest");
    }

    mining_set_sha_hwrite_safe(safe);
    return safe;
}

// TA-320f: boot probes bundle. Called from mining_run_self_tests() in
// app_main so output appears before "Returned from app_main()" rather
// than concurrently with mining task startup. Skipped on ASIC builds —
// the SHA peripheral isn't on the hashing path there. Acquires the
// peripheral around the bundle; safe to call once at boot.
void sha256_hw_ahb_boot_probes(void)
{
#ifndef ASIC_CHIP
    sha256_hw_acquire();
    sha256_hw_verify_text_preserved();
    sha256_hw_overlap_canary();
    sha256_hw_hwrite_canary();
    sha256_hw_profile_hotloop(1000);
    bb_err_t rc = sha256_hw_ahb_self_test_lockstep(1000);
    if (rc != BB_OK) {
        bb_log_e(TAG, "S3 lockstep self-test FAILED — SHA hot loop digest diverges from SW SHA256d");
    }
    sha256_hw_release();
#endif
}

// Boot-time micro-bench (TA-337): 1000 SHA peripheral ops, log a single
// "HW SHA microbench: N us/op (~M kH/s)" line so per-device + per-firmware
// throughput regressions are visible in every boot log without rebuilding
// -debug. Mining does 2 SHA ops per nonce, so kH/s = 500 / us_per_op.
// Budget: ~5ms boot cost (1000 iters * ~5us).
void sha256_hw_microbench(void)
{
    static const uint32_t test_msg[16] = {
        0x12345678, 0x9abcdef0, 0x12345678, 0x9abcdef0,
        0x12345678, 0x9abcdef0, 0x12345678, 0x9abcdef0,
        0x00000080, 0, 0, 0, 0, 0, 0, 0x00010000
    };
    const uint32_t iterations = 1000;

    int64_t start = esp_timer_get_time();
    for (uint32_t i = 0; i < iterations; i++) {
        for (int j = 0; j < 16; j++) {
            SHA_TEXT_REG[j] = test_msg[j];
        }
        REG_WRITE(SHA_START_REG, 1);
        while (REG_READ(SHA_BUSY_REG)) {}
    }
    int64_t elapsed = esp_timer_get_time() - start;

    double us_per_op = (double)elapsed / iterations;
    double khs = 500.0 / us_per_op;  // 2 SHA ops per nonce
    bb_log_i(TAG, "HW SHA microbench: %.2f us/op (~%.0f kH/s peripheral ceiling)",
             us_per_op, khs);
    mining_set_sha_microbench(us_per_op, khs);
}

// TA-320f: per-phase cycle profile of the per-nonce hot loop.
// Mirrors sha256_hw_mine_nonce body exactly but with esp_cpu_get_cycle_count()
// brackets around each phase. Synthetic inputs (zeros) — measures timing only.
// Uses asm volatile + ordering barriers so the compiler doesn't reorder the
// reads relative to the MMIO stores.
void sha256_hw_profile_hotloop(uint32_t iterations)
{
    static const uint32_t midstate_zero[8] = {0};
    static const uint32_t block2_zero[3] = {0};
    uint32_t digest_hw[8];

    uint64_t pass1_setup_cyc = 0;
    uint64_t pass1_wait_cyc  = 0;
    uint64_t pass2_setup_cyc = 0;
    uint64_t pass2_wait_cyc  = 0;
    uint64_t reject_cyc      = 0;
    uint64_t total_cyc       = 0;

    // Prime all persistent + restore-from-overlap slots (matches
    // sha256_hw_pipeline_prep: TEXT[9..14]=0, TEXT[4..8, 15] for first pass1).
    sha256_hw_pipeline_prep();

    for (uint32_t n = 0; n < iterations; n++) {
        uint32_t t0 = esp_cpu_get_cycle_count();

        // --- pass1 setup: H + TEXT[0..3] + CONTINUE ---
        // TEXT[4..8, 15] restored during prior pass2_wait overlap.
        for (int i = 0; i < 8; i++) {
            SHA_H_REG[i] = midstate_zero[i];
        }
        SHA_TEXT_REG[0] = block2_zero[0];
        SHA_TEXT_REG[1] = block2_zero[1];
        SHA_TEXT_REG[2] = block2_zero[2];
        SHA_TEXT_REG[3] = n;
        REG_WRITE(SHA_CONTINUE_REG, 1);

        uint32_t t1 = esp_cpu_get_cycle_count();

        // --- pass1 wait + pass2 padding overlap ---
        SHA_TEXT_REG[8] = 0x00000080;
        SHA_TEXT_REG[15] = 0x00010000;
        while (REG_READ(SHA_BUSY_REG)) {}

        uint32_t t2 = esp_cpu_get_cycle_count();

        // --- pass2 setup: H -> TEXT[0..7] + START ---
        SHA_TEXT_REG[0] = SHA_H_REG[0];
        SHA_TEXT_REG[1] = SHA_H_REG[1];
        SHA_TEXT_REG[2] = SHA_H_REG[2];
        SHA_TEXT_REG[3] = SHA_H_REG[3];
        SHA_TEXT_REG[4] = SHA_H_REG[4];
        SHA_TEXT_REG[5] = SHA_H_REG[5];
        SHA_TEXT_REG[6] = SHA_H_REG[6];
        SHA_TEXT_REG[7] = SHA_H_REG[7];
        REG_WRITE(SHA_START_REG, 1);

        uint32_t t3 = esp_cpu_get_cycle_count();

        // --- pass2 wait + next pass1 padding restore overlap ---
        SHA_TEXT_REG[4] = 0x00000080;
        SHA_TEXT_REG[5] = 0;
        SHA_TEXT_REG[6] = 0;
        SHA_TEXT_REG[7] = 0;
        SHA_TEXT_REG[8] = 0;
        SHA_TEXT_REG[15] = 0x80020000;
        while (REG_READ(SHA_BUSY_REG)) {}

        uint32_t t4 = esp_cpu_get_cycle_count();

        // --- reject check: read h7, branch, conditional digest read ---
        uint32_t h7_raw = SHA_H_REG[7];
        if ((h7_raw >> 16) == 0) {
            for (int i = 0; i < 7; i++) {
                digest_hw[i] = SHA_H_REG[i];
            }
            digest_hw[7] = h7_raw;
        }

        uint32_t t5 = esp_cpu_get_cycle_count();

        pass1_setup_cyc += (uint32_t)(t1 - t0);
        pass1_wait_cyc  += (uint32_t)(t2 - t1);
        pass2_setup_cyc += (uint32_t)(t3 - t2);
        pass2_wait_cyc  += (uint32_t)(t4 - t3);
        reject_cyc      += (uint32_t)(t5 - t4);
        total_cyc       += (uint32_t)(t5 - t0);
    }
    (void)digest_hw;

    double n = (double)iterations;
    double total_per   = (double)total_cyc       / n;
    uint32_t cpu_freq = (uint32_t)esp_clk_cpu_freq();
    double khs = ((double)cpu_freq / total_per) / 1000.0;
    /* us_per_op: S3 does 2 SHA ops per nonce (pass1 midstate-continue + pass2 start). */
    double us_per_op_equiv = (1e6 * total_per / (double)cpu_freq) / 2.0;

    bb_log_i(TAG,
        "SHA hotloop profile (%" PRIu32 " iters): "
        "pass1_setup=%.0f pass1_wait=%.0f pass2_setup=%.0f pass2_wait=%.0f reject=%.0f "
        "total=%.0f cyc/nonce (~%.0f kH/s effective)",
        iterations,
        (double)pass1_setup_cyc / n,
        (double)pass1_wait_cyc  / n,
        (double)pass2_setup_cyc / n,
        (double)pass2_wait_cyc  / n,
        (double)reject_cyc      / n,
        total_per, khs);
    mining_set_sha_microbench(us_per_op_equiv, khs);
}

// --- Debug utilities ---

#ifdef TAIPANMINER_DEBUG
#include "esp_log.h"

void sha256_hw_bench_pass2(uint32_t iterations)
{
    // Prepare a fixed test block (32-byte hash + padding for second pass)
    uint32_t test_msg[16] = {
        0x12345678, 0x9abcdef0, 0x12345678, 0x9abcdef0,
        0x12345678, 0x9abcdef0, 0x12345678, 0x9abcdef0,
        0x00000080, 0, 0, 0, 0, 0, 0, 0x00010000
    };

    // Benchmark SHA_START (current approach)
    int64_t start = esp_timer_get_time();
    for (uint32_t i = 0; i < iterations; i++) {
        for (int j = 0; j < 16; j++) {
            SHA_TEXT_REG[j] = test_msg[j];
        }
        REG_WRITE(SHA_START_REG, 1);
        while (REG_READ(SHA_BUSY_REG)) {}
    }
    int64_t elapsed_start = esp_timer_get_time() - start;

    // Benchmark SHA_CONTINUE with pre-loaded H0
    start = esp_timer_get_time();
    for (uint32_t i = 0; i < iterations; i++) {
        for (int j = 0; j < 8; j++) {
            SHA_H_REG[j] = H0_hw[j];
        }
        for (int j = 0; j < 16; j++) {
            SHA_TEXT_REG[j] = test_msg[j];
        }
        REG_WRITE(SHA_CONTINUE_REG, 1);
        while (REG_READ(SHA_BUSY_REG)) {}
    }
    int64_t elapsed_continue = esp_timer_get_time() - start;

    bb_log_i(TAG, "pass2 bench (%"PRIu32" iterations):", iterations);
    bb_log_i(TAG, "  SHA_START:       %"PRId64" us (%.2f us/op)",
             elapsed_start, (double)elapsed_start / iterations);
    bb_log_i(TAG, "  SHA_CONTINUE+H0: %"PRId64" us (%.2f us/op)",
             elapsed_continue, (double)elapsed_continue / iterations);

    if (elapsed_continue < elapsed_start) {
        bb_log_i(TAG, "  CONTINUE is %.1f%% faster",
                 (1.0 - (double)elapsed_continue / elapsed_start) * 100.0);
    } else {
        bb_log_i(TAG, "  START is %.1f%% faster (or equal) — keep current approach",
                 (1.0 - (double)elapsed_start / elapsed_continue) * 100.0);
    }
}
#endif

#endif // CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3

#endif // ESP_PLATFORM
