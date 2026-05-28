#pragma once
/* AHB-bus SHA-256 hardware backend (ESP32-S3, ESP32-S2, ESP32-C3). Raw volatile-pointer access; no DPORT serialization needed. */

#ifdef ESP_PLATFORM

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3

#include <stdint.h>
#include "bb_core.h"
#include "esp_attr.h"
#include "soc/soc.h"
#include "soc/hwcrypto_reg.h"

// ESP32-S3/S2/C3: separate H and TEXT register regions
#define SHA_H_REG    ((volatile uint32_t *)SHA_H_BASE)
#define SHA_TEXT_REG ((volatile uint32_t *)SHA_TEXT_BASE)

// Enable SHA-256 hardware peripheral clock. Call once at startup.
void sha256_hw_init(void);

// Acquire/release the SHA peripheral lock + clock.
// Mining holds the lock while hashing; releases during pause so mbedTLS (TLS)
// can use the peripheral. mbedTLS blocks on acquire, so it falls back to
// software SHA only while mining holds the lock — no contention.
void sha256_hw_acquire(void);
void sha256_hw_release(void);

// Drop-in hardware replacement for sha256_transform.
// Writes state to SHA_H, bswaps block to SHA_TEXT, SHA_CONTINUE, polls, reads back.
void sha256_hw_transform(uint32_t state[8], const uint8_t block[64]);

// Like sha256_hw_transform but starts fresh (SHA_START auto-seeds H0).
// Ignores incoming state, writes result to state.
void sha256_hw_transform_start(uint32_t state[8], const uint8_t block[64]);

// --- Phase 2: Optimized mining functions ---

// Once per job: prime SHA_TEXT with block2 constants and persistent zeros.
void sha256_hw_init_job(const uint8_t block2[64]);

// TA-320b: prep persistent SHA_TEXT slots that don't change across nonces
// or across pass1/pass2 within a nonce. The TA-320 boot probe confirmed
// SHA_TEXT preserves contents across SHA_START / SHA_CONTINUE on this
// silicon, so writes to these slots only need to happen once per job
// rather than per-nonce. Indices [9..14] are always 0 in both pass1
// (block2 tail padding) and pass2 (32-byte digest tail padding).
//
// Must be called at the start of every job (hw_prepare_job), since
// mbedTLS can grab the SHA peripheral while mining yields and reset
// arbitrary register state.
void sha256_hw_pipeline_prep(void);

// --- Phase 3: Optimized zero-bswap HW-format pipeline ---

// Compute midstate in HW-native format (no bswap on readback).
// Call once per job. Writes midstate in HW word order to midstate_hw.
void sha256_hw_midstate(const uint8_t header_block1[64],
                        uint32_t midstate_hw[8]);

// Known-vector self-test: SHA-256("abc")
// Returns BB_OK on PASS, BB_ERR_INVALID_STATE on FAIL
bb_err_t sha256_hw_ahb_self_test(void);

// SW-vs-HW lockstep self-test of sha256_hw_mine_nonce. Runs iters nonces
// against synthetic fixed midstate + block2 tail, comparing HW and SW digests.
// Caller MUST hold sha256_hw_acquire(); returns BB_OK on PASS,
// BB_ERR_INVALID_STATE on first mismatch.
bb_err_t sha256_hw_ahb_self_test_lockstep(uint32_t iters);

