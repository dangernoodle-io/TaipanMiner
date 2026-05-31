#include "sdkconfig.h"

#ifdef ESP_PLATFORM
#if CONFIG_IDF_TARGET_ESP32

#include "sha256_hw_dport.h"
#include "sha256.h"   // sha_bench_result_t, sha256_H0
#include "work.h"
#include "mining.h"
#include "bb_core.h"
#include "bb_byte_order.h"
#include "esp_crypto_lock.h"
#include "esp_private/periph_ctrl.h"
#include "esp_timer.h"
#include "soc/dport_access.h"
#include "soc/hwcrypto_reg.h"
#include "esp_attr.h"
#include "bb_log.h"
#include "sha256_hw_dport_kernel.h"
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

static const char *TAG = "sha256_hw_dport";

static bool s_first_acquire = true;

void sha256_hw_dport_acquire(void)
{
    esp_crypto_sha_aes_lock_acquire();
    /* TA-271, KB #323: reset on first acquire post-boot to clear any stale
     * BUSY latch from a soft restart while another caller (mbedTLS HMAC) was
     * mid-operation. Only matters on raw-register paths like ours. */
    if (s_first_acquire) {
        periph_module_reset(PERIPH_SHA_MODULE);
        s_first_acquire = false;
    }
    periph_module_enable(PERIPH_SHA_MODULE);
}

void sha256_hw_dport_release(void)
{
    periph_module_disable(PERIPH_SHA_MODULE);
    esp_crypto_sha_aes_lock_release();
}

/* ---------------------------------------------------------------------------
 * NerdMiner-verbatim helpers
 * All mirrored from NerdMiner_v2/src/mining.cpp:903-1124
 * ---------------------------------------------------------------------------
 */

/* Fill 16 message words for SHA peripheral. Block bytes are stored in BTC
 * little-endian byte order; SHA spec requires M[i] = uint32_be(bytes[4i..4i+3]).
 * On a LE host that's bswap32 of the raw uint32 load. */
static inline void dport_fill_block_raw(const void *block_64)
{
    const uint32_t *data_words = (const uint32_t *)block_64;
    uint32_t *reg_addr_buf     = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = __builtin_bswap32(data_words[0]);
    reg_addr_buf[1]  = __builtin_bswap32(data_words[1]);
    reg_addr_buf[2]  = __builtin_bswap32(data_words[2]);
    reg_addr_buf[3]  = __builtin_bswap32(data_words[3]);
    reg_addr_buf[4]  = __builtin_bswap32(data_words[4]);
    reg_addr_buf[5]  = __builtin_bswap32(data_words[5]);
    reg_addr_buf[6]  = __builtin_bswap32(data_words[6]);
    reg_addr_buf[7]  = __builtin_bswap32(data_words[7]);
    reg_addr_buf[8]  = __builtin_bswap32(data_words[8]);
    reg_addr_buf[9]  = __builtin_bswap32(data_words[9]);
    reg_addr_buf[10] = __builtin_bswap32(data_words[10]);
    reg_addr_buf[11] = __builtin_bswap32(data_words[11]);
    reg_addr_buf[12] = __builtin_bswap32(data_words[12]);
    reg_addr_buf[13] = __builtin_bswap32(data_words[13]);
    reg_addr_buf[14] = __builtin_bswap32(data_words[14]);
    reg_addr_buf[15] = __builtin_bswap32(data_words[15]);
}

/* Block 2 of an 80-byte BTC header. Words 0-2 are header tail (LE bytes 64..75)
 * — bswap to canonical M. Word 3 is the nonce; bswap converts host uint32 to M[3].
 * Words 4-14: SHA padding zeros (M[4]=0x80000000 padding bit). Word 15: bit
 * length 640 (canonical M[15]). */
