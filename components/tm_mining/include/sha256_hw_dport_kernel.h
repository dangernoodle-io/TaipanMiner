#pragma once
/*
 * TA-367 Phase B+C: pipelined fill-during-compute hot loop kernel (inline).
 * TA-369 D.1: partial block-1 fill + persistent TEXT[10..15] refresh.
 *
 * Body lives in this header so mine_nonce_range can actually inline it
 * across translation units (always_inline alone doesn't cross TU boundaries
 * without LTO). Caller must hold sha256_hw_dport_acquire().
 *
 * Per-nonce sequence (classic ESP32 / DPORT bus):
 *   1. Fill block 1 — only TEXT[0..9] this iter (header[0..39]).
 *      TEXT[10..15] (header[40..63]) are constant across all nonces of
 *      a single job; preloaded once in sha256_hw_dport_kernel_init() and
 *      refreshed at end of prior nonce's block-3 compute (or job start).
 *   2. SHA_START — peripheral computes block 1.
 *   3. OVERLAP: fill TEXT[0..15] = block 2 (header[64..79] + nonce + pad)
 *      while peripheral works on block 1 (fill=117cyc, compute=87cyc).
 *   4. Wait BUSY=0; SHA_CONTINUE block 2 — peripheral computes block 2.
 *      No overlap during block-2 compute (writes to TEXT[*] corrupt result).
 *   5. Wait BUSY=0; SHA_LOAD digest1 → TEXT[0..7].
 *   6. Wait BUSY=0; fill TEXT[8] = 0x80000000 and TEXT[15] = 0x100.
 *      TEXT[9..14] retain zeros from step 3 (probe-confirmed across LOAD).
 *   7. SHA_START block 3 (second hash on the digest1 padded to 64 bytes).
 *   8. NEW (TA-369 D.1): refresh TEXT[10..15] = header[40..63] from preload
 *      during block-3 compute wait (87 cyc window; 6 writes ~45 cyc, hidden).
 *   9. Wait BUSY=0; SHA_LOAD final digest → TEXT[0..7].
 *  10. Early-reject on TEXT[7] vs target_word0_max.
 *  11. Full 8-word readback, then mining_hash_from_state for the canonical
 *      32-byte hash.
 *
 *  TA-4xx: the ENTIRE per-nonce sequence (steps 1-11) must run with the
 *  other CPU stalled (DPORT_STALL_OTHER_CPU_START()/END(), IPC-ISR
 *  cross-core stall, esp_hw_support/dport_access.h) — not just the
 *  erratum's stale-read workaround. Root cause: the other core's
 *  concurrent AHB/DPORT-bus traffic can clobber SHA TEXT[] between our
 *  first fill and final readback, corrupting the digest outright (not
 *  just a stale/zero read). A narrower stall over steps 7-11 only
 *  (block-3 start through readback) measured zero prefilter hits over
 *  ~40 min at diff 0.01 (expected ~11) — widened to the full sequence
 *  since the clobber can evidently also land during blocks 1-2.
 *
 *  TA-5xx: bracketing the stall INSIDE this per-nonce kernel cost ~2us/
 *  nonce in stall/unstall transition overhead (373->215 kH/s). The stall
 *  is now the CALLER's responsibility (mine_nonce_range() batches
 *  TM_MINING_STALL_BATCH consecutive kernel calls under one
 *  START()/END() pair) so the transition cost amortizes over the batch.
 *  This kernel is stall-FREE: callers MUST hold the stall across the
 *  entire call for correctness. Raw _DPORT_REG_READ remains race-free
 *  here only because the caller guarantees that.
 */

#ifdef ESP_PLATFORM
#if CONFIG_IDF_TARGET_ESP32

#include <stdint.h>
#include <stdbool.h>
#include "soc/dport_access.h"
#include "soc/hwcrypto_reg.h"
#include "dport_access.h"  /* esp_hw_support: DPORT_STALL_OTHER_CPU_START/END */
#include "work.h"

/* TA-369 D.1: preload TEXT[10..15] from header[40..63] once per job.
 * Call from hw_prepare_job after acquiring SHA lock. */