// Optimized per-nonce SHA-256d with zero-bswap pipeline.
// midstate_hw[]: midstate in HW format (from sha256_hw_midstate).
// block2_words[3]: header tail words (block2 bytes 0-11 as uint32_t[3]).
// nonce: nonce to test.
// digest_hw[8]: written only on potential hit (upper 16 bits of h7_raw are zero).
// Returns raw SHA_H_REG[7] value; caller performs full target comparison.
//
// Note: an experimental memw-collapse variant (TA-320) traded the per-store
// Xtensa memw fences for a single asm("memw") barrier before each trigger.
// Measured +0.7% kH/s on tdongle-s3 but caused ~+12°C die temp and visibly
// starved Core-0 work (HTTP/UI unresponsive). The fences function as
// implicit cooperative-yield points by draining the AHB pipeline; removing
// them lets the SHA hot loop monopolize the bus. Reverted 2026-05-01.
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

    // Write SHA_TEXT: block2_words[0-2], nonce.
    // TA-320b: indices [9..14] omitted — primed once per job by
    // sha256_hw_pipeline_prep() and preserved across SHA operations.
    // TA-320f: indices [4..8, 15] are restored during the previous nonce's
    // pass2_wait window (see below), or primed once per job for the first
    // nonce. So pass1 only writes the per-nonce / per-job slots here.
    SHA_TEXT_REG[0] = block2_words[0];
    SHA_TEXT_REG[1] = block2_words[1];
    SHA_TEXT_REG[2] = block2_words[2];
    SHA_TEXT_REG[3] = nonce;

    REG_WRITE(SHA_CONTINUE_REG, 1);

    // TA-320f: overlap pass2's TEXT[8] / TEXT[15] writes into pass1's
    // busy-wait window. Canary (sha256_hw_overlap_canary) confirmed the
    // peripheral snapshots TEXT at trigger, so writes here only affect
    // the *next* trigger (pass2 below). Saves ~10 cyc/nonce.
    SHA_TEXT_REG[8] = 0x00000080;
    SHA_TEXT_REG[15] = 0x00010000;
    while (REG_READ(SHA_BUSY_REG)) {}

    // --- Pass 2: copy SHA_H → SHA_TEXT directly (no bswap!) ---
    // TEXT[8] and TEXT[15] were already staged during pass1_wait above.
    SHA_TEXT_REG[0] = SHA_H_REG[0];
    SHA_TEXT_REG[1] = SHA_H_REG[1];
    SHA_TEXT_REG[2] = SHA_H_REG[2];
    SHA_TEXT_REG[3] = SHA_H_REG[3];
    SHA_TEXT_REG[4] = SHA_H_REG[4];
    SHA_TEXT_REG[5] = SHA_H_REG[5];
    SHA_TEXT_REG[6] = SHA_H_REG[6];
    SHA_TEXT_REG[7] = SHA_H_REG[7];

    REG_WRITE(SHA_START_REG, 1);

    // TA-320f: overlap NEXT pass1's TEXT[4..8, 15] restoration into
    // pass2's busy-wait window. After pass2 these slots hold H[4..7], 0x80,
    // and 0x00010000; restore them to the constants pass1 needs. Saves ~30
    // cyc/nonce. Same canary justifies safety: writes here affect the next
    // trigger (next nonce's pass1), not the in-flight pass2.
    SHA_TEXT_REG[4] = 0x00000080;
    SHA_TEXT_REG[5] = 0;
    SHA_TEXT_REG[6] = 0;
    SHA_TEXT_REG[7] = 0;
    SHA_TEXT_REG[8] = 0;
    SHA_TEXT_REG[15] = 0x80020000;
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

#include <stdbool.h>

// Verify that SHA_TEXT registers preserve their contents after SHA_START.
// Returns true if all 16 words are preserved, false if any are modified.
// Run at boot in every build (TA-320) so the persistence assumption is
// visible in the log; release builds use the result for diagnostic only.
bool sha256_hw_verify_text_preserved(void);

// Boot-time SHA throughput micro-bench (TA-337). Logs a single
// "HW SHA microbench: N us/op (~M kH/s peripheral ceiling)" line.
// 1000 iterations; ~5ms boot cost. Run unconditionally so per-device +
// per-firmware throughput regressions are visible without -debug rebuilds.
void sha256_hw_microbench(void);

// TA-320f: per-phase cycle profile of the per-nonce hot loop. Logs a
// single "SHA hotloop profile: pass1_setup=X pass1_wait=Y pass2_setup=Z
// pass2_wait=W reject=R total=T cycles/nonce (effective ~K kH/s)" line.
// Runs N iterations of the same body as sha256_hw_mine_nonce against
// synthetic inputs (zeros) — measures timing only, not correctness.
// Pinpoints which phase owns the wrapper overhead above the peripheral
// compute floor (786 cycles for 2 passes @ 1.64us each on 240 MHz S3).
void sha256_hw_profile_hotloop(uint32_t iterations);

// TA-320f: boot diagnostics bundle. Runs verify_text_preserved + both
// canaries + microbench + profile_hotloop. Call from app_main (via
// mining_run_self_tests) so output is visible in the boot log before
// "Returned from app_main()" rather than concurrent with mining task
// startup. Skipped on ASIC builds — peripheral isn't on the hashing
// path there. Acquires the SHA peripheral internally; safe to call
// multiple times.
void sha256_hw_ahb_boot_probes(void);

// SHA TEXT-overlap canary (TA-320a). Determines whether writing the next
// pass's SHA_TEXT registers during the current pass's busy-wait window is
// safe. Returns true if peripheral snapshots TEXT at trigger; false if it
// reads TEXT continuously during compute. Result cached via
// mining_set_sha_overlap_safe for /api/info exposure.
bool sha256_hw_overlap_canary(void);

// SHA H-write-during-compute canary (TA-320a). Returns true if writing
// SHA_H mid-compute does not corrupt the digest. Result cached via
// mining_set_sha_hwrite_safe.
bool sha256_hw_hwrite_canary(void);

#ifdef TAIPANMINER_DEBUG
// Debug benchmark comparing SHA_START vs SHA_CONTINUE+H0 for second hash pass.
// Runs iterations times for each approach and logs timing results.
void sha256_hw_bench_pass2(uint32_t iterations);
#endif

#endif // CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3

#endif // ESP_PLATFORM