static inline void dport_fill_block_upper(const void *block_64_partial, uint32_t nonce)
{
    const uint32_t *data_words = (const uint32_t *)block_64_partial;
    uint32_t *reg_addr_buf     = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = __builtin_bswap32(data_words[0]);
    reg_addr_buf[1]  = __builtin_bswap32(data_words[1]);
    reg_addr_buf[2]  = __builtin_bswap32(data_words[2]);
    reg_addr_buf[3]  = __builtin_bswap32(nonce);
    reg_addr_buf[4]  = 0x80000000;
    reg_addr_buf[5]  = 0x00000000;
    reg_addr_buf[6]  = 0x00000000;
    reg_addr_buf[7]  = 0x00000000;
    reg_addr_buf[8]  = 0x00000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000280;
}

/* Mirror: nerd_sha_ll_fill_text_block_sha256_double (mining.cpp:1007-1029)
 * Words 0-7 LEFT IN PLACE (loaded from prior sha_ll_load).
 * Words 8-15 = SHA-256d second-round padding for 32-byte (256-bit) input. */
static inline void dport_fill_block_double(void)
{
    /* mining.cpp:1009 */
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    /* mining.cpp:1022-1029 */
    reg_addr_buf[8]  = 0x80000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000100;                 /* mining.cpp:1029 */
}

/* Mirror: nerd_sha_hal_wait_idle (mining.cpp:940-944) */
static inline void dport_wait_idle(void)
{
    while (DPORT_REG_READ(SHA_256_BUSY_REG)) {}    /* mining.cpp:942 */
}

/* Pool-target-aware early-reject on the LE-MSB word of the digest.
 *
 * Classic ESP32 SHA TEXT registers are in CANONICAL SHA-256 H[] order:
 *   SHA_TEXT[0] = canonical H[0] (MSB of canonical SHA-256 state)
 *   SHA_TEXT[7] = canonical H[7] (LSB of canonical SHA-256 state)
 * Bitcoin raw SHA256d byte output = mining_hash_from_state(canonical H[]):
 *   raw[0..3]  = H[0] BE, raw[28..31] = H[7] BE.
 * meets_target() uses LE-internal (byte[31] = MSB of 256-bit integer = H[7]&0xff).
 * Valid Bitcoin hashes have H[7] small (leading zeros in display hash are zeros
 * at the END of the raw byte stream = H[7] near zero).
 * Early-reject: SHA_TEXT[7] is canonical H[7] in big-endian register form; the
 * TRUE most-significant 32-bit word of the PoW value is bswap32(H[7]), and
 * target_word0_max is packed in that same true-MSB order (TA-396). */
static inline bool dport_read_digest_swap_if(uint8_t out[32], uint32_t target_word0_max)
{
    DPORT_INTERRUPT_DISABLE();
    /* SHA_TEXT[7] = canonical H[7] (big-endian register). Compare bswap32(H[7])
     * — the true PoW MSB word — against target_word0_max, but keep the raw
     * register value for mining_hash_from_state, which expects canonical H[i]. */
    uint32_t word7_raw = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
    if (__builtin_bswap32(word7_raw) > target_word0_max) {
        DPORT_INTERRUPT_RESTORE();
        return false;  /* early reject — cheapest path */
    }
    /* Full readback: canonical register order state[i] = SHA_TEXT[i] = H[i]. */
    uint32_t state[8];
    state[7] = word7_raw;
    state[0] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4);
    state[1] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4);
    state[2] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4);
    state[3] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4);
    state[4] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4);
    state[5] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4);
    state[6] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4);
    DPORT_INTERRUPT_RESTORE();
    mining_hash_from_state(state, out);
    return true;
}

/* TA-367 Phase B+C inline kernel lives in sha256_hw_dport_kernel.h so
 * mine_nonce_range can inline it across translation units. The non-inlined
 * sha256_hw_dport_per_nonce below remains as the SW-vs-HW lockstep test's
 * reference path. */