static inline __attribute__((always_inline))
void sha256_hw_dport_kernel_init(const uint8_t header_80[80])
{
    volatile uint32_t *sha_text = (volatile uint32_t *)(SHA_TEXT_BASE);
    const uint32_t *header_words = (const uint32_t *)header_80;
    sha_text[10] = __builtin_bswap32(header_words[10]);
    sha_text[11] = __builtin_bswap32(header_words[11]);
    sha_text[12] = __builtin_bswap32(header_words[12]);
    sha_text[13] = __builtin_bswap32(header_words[13]);
    sha_text[14] = __builtin_bswap32(header_words[14]);
    sha_text[15] = __builtin_bswap32(header_words[15]);
}

static inline __attribute__((always_inline))
bool sha256_hw_dport_kernel(const uint8_t header_80[80],
                            uint32_t nonce,
                            uint32_t target_word0_max,
                            uint8_t hash_out[32])
{
    volatile uint32_t *sha_text     = (volatile uint32_t *)(SHA_TEXT_BASE);
    volatile uint32_t *sha_start    = (volatile uint32_t *)(SHA_256_START_REG);
    volatile uint32_t *sha_continue = (volatile uint32_t *)(SHA_256_CONTINUE_REG);
    volatile uint32_t *sha_load     = (volatile uint32_t *)(SHA_256_LOAD_REG);

    const uint32_t *header_words   = (const uint32_t *)header_80;
    const uint32_t *block2_partial = (const uint32_t *)(header_80 + 64);

    /* TA-5xx: caller (mine_nonce_range()) holds DPORT_STALL_OTHER_CPU_START()
     * across a batch of these calls — see file-header comment. This kernel
     * no longer brackets its own stall. */

    /* 1. Fill block 1 — only TEXT[0..9] (header[0..39]).
     *    TEXT[10..15] (header[40..63]) are constant per job;
     *    preloaded in sha256_hw_dport_kernel_init() and refreshed
     *    at end of prior nonce's block-3 compute. */
    sha_text[0]  = __builtin_bswap32(header_words[0]);
    sha_text[1]  = __builtin_bswap32(header_words[1]);
    sha_text[2]  = __builtin_bswap32(header_words[2]);
    sha_text[3]  = __builtin_bswap32(header_words[3]);
    sha_text[4]  = __builtin_bswap32(header_words[4]);
    sha_text[5]  = __builtin_bswap32(header_words[5]);
    sha_text[6]  = __builtin_bswap32(header_words[6]);
    sha_text[7]  = __builtin_bswap32(header_words[7]);
    sha_text[8]  = __builtin_bswap32(header_words[8]);
    sha_text[9]  = __builtin_bswap32(header_words[9]);
    /* TEXT[10..15] left from prior nonce or kernel_init */

    /* 2. START block 1 */
    *sha_start = 1;

    /* 3. OVERLAP: fill block 2 during block-1 compute */
    sha_text[0]  = __builtin_bswap32(block2_partial[0]);
    sha_text[1]  = __builtin_bswap32(block2_partial[1]);
    sha_text[2]  = __builtin_bswap32(block2_partial[2]);
    sha_text[3]  = __builtin_bswap32(nonce);
    sha_text[4]  = 0x80000000;
    sha_text[5]  = 0x00000000;
    sha_text[6]  = 0x00000000;
    sha_text[7]  = 0x00000000;
    sha_text[8]  = 0x00000000;
    sha_text[9]  = 0x00000000;
    sha_text[10] = 0x00000000;
    sha_text[11] = 0x00000000;
    sha_text[12] = 0x00000000;
    sha_text[13] = 0x00000000;
    sha_text[14] = 0x00000000;
    sha_text[15] = 0x00000280;

    /* 4. Wait block-1. Other CPU is stalled for the whole sequence now, so
     *    a raw _DPORT_REG_READ is race-free (no cross-core interleaving to
     *    guard against). */
    while (_DPORT_REG_READ(SHA_256_BUSY_REG)) {}
    *sha_continue = 1;

    /* 5. Wait block-2, LOAD digest1 into TEXT[0..7] */
    while (_DPORT_REG_READ(SHA_256_BUSY_REG)) {}
    *sha_load = 1;

    /* 6. Wait LOAD, fill double-pad. Only TEXT[8] and TEXT[15] need refresh;
     *    TEXT[9..14] retain zeros from step 3. */
    while (_DPORT_REG_READ(SHA_256_BUSY_REG)) {}
    sha_text[8]  = 0x80000000;
    sha_text[15] = 0x00000100;

    /* 7. START block 3 */
    *sha_start = 1;

    /* 8. NEW (TA-369 D.1): refresh TEXT[10..15] = header[40..63]
     *    during block-3 compute wait (87 cyc; 6 writes ~45 cyc, hidden).
     *    Safe because the peripheral snapshots TEXT at START time. */
    sha_text[10] = __builtin_bswap32(header_words[10]);
    sha_text[11] = __builtin_bswap32(header_words[11]);
    sha_text[12] = __builtin_bswap32(header_words[12]);
    sha_text[13] = __builtin_bswap32(header_words[13]);
    sha_text[14] = __builtin_bswap32(header_words[14]);
    sha_text[15] = __builtin_bswap32(header_words[15]);

    /* 9. Wait block-3, LOAD final digest */
    while (_DPORT_REG_READ(SHA_256_BUSY_REG)) {}
    *sha_load = 1;

    /* 10. Wait LOAD; early-reject on word 7. The other CPU is stalled for
     *     this whole per-nonce sequence, so a raw _DPORT_REG_READ is safe
     *     here (no cross-core race to guard against inside the stall
     *     window).
     *
     *     TA-396: SHA_TEXT[7] holds canonical H[7] in big-endian register form;
     *     the TRUE most-significant 32-bit word of the PoW value (the chunk
     *     meets_target compares first) is bswap32(H[7]). target_word0_max is
     *     packed in that same true-MSB order, so this is an EXACT top-32-bit
     *     target compare — passing only genuine candidates. */
    while (_DPORT_REG_READ(SHA_256_BUSY_REG)) {}
    uint32_t word7 = __builtin_bswap32(_DPORT_REG_READ(SHA_TEXT_BASE + 7 * 4));
    if (word7 > target_word0_max) {
        return false;
    }

    /* 11. Full readback — other CPU still stalled from above, so a plain
     * volatile read sequence is race-free.
     *
     * Classic ESP32 SHA TEXT registers are in CANONICAL SHA-256 H[] order:
     *   SHA_TEXT[0] = canonical H[0] (MSB word)
     *   SHA_TEXT[7] = canonical H[7] (LSB word)
     * Read state[i] = SHA_TEXT[i] so mining_hash_from_state writes state[0] BE
     * at hash_out[0..3] = canonical MSB word bytes = raw SHA256d byte output.
     * meets_target() uses LE-internal convention (byte[31]=LSB of H[7], which is
     * the least-significant byte of the 256-bit value = raw SHA256d byte[31] → 0
     * for valid Bitcoin hashes). */
    uint32_t state[8];
    state[0] = _DPORT_REG_READ(SHA_TEXT_BASE + 0 * 4);
    state[1] = _DPORT_REG_READ(SHA_TEXT_BASE + 1 * 4);
    state[2] = _DPORT_REG_READ(SHA_TEXT_BASE + 2 * 4);
    state[3] = _DPORT_REG_READ(SHA_TEXT_BASE + 3 * 4);
    state[4] = _DPORT_REG_READ(SHA_TEXT_BASE + 4 * 4);
    state[5] = _DPORT_REG_READ(SHA_TEXT_BASE + 5 * 4);
    state[6] = _DPORT_REG_READ(SHA_TEXT_BASE + 6 * 4);
    state[7] = _DPORT_REG_READ(SHA_TEXT_BASE + 7 * 4);

    mining_hash_from_state(state, hash_out);
    return true;
}

#endif /* CONFIG_IDF_TARGET_ESP32 */
#endif /* ESP_PLATFORM */