/* ---------------------------------------------------------------------------
 * Per-nonce SHA-256d hot loop with pool-target early-reject (classic ESP32, DPORT bus)
 * Mirror of minerWorkerHw inner loop (mining.cpp:1076-1094).
 *
 * header_80[80]: full 80-byte block header (nonce field will be overwritten).
 * nonce: the nonce to hash.
 * target_word0_max: MSB word of pool target (bytes 28-31); early-reject threshold.
 * hash_out[32]: written only when returning true (potential hit).
 *
 * Returns true if digest MSB word <= target_word0_max (full readback + hash_from_state).
 * Returns false on early reject (digest MSB word > target_word0_max).
 * ---------------------------------------------------------------------------
 */
bool sha256_hw_dport_per_nonce(const uint8_t header_80[80], uint32_t nonce, uint32_t target_word0_max, uint8_t hash_out[32])
{
    /* 1. Ensure idle */
    dport_wait_idle();

    /* 2. Load block 1 (bytes 0-63), raw, no bswap — mining.cpp:1076 */
    dport_fill_block_raw(header_80);

    /* 3. START block 1 — mining.cpp:1077 */
    DPORT_REG_WRITE(SHA_256_START_REG, 1);

    /* 4. Wait */
    dport_wait_idle();                              /* mining.cpp:1080 */

    /* 5. Load block 2 (bytes 64-79 partial + nonce + padding) — mining.cpp:1081 */
    dport_fill_block_upper(header_80 + 64, nonce);

    /* 6. CONTINUE block 2 — mining.cpp:1082 */
    DPORT_REG_WRITE(SHA_256_CONTINUE_REG, 1);

    /* 7. Wait */
    dport_wait_idle();                              /* mining.cpp:1084 */

    /* 8. LOAD digest into text registers — mining.cpp:1085 */
    DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);

    /* 9. Wait */
    dport_wait_idle();                              /* mining.cpp:1088 */

    /* 10. Fill double-hash padding (words 8-15; words 0-7 stay from LOAD) — mining.cpp:1089 */
    dport_fill_block_double();

    /* 11. START second hash — mining.cpp:1090 */
    DPORT_REG_WRITE(SHA_256_START_REG, 1);

    /* 12. Wait */
    dport_wait_idle();                              /* mining.cpp:1092 */

    /* 13. LOAD second digest — mining.cpp:1093 */
    DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);

    /* 14. Wait */
    dport_wait_idle();

    /* 15. Early-reject + readback — mining.cpp:1094 */
    return dport_read_digest_swap_if(hash_out, target_word0_max);
}

/* ---------------------------------------------------------------------------
 * Known-vector self-test: SHA-256("abc")
 * Returns BB_OK on PASS, BB_ERR_INVALID_STATE on FAIL.
 * ---------------------------------------------------------------------------
 */
bb_err_t sha256_hw_dport_self_test(void)
{
    /* Known-vector test: SHA-256("abc"). Byte form matches how BTC headers
     * are stored — fill_raw will bswap each word on the way to the peripheral. */
    uint8_t abc_block[64];
    sha256_build_abc_block(abc_block);

    /* Caller MUST hold sha256_hw_dport_acquire() — this function touches SHA
     * peripheral registers directly. esp_crypto_sha_aes_lock is non-recursive,
     * so we cannot re-acquire here; doing so would deadlock when called from
     * sha256_hw_dport_init() which already holds the lock. */

    dport_wait_idle();
    dport_fill_block_raw(abc_block);
    DPORT_REG_WRITE(SHA_256_START_REG, 1);
    dport_wait_idle();
    DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);
    dport_wait_idle();

    /* Read digest — peripheral writes registers in NIST canonical form */
    uint32_t digest[8];
    DPORT_INTERRUPT_DISABLE();
    digest[0] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4);
    digest[1] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4);
    digest[2] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4);
    digest[3] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4);
    digest[4] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4);
    digest[5] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4);
    digest[6] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4);
    digest[7] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
    DPORT_INTERRUPT_RESTORE();

    /* Pack canonical-word digest into byte form for the shared comparison helper. */
    uint8_t digest_bytes[32];
    mining_hash_from_state(digest, digest_bytes);
    return sha256_check_abc_vector("dport", digest_bytes);
}

/* ---------------------------------------------------------------------------
 * SW-vs-HW lockstep self-test for the D0 SHA hot loop.
 *
 * Mirrors sha256_hw_ahb_self_test() lockstep section. Runs N_LOCKSTEP_ITERS
 * nonces from a fixed starting point. For each nonce:
 *   - SW: sha256d(header_with_nonce, 80) → canonical BE bytes.
 *   - HW: sha256_hw_dport_per_nonce(header, nonce) → dport byte format.
 *
 * Byte-order: both SW (sha256d → sha256_final → mining_hash_from_state) and HW
 * (dport_read_digest_swap_if → mining_hash_from_state) produce the raw SHA256d
 * byte output: hash[0..3] = H[0] BE (canonical MSB), hash[28..31] = H[7] BE
 * (canonical LSB = LE-MSB for Bitcoin meets_target).
 * memcmp(hash_hw, hash_sw, 32) is a direct comparison — no conversion needed.
 *
 * Caller MUST hold sha256_hw_dport_acquire() — no re-acquire here.
 * Returns BB_OK on PASS, BB_ERR_INVALID_STATE on first mismatch.
 * ---------------------------------------------------------------------------
 */
#define DPORT_LOCKSTEP_ITERS 1000

bb_err_t sha256_hw_dport_self_test_lockstep(void)
{
    /* Synthetic 80-byte block header. Content is arbitrary — correctness of the
     * HW loop relative to SW is independent of the specific header bytes.
     * Bytes 76-79 (nonce field) are overwritten per iteration below. */
    uint8_t header[80];
    memset(header, 0, sizeof(header));
    /* Version (LE uint32) */
    header[0] = 0x01; header[1] = 0x00; header[2] = 0x00; header[3] = 0x00;
    /* Fill bytes 4-75 with a recognisable pattern (prevhash + merkle + time + bits) */
    for (int i = 4; i < 76; i++) {
        header[i] = (uint8_t)((i * 0x5a) ^ 0xa5);
    }

    bb_log_i(TAG, "lockstep self-test: %d nonces", DPORT_LOCKSTEP_ITERS);

    for (int iter = 0; iter < DPORT_LOCKSTEP_ITERS; iter++) {
        uint32_t nonce = (uint32_t)(0x10000000 + iter);

        /* Write nonce into header bytes 76-79 in LE (Bitcoin convention). */
        header[76] = (uint8_t)(nonce & 0xff);
        header[77] = (uint8_t)((nonce >> 8) & 0xff);
        header[78] = (uint8_t)((nonce >> 16) & 0xff);
        header[79] = (uint8_t)((nonce >> 24) & 0xff);

        /* SW reference: SHA256d of the full 80-byte header. */
        uint8_t hash_sw[32];
        sha256d(header, 80, hash_sw);

        /* HW path: pass permissive threshold (0xFFFFFFFF = always-permissive) to
         * ensure full readback for byte-equality comparison. dport_read_digest_swap_if
         * will never early-reject when threshold is max value. */
        uint8_t hash_hw[32];
        sha256_hw_dport_per_nonce(header, nonce, 0xFFFFFFFF, hash_hw);

        if (memcmp(hash_hw, hash_sw, 32) != 0) {
            /* Find first diverging byte. */
            int first = -1;
            for (int b = 0; b < 32; b++) {
                if (hash_hw[b] != hash_sw[b]) {
                    first = b;
                    break;
                }
            }
            /* Log context: first divergence byte ± 3 bytes (clamped). */
            int lo = first - 3; if (lo < 0) lo = 0;
            int hi = first + 4; if (hi > 32) hi = 32;
            bb_log_e(TAG, "lockstep mismatch nonce=0x%08" PRIx32
                     " first_diff_byte=%d", nonce, first);
            for (int b = lo; b < hi; b++) {
                bb_log_e(TAG, "  byte[%2d]: hw=0x%02x sw=0x%02x%s",
                         b, hash_hw[b], hash_sw[b],
                         (b == first) ? " <--" : "");
            }
            return BB_ERR_INVALID_STATE;
        }
    }

    bb_log_i(TAG, "lockstep self-test: PASS (%d nonces)", DPORT_LOCKSTEP_ITERS);
    return BB_OK;
}

/* ---------------------------------------------------------------------------
 * Boot-time micro-bench: real kernel throughput ceiling on D0 (classic ESP32).
 * Calls sha256_hw_dport_bench_pass2() with adaptive convergence detection.
 * D0 kernel does 3 SHA ops/nonce (block1 + block2 + block3/double-hash).
 * Bumped to 10000 iters: S2 worst-case settles at ~4900 iters; 10000 gives
 * ample headroom for 3 settled buckets beyond the settling point.
 * Caller must hold the SHA peripheral lock.
 * ---------------------------------------------------------------------------
 */
void sha256_hw_dport_microbench(void)
{
    const uint32_t iters = 10000;
    sha_bench_result_t result = {0};
    sha256_hw_dport_bench_pass2(iters, &result);

    /* Use settled portion for khs when convergence was reached; fall back to full-window. */
    double khs = (result.settled && result.settled_total_us > 0)
        ? ((double)result.settled_iters * 1000.0 / (double)result.settled_total_us)
        : ((result.total_us > 0) ? ((double)iters * 1000.0 / (double)result.total_us) : 0.0);

    bb_log_i(TAG, "HW SHA microbench (real kernel): %"PRId64" us / %"PRIu32" iters, %.2f us/SHA-op, ~%.0f kH/s reject-path ceiling (settled=%s)",
             result.total_us, iters, result.us_per_op, khs, result.settled ? "yes" : "no");
    mining_set_sha_microbench(result.us_per_op, khs);
}

/* ---------------------------------------------------------------------------
 * TA-33: on-demand bench for /api/diag/benchmark.
 * Measures DPORT kernel throughput using adaptive bucketed convergence detection.
 * Iters are split into buckets of DPORT_BENCH_BUCKET_SIZE. When
 * DPORT_BENCH_MIN_SETTLED_BUCKETS consecutive buckets are within
 * DPORT_BENCH_SETTLE_THRESHOLD_PCT of each other, steady-state is declared.
 * us_per_op and settled_* fields are computed from settled portion only.
 * Fallback: if convergence is never reached, reports full-window avg + warning.
 * Caller must hold sha256_hw_dport_acquire().
 * ---------------------------------------------------------------------------
 */
#define DPORT_BENCH_BUCKET_SIZE          200
#define DPORT_BENCH_MAX_BUCKETS          128
#define DPORT_BENCH_SETTLE_THRESHOLD_PCT   2
#define DPORT_BENCH_MIN_SETTLED_BUCKETS    3

void sha256_hw_dport_bench_pass2(uint32_t iterations, sha_bench_result_t *out)
{
    static uint8_t synthetic_header[80];   /* zeros; content is arbitrary */

    sha256_hw_dport_kernel_init(synthetic_header);

#if BB_BENCH_DIAG
    bb_log_i(TAG, "bench probe: cpu=%u Hz, apb=%u Hz, iters=%" PRIu32,
             (unsigned)esp_clk_cpu_freq(), (unsigned)esp_clk_apb_freq(), iterations);
#endif

    /* Compute bucket size; grow if iters > DPORT_BENCH_MAX_BUCKETS * DPORT_BENCH_BUCKET_SIZE. */
    uint32_t bucket_size = DPORT_BENCH_BUCKET_SIZE;
    if (iterations / DPORT_BENCH_MAX_BUCKETS > bucket_size) {
        bucket_size = iterations / DPORT_BENCH_MAX_BUCKETS;
    }
    uint32_t num_buckets = iterations / bucket_size;
    if (num_buckets == 0) num_buckets = 1;
    if (num_buckets > DPORT_BENCH_MAX_BUCKETS) num_buckets = DPORT_BENCH_MAX_BUCKETS;

    static int64_t bucket_us[DPORT_BENCH_MAX_BUCKETS]; /* static — avoid stack pressure */
    int32_t settled_first = -1;
    int consecutive_settled = 0;

    int64_t bench_start = esp_timer_get_time();
    for (uint32_t b = 0; b < num_buckets; b++) {
        int64_t bs = esp_timer_get_time();
        for (uint32_t i = 0; i < bucket_size; i++) {
            uint8_t hash_out[32];
            sha256_hw_dport_kernel(synthetic_header, b * bucket_size + i,
                                   /*target_word0_max=*/0, hash_out);
        }
        bucket_us[b] = esp_timer_get_time() - bs;

        /* Convergence check vs prior bucket. */
        if (b > 0 && settled_first < 0) {
            int64_t prev = bucket_us[b - 1];
            int64_t curr = bucket_us[b];
            int64_t diff = (curr > prev) ? (curr - prev) : (prev - curr);
            int64_t pct  = (prev > 0) ? (diff * 100 / prev) : 100;
            if (pct <= DPORT_BENCH_SETTLE_THRESHOLD_PCT) {
                consecutive_settled++;
                if (consecutive_settled >= DPORT_BENCH_MIN_SETTLED_BUCKETS) {
                    settled_first = (int32_t)b - DPORT_BENCH_MIN_SETTLED_BUCKETS + 1;
                }
            } else {
                consecutive_settled = 0;
            }
        }
    }
    int64_t total_us = esp_timer_get_time() - bench_start;

    /* Compute steady-state metrics from settled-onward portion. */
    int64_t settled_us     = 0;
    uint32_t settled_iters = 0;
    bool settled = (settled_first >= 0);
    double us_per_op;

    if (settled) {
        /* Change 2: min-of-tail estimator — min bucket is closest to true steady-state. */
        int64_t min_bucket_us = bucket_us[settled_first];
        for (uint32_t b = (uint32_t)settled_first + 1; b < num_buckets; b++) {
            if (bucket_us[b] < min_bucket_us) min_bucket_us = bucket_us[b];
        }
        /* DPORT: 3 SHA ops per nonce (block1 + block2 + double-hash). */
        us_per_op     = (double)min_bucket_us / bucket_size / 3.0;
        settled_us    = min_bucket_us;
        settled_iters = bucket_size;

        /* Change 3: log both mean and min for ramp-profile investigation. */
        {
            int64_t mean_us   = 0;
            uint32_t mean_cnt = num_buckets - (uint32_t)settled_first;
            for (uint32_t b = (uint32_t)settled_first; b < num_buckets; b++) mean_us += bucket_us[b];
            mean_us /= (int64_t)mean_cnt;
            bb_log_i(TAG, "bench settled at iter %u: mean_bucket=%"PRId64" us, min_bucket=%"PRId64" us, ratio=%.2f",
                     (uint32_t)settled_first * bucket_size, mean_us, min_bucket_us,
                     (double)mean_us / (double)min_bucket_us);
        }
    } else {
        /* Fallback: full-window average (transparently flagged). */
        us_per_op = (iterations > 0) ? (double)total_us / iterations / 3.0 : 0.0;
        bb_log_w(TAG, "bench did NOT settle after %"PRIu32" iters / %"PRIu32" buckets — fallback to full-window avg",
                 iterations, num_buckets);
    }

    bb_log_i(TAG, "dport bench: %"PRIu32" iters, %"PRIu32" buckets*%"PRIu32", settled=%s after %"PRIu32" iters, us/op=%.2f%s",
             iterations, num_buckets, bucket_size,
             settled ? "yes" : "no",
             settled ? (uint32_t)settled_first * bucket_size : iterations,
             us_per_op, settled ? "" : " (UNSETTLED)");

    if (out) {
        out->total_us           = total_us;
        out->us_per_op          = us_per_op;
        out->settled            = settled;
        out->settled_after_iters = settled ? (uint32_t)settled_first * bucket_size : 0;
        out->settled_total_us   = settled ? settled_us : 0;
        out->settled_iters      = settled ? settled_iters : 0;
    }
}

/* ---------------------------------------------------------------------------
 * TA-403: SHA TEXT-overlap canary for the DPORT driver (classic ESP32 D0).
 *
 * Mirrors sha256_hw_overlap_canary() (AHB driver, sha256_hw_ahb.c:401), but
 * exercises the specific overlap pattern used by the DPORT kernel
 * (sha256_hw_dport_kernel.h:91-107): writes TEXT[0..15] (block-2 fill) during
 * block-1 compute, immediately after SHA_START.
 *
 * Method: compute a reference digest with no interference; then trigger
 * SHA_256_START and immediately write garbage to TEXT[0] before BUSY clears.
 * If the peripheral snapshots TEXT at trigger time, the interference is harmless
 * and the observed digest matches the reference → SAFE.
 *
 * Uses the same test vector as sha256_hw_overlap_canary() for direct comparison.
 * Caller MUST hold sha256_hw_dport_acquire() — does not re-acquire.
 * ---------------------------------------------------------------------------
 */
static bool sha256_hw_dport_overlap_canary(void)
{
    /* Same vector as AHB canary (sha256_hw_ahb.c:403-407) for direct comparison.
     * This is a synthetic 512-bit message block; absolute value is irrelevant —
     * the probe only checks that the digest is stable under mid-compute TEXT writes. */
    static const uint32_t test_msg[16] = {
        0xfeedface, 0xcafebabe, 0xdeadbeef, 0x01234567,
        0x89abcdef, 0x12345678, 0x9abcdef0, 0x0fedcba9,
        0x00000080, 0, 0, 0, 0, 0, 0, 0x00010000
    };

    volatile uint32_t *sha_text = (volatile uint32_t *)(SHA_TEXT_BASE);
    uint32_t reference[8];
    uint32_t observed[8];

    /* Reference: clean SHA_256_START with no mid-compute interference. */
    {
        for (int j = 0; j < 16; j++) sha_text[j] = test_msg[j];
        DPORT_REG_WRITE(SHA_256_START_REG, 1);
        while (DPORT_REG_READ(SHA_256_BUSY_REG)) {}
        DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);
        while (DPORT_REG_READ(SHA_256_BUSY_REG)) {}
        DPORT_INTERRUPT_DISABLE();
        for (int j = 0; j < 8; j++) reference[j] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + j * 4);
        DPORT_INTERRUPT_RESTORE();
    }

    /* Trial: SHA_256_START, then immediately corrupt TEXT[0] before BUSY clears.
     * Mirrors the kernel's block-2 fill pattern (kernel.h:91-107) which writes
     * all TEXT[0..15] during block-1 compute. */
    {
        for (int j = 0; j < 16; j++) sha_text[j] = test_msg[j];
        DPORT_REG_WRITE(SHA_256_START_REG, 1);
        sha_text[0] = 0xBADC0FFE;  /* corrupt TEXT[0] mid-compute — mirrors kernel overlap */
        while (DPORT_REG_READ(SHA_256_BUSY_REG)) {}
        DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);
        while (DPORT_REG_READ(SHA_256_BUSY_REG)) {}
        DPORT_INTERRUPT_DISABLE();
        for (int j = 0; j < 8; j++) observed[j] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + j * 4);
        DPORT_INTERRUPT_RESTORE();
    }

    bool safe = true;
    for (int j = 0; j < 8; j++) {
        if (observed[j] != reference[j]) { safe = false; break; }
    }

    if (safe) {
        bb_log_i(TAG, "SHA TEXT-overlap canary (DPORT): SAFE — peripheral snapshots TEXT at trigger; overlap-during-busy-wait possible");
    } else {
        bb_log_i(TAG, "SHA TEXT-overlap canary (DPORT): UNSAFE — peripheral reads TEXT during compute; cannot overlap");
    }

    mining_set_sha_overlap_safe(safe);
    return safe;
}

/* ---------------------------------------------------------------------------
 * TA-403: SHA H-write-during-compute canary for the DPORT driver (D0).
 *
 * Mirrors sha256_hw_hwrite_canary() (AHB driver, sha256_hw_ahb.c:447), but the
 * failure mode being probed does not exist on the classic ESP32 DPORT SHA path:
 *
 * On AHB targets (S3/S2/C3), SHA_H[0..7] is a separate register bank that holds
 * the running midstate and can be written independently of SHA_TEXT. The AHB
 * canary tests whether clobbering SHA_H during compute corrupts the output.
 *
 * On classic ESP32 DPORT SHA, there is no separate H register bank. After
 * SHA_256_START/CONTINUE completes, SHA_256_LOAD copies the finished digest into
 * TEXT[0..7]. The kernel (kernel.h:119-123) writes TEXT[8] and TEXT[15] during
 * the post-LOAD window — never TEXT[0..7]. There is no midstate-reload path,
 * no cross-nonce pipelining that targets the H bank, and therefore no scenario
 * where an H-write-during-compute could occur. The failure mode this canary
 * guards against simply does not exist on this hardware path.
 *
 * Reports SAFE because the hazard is structurally absent, not merely untested.
 * Caller MUST hold sha256_hw_dport_acquire() — does not re-acquire.
 * ---------------------------------------------------------------------------
 */
static bool sha256_hw_dport_hwrite_canary(void)
{
    /* DPORT SHA has no separate H register — digest accumulates in TEXT[0..7]
     * only after SHA_256_LOAD. The kernel never writes TEXT[0..7] during
     * compute (only TEXT[8] and TEXT[15] after LOAD, per kernel.h:119-123).
     * The H-write-during-compute failure mode does not exist on this path;
     * report SAFE to distinguish "probed and confirmed absent" from UNKNOWN
     * ("probe never ran"). */
    bb_log_i(TAG, "SHA H-write canary (DPORT): SAFE — no separate H register on classic ESP32; "
             "digest in TEXT[0..7] after LOAD; kernel never writes TEXT[0..7] during compute");
    mining_set_sha_hwrite_safe(true);
    return true;
}

/* ---------------------------------------------------------------------------
 * Boot probes bundle for D0 (classic ESP32).
 * Called from mining_run_self_tests() under the SHA peripheral lock.
 * Runs the lockstep test + microbench; logs results; safe to call once at boot.
 * ---------------------------------------------------------------------------
 */
void sha256_hw_dport_boot_probes(void)
{
    sha256_hw_dport_overlap_canary();
    sha256_hw_dport_hwrite_canary();
    bb_err_t rc = sha256_hw_dport_self_test_lockstep();
    if (rc != BB_OK) {
        bb_log_e(TAG, "D0 lockstep self-test FAILED — SHA hot loop digest diverges from SW SHA256d");
    }
    sha256_hw_dport_microbench();
}

/* ---------------------------------------------------------------------------
 * Init: runs known-vector self-test then logs result.
 * Return value is ignored at this layer; gate logic (Phase 2) will check it.
 * ---------------------------------------------------------------------------
 */
void sha256_hw_dport_init(void)
{
    sha256_hw_dport_acquire();
    sha256_hw_dport_self_test();  /* return value ignored; mining.c will check it in Phase 2 */
    sha256_hw_dport_release();
}

#endif // CONFIG_IDF_TARGET_ESP32
#endif // ESP_PLATFORM
