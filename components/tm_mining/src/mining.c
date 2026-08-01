// mining.c — the compute core: SHA-256 nonce loop + hash_backend_t seam.
//
// Scope (TA-561, this PR): esp32-wroom32 / SW-mining path only. No ASIC, no
// AHB (S3/S2/C3) HW-SHA backend, no unicore branch -- classic ESP32 is
// dual-core and this board's only supported non-ASIC target right now.
//
// Delivery-agnostic producer core: pure gather() snapshot (no I/O, no
// allocation). The producer surface definition lives in tm_mining_producer.c
// and registration happens in src/main.c.
//
// Cross-core work/result delivery and the OTA pause handshake are NOT
// hand-rolled here: they go through the mining_queue_ops_t / pause-gate
// seams in mining.h (TA-562/TA-563, gated on breadboard bb_bqueue /
// bb_lifecycle). Until a later PR binds real ops, both are no-ops.

#include "mining.h"
#include "share_validate.h"
#include "sha256.h"
#include "work.h"
#include "bb_log.h"
#include "bb_byte_order.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#endif

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>

/* Single-core targets have no core 1; this component only targets
 * dual-core classic ESP32 in this PR, so mining always pins to core 1. */
#define MINER_TASK_CORE 1

static const char *TAG = "mining";

/* Detect an all-zero SHA256d result. A valid double-SHA256 is never all-zero;
 * on classic ESP32 (D0) the DPORT cross-bus erratum can return a zeroed hash.
 * Used by the share-verdict block to drop corrupt reads before they corrupt
 * best_diff or fire false block detections. */
static inline bool s_hash_is_all_zero(const uint8_t *hash)
{
    for (int i = 0; i < 32; i++) {
        if (hash[i] != 0) return false;
    }
    return true;
}

/* Wall-clock unix seconds, sanitized so pre-SNTP epochs don't bleed into UI.
 * Returns 0 if the clock hasn't been synced yet. */
static inline int64_t s_wall_clock_or_zero(void)
{
    time_t now = time(NULL);
    return ((int64_t)now < 1700000000LL) ? 0 : (int64_t)now;
}

inline void pack_double(double v, uint32_t *hi, uint32_t *lo) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    *hi = (uint32_t)(bits >> 32);
    *lo = (uint32_t)(bits & 0xFFFFFFFFu);
}
inline double unpack_double(uint32_t hi, uint32_t lo) {
    uint64_t bits = ((uint64_t)hi << 32) | lo;
    double v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

void mining_stats_update_ema(hashrate_ema_t *ema, double sample, int64_t now_us)
{
    if (ema->value == 0.0) {
        ema->value = sample;
    } else {
        ema->value = 0.2 * sample + 0.8 * ema->value;
    }
    ema->last_us = now_us;
}

// J/TH efficiency helper — pure math, host-testable.
// mW/(GH/s) == J/TH: milli cancels 1e-3, giga->tera cancels 1e-3, net 1x.
double mining_efficiency_jth(double power_mw, double hashrate_ghs)
{
    return (hashrate_ghs > 0.0 && power_mw > 0.0) ? (power_mw / hashrate_ghs) : -1.0;
}

// Pure math, no FreeRTOS — usable in host tests directly.
double mining_compute_pool_effective_hps(double accepted_diff_sum, double uptime_s)
{
    if (accepted_diff_sum <= 0.0 || uptime_s < 1.0) return 0.0;
    return accepted_diff_sum * 4294967296.0 / uptime_s;
}

#ifndef ESP_PLATFORM
// Host-test stubs — callers that need the live value use
// mining_compute_pool_effective_hps directly.
double mining_get_pool_effective_hashrate(void) { return 0.0; }
double mining_get_pool_effective_1m(void) { return 0.0; }
double mining_get_pool_effective_10m(void) { return 0.0; }
double mining_get_pool_effective_1h(void) { return 0.0; }
#endif

// Block-found callback hook (keeps mining decoupled from LED/UI/event delivery).
static void (*s_block_found_cb)(void) = NULL;

void mining_set_block_found_cb(void (*cb)(void)) {
    s_block_found_cb = cb;
}

void mining_notify_block_found(void) {
    if (s_block_found_cb) s_block_found_cb();
}

// SHA self-test flag (process-static, exposed for host tests)
static bool s_sha_self_test_failed = false;

bool mining_sha_self_test_failed(void) { return s_sha_self_test_failed; }

void mining_set_sha_self_test_failed(void) {
    s_sha_self_test_failed = true;
}

// SHA TEXT-overlap canary state.
static sha_overlap_state_t s_sha_overlap_state = SHA_OVERLAP_UNKNOWN;

void mining_set_sha_overlap_safe(bool safe) {
    s_sha_overlap_state = safe ? SHA_OVERLAP_SAFE : SHA_OVERLAP_UNSAFE;
}

sha_overlap_state_t mining_get_sha_overlap_state(void) {
    return s_sha_overlap_state;
}

// SHA H-write-during-compute canary state.
static sha_overlap_state_t s_sha_hwrite_state = SHA_OVERLAP_UNKNOWN;

void mining_set_sha_hwrite_safe(bool safe) {
    s_sha_hwrite_state = safe ? SHA_OVERLAP_SAFE : SHA_OVERLAP_UNSAFE;
}

sha_overlap_state_t mining_get_sha_hwrite_state(void) {
    return s_sha_hwrite_state;
}

// ---------------------------------------------------------------------------
// Queue seam (TA-562) — default no-op ops. A later PR calls
// mining_set_queue_ops() from the composition root once bb_bqueue exists.
// ---------------------------------------------------------------------------

static const mining_queue_ops_t *s_queue_ops = NULL;

void mining_set_queue_ops(const mining_queue_ops_t *ops)
{
    s_queue_ops = ops;
}

bool mining_work_peek(mining_work_t *out)
{
    if (!s_queue_ops || !s_queue_ops->peek_work) return false;
    return s_queue_ops->peek_work(s_queue_ops->ctx, out);
}

bool mining_result_post(const mining_result_t *result)
{
    if (!s_queue_ops || !s_queue_ops->post_result) return false;
    return s_queue_ops->post_result(s_queue_ops->ctx, result);
}

// ---------------------------------------------------------------------------
// Run-state gate seam (TA-563) — default no-op gate (never pauses).
// ---------------------------------------------------------------------------

static mining_should_pause_fn s_pause_gate_fn = NULL;
static void                  *s_pause_gate_ctx = NULL;

void mining_set_pause_gate(mining_should_pause_fn fn, void *ctx)
{
    s_pause_gate_fn = fn;
    s_pause_gate_ctx = ctx;
}

static inline bool s_mining_should_pause(void)
{
    return s_pause_gate_fn ? s_pause_gate_fn(s_pause_gate_ctx) : false;
}

#ifdef ESP_PLATFORM
#include "bb_timer.h"
#include "bb_wdt.h"
#include "mining_avg.h"
#if CONFIG_IDF_TARGET_ESP32
#include "sha256_hw_dport.h"
#include "sha256_hw_dport_kernel.h"
#endif
#include "bb_system.h"

mining_stats_t mining_stats = {0};

/* Non-ASIC rolling 1m/10m/1h hashrate + pool-effective samplers. */
static unsigned long    s_hw_avg_poll_count = 0;
static float            s_hw_hr_1m[MINING_AVG_1M_SIZE];
static float            s_hw_hr_10m[MINING_AVG_10M_SIZE];
static float            s_hw_hr_1h[MINING_AVG_1H_SIZE];
static float            s_hw_hr_10m_prev = NAN;
static float            s_hw_hr_1h_prev  = NAN;
static bb_periodic_timer_t s_hw_avg_timer = NULL;

static unsigned long    s_pool_eff_poll_count = 0;
static float            s_pool_eff_1m[MINING_AVG_1M_SIZE];
static float            s_pool_eff_10m[MINING_AVG_10M_SIZE];
static float            s_pool_eff_1h[MINING_AVG_1H_SIZE];
static float            s_pool_eff_10m_prev = NAN;
static float            s_pool_eff_1h_prev  = NAN;
static double           s_pool_eff_prev_sum = 0.0;

static void hw_avg_timer_cb(void *arg)
{
    (void)arg;
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    float sample = (float)mining_stats.hw_hashrate;
    float out_1m = 0.0f, out_10m = 0.0f, out_1h = 0.0f;
    mining_avg_update(s_hw_avg_poll_count++, sample,
                      s_hw_hr_1m, s_hw_hr_10m, s_hw_hr_1h,
                      &s_hw_hr_10m_prev, &s_hw_hr_1h_prev,
                      &out_1m, &out_10m, &out_1h);
    mining_stats.hashrate_1m  = out_1m;
    mining_stats.hashrate_10m = out_10m;
    mining_stats.hashrate_1h  = out_1h;

    double sum_now = mining_stats.session.accepted_diff_sum;
    float pe_1m = 0.0f, pe_10m = 0.0f, pe_1h = 0.0f;
    mining_pool_eff_tick(sum_now, 5.0,
                         &s_pool_eff_prev_sum, s_pool_eff_poll_count++,
                         s_pool_eff_1m, s_pool_eff_10m, s_pool_eff_1h,
                         &s_pool_eff_10m_prev, &s_pool_eff_1h_prev,
                         &pe_1m, &pe_10m, &pe_1h);
    mining_stats.pool_eff_1m  = pe_1m;
    mining_stats.pool_eff_10m = pe_10m;
    mining_stats.pool_eff_1h  = pe_1h;

    xSemaphoreGive(mining_stats.mutex);
}

// Run SHA self-tests synchronously before any task starts. Self-tests gate
// mining (failure flag committed before anything else can read it).
void mining_run_self_tests(void)
{
    if (sha256_sw_self_test() != BB_OK) {
        bb_log_e(TAG, "SHA SW self-test FAILED — mining will not start");
        mining_set_sha_self_test_failed();
        return;
    }
#if CONFIG_IDF_TARGET_ESP32
    sha256_hw_dport_acquire();
    bb_err_t dport_rc = sha256_hw_dport_self_test();
    if (dport_rc != BB_OK) {
        sha256_hw_dport_release();
        bb_log_e(TAG, "SHA DPORT self-test FAILED — mining will not start");
        mining_set_sha_self_test_failed();
        return;
    }
    sha256_hw_dport_boot_probes();
    sha256_hw_dport_release();
#endif
}

double mining_get_pool_effective_hashrate(void)
{
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0.0;
    double sum    = mining_stats.session.accepted_diff_sum;
    int64_t start = mining_stats.session.start_us;
    xSemaphoreGive(mining_stats.mutex);
    if (sum <= 0.0 || start <= 0) return 0.0;
    int64_t now = (int64_t)bb_timer_now_us();
    double uptime_s = (double)(now - start) / 1e6;
    return mining_compute_pool_effective_hps(sum, uptime_s);
}

double mining_get_pool_effective_1m(void)
{
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0.0;
    float v = mining_stats.pool_eff_1m;
    xSemaphoreGive(mining_stats.mutex);
    return v >= 0.0f ? (double)v : 0.0;
}

double mining_get_pool_effective_10m(void)
{
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0.0;
    float v = mining_stats.pool_eff_10m;
    xSemaphoreGive(mining_stats.mutex);
    return v >= 0.0f ? (double)v : 0.0;
}

double mining_get_pool_effective_1h(void)
{
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0.0;
    float v = mining_stats.pool_eff_1h;
    xSemaphoreGive(mining_stats.mutex);
    return v >= 0.0f ? (double)v : 0.0;
}

void mining_stats_init(void)
{
    mining_stats.mutex = xSemaphoreCreateMutex();
    mining_stats.session.start_us = (int64_t)bb_timer_now_us();
    mining_stats.session.accepted_diff_sum = 0.0;
    mining_stats.hashrate_1m  = -1.0f;
    mining_stats.hashrate_10m = -1.0f;
    mining_stats.hashrate_1h  = -1.0f;
    mining_stats.pool_eff_1m  = -1.0f;
    mining_stats.pool_eff_10m = -1.0f;
    mining_stats.pool_eff_1h  = -1.0f;
    for (size_t i = 0; i < MINING_AVG_1M_SIZE;  i++) s_hw_hr_1m[i]  = NAN;
    for (size_t i = 0; i < MINING_AVG_10M_SIZE; i++) s_hw_hr_10m[i] = NAN;
    for (size_t i = 0; i < MINING_AVG_1H_SIZE;  i++) s_hw_hr_1h[i]  = NAN;
    for (size_t i = 0; i < MINING_AVG_1M_SIZE;  i++) s_pool_eff_1m[i]  = NAN;
    for (size_t i = 0; i < MINING_AVG_10M_SIZE; i++) s_pool_eff_10m[i] = NAN;
    for (size_t i = 0; i < MINING_AVG_1H_SIZE;  i++) s_pool_eff_1h[i]  = NAN;
    s_pool_eff_prev_sum = 0.0;
    BB_ERROR_CHECK(bb_timer_periodic_create(hw_avg_timer_cb, NULL, "hw_avg", &s_hw_avg_timer));
    BB_ERROR_CHECK(bb_timer_periodic_start(s_hw_avg_timer, 5000000ULL));
}

void mining_stats_session_reset(void)
{
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&mining_stats.session, 0, sizeof(mining_stats.session));
        mining_stats.session.start_us = (int64_t)bb_timer_now_us();
        mining_stats.session.rejected_other_last_code = -1;
        xSemaphoreGive(mining_stats.mutex);
    } else {
        bb_log_w(TAG, "session_reset: mutex timeout");
    }
}
#endif // ESP_PLATFORM

// Build the 64-byte block2 from header tail + SHA padding
void build_block2(uint8_t block2[64], const uint8_t header[80])
{
    memset(block2, 0, 64);
    memcpy(block2, header + 64, 16);
    block2[16] = 0x80;
    block2[62] = 0x02;
    block2[63] = 0x80;
}

#ifdef ESP_PLATFORM
// Single die-temp read path. Best-effort: no-op on parts without the
// sensor and when the stats mutex is busy.
void mining_stats_sample_die_temp(void)
{
    float t;
    if (bb_system_read_temp_celsius(&t) != BB_OK) return;
    if (xSemaphoreTake(mining_stats.mutex, 0) == pdTRUE) {
        mining_stats.temp_c = t;
        xSemaphoreGive(mining_stats.mutex);
    }
}
#endif

// Pack target bytes [28-31] into a single word for early reject comparison
uint32_t pack_target_word0(const uint8_t target[32])
{
    /* The TRUE most-significant 32-bit word of the 256-bit PoW target, in
     * meets_target's convention (target[31] = MSB byte, target[0] = LSB).
     * Callers compare this against bswap32(H[7]) — the true PoW MSB word. */
    return ((uint32_t)target[31] << 24) |
           ((uint32_t)target[30] << 16) |
           ((uint32_t)target[29] <<  8) |
            (uint32_t)target[28];
}

// Fill a mining_result_t from work + nonce + version info.
// Stratum mining.submit version field: the pool recomposes the full header
// version as (base & ~mask) | (submitted & mask), so we submit only the
// rolled bits within the negotiated version_mask -- never the full rolled
// version, and never a bit outside the mask (issue #606: a pool may
// advertise a mask narrower than its min-bit-count wants; TM must still
// only roll/submit within the mask it was actually given).
void package_result(mining_result_t *result,
                    const mining_work_t *work,
                    uint32_t nonce,
                    uint32_t ver_bits)
{
    strncpy(result->job_id, work->job_id, sizeof(result->job_id) - 1);
    result->job_id[sizeof(result->job_id) - 1] = '\0';
    strncpy(result->extranonce2_hex, work->extranonce2_hex, sizeof(result->extranonce2_hex) - 1);
    result->extranonce2_hex[sizeof(result->extranonce2_hex) - 1] = '\0';
    sprintf(result->ntime_hex, "%08" PRIx32, work->ntime);
    sprintf(result->nonce_hex, "%08" PRIx32, nonce);
    if (ver_bits != 0) {
        sprintf(result->version_hex, "%08" PRIx32, ver_bits & work->version_mask);
    } else {
        result->version_hex[0] = '\0';
    }
}

// --- Software SHA hash backend (native build + host tests) ---

void sw_prepare_job(hash_backend_t *b,
                    const mining_work_t *work,
                    const uint8_t block2[64])
{
    sw_backend_ctx_t *ctx = (sw_backend_ctx_t *)b->ctx;
    memcpy(ctx->midstate, sha256_H0, sizeof(sha256_H0));
    sha256_transform(ctx->midstate, work->header);
    ctx->target_word0 = pack_target_word0(work->target);
    memcpy(ctx->block2, block2, 64);
}

hash_result_t sw_hash_nonce(hash_backend_t *b,
                            uint32_t nonce,
                            uint8_t hash_out[32])
{
    sw_backend_ctx_t *ctx = (sw_backend_ctx_t *)b->ctx;

    ctx->block2[12] = (uint8_t)(nonce & 0xff);
    ctx->block2[13] = (uint8_t)((nonce >> 8) & 0xff);
    ctx->block2[14] = (uint8_t)((nonce >> 16) & 0xff);
    ctx->block2[15] = (uint8_t)((nonce >> 24) & 0xff);

    uint32_t state[8];
    memcpy(state, ctx->midstate, 32);
    sha256_transform(state, ctx->block2);

    for (int i = 0; i < 8; i++) {
        ctx->block3_words[i] = state[i];
    }

    memcpy(state, sha256_H0, 32);
    sha256_transform_words(state, ctx->block3_words);

    /* Early reject: compare the TRUE PoW most-significant word. state[7] is
     * canonical H[7]; the true MSB word is bswap32(state[7]). target_word0
     * is packed in that same true-MSB order. */
    if (__builtin_bswap32(state[7]) <= ctx->target_word0) {
        mining_hash_from_state(state, hash_out);
        return HASH_CHECK;
    }
    return HASH_MISS;
}

void sw_backend_setup(hash_backend_t *b, sw_backend_ctx_t *ctx)
{
    memset(ctx->block3_words, 0, sizeof(ctx->block3_words));
    ctx->block3_words[8]  = 0x80000000U;
    ctx->block3_words[15] = 0x00000100U;

    b->init = NULL;
    b->prepare_job = sw_prepare_job;
    b->hash_nonce = sw_hash_nonce;
    b->ctx = ctx;
}

#if defined(ESP_PLATFORM) && CONFIG_IDF_TARGET_ESP32
// --- Hardware SHA hash backend (DPORT-bus: classic ESP32) ---
// TA-271 step B: NerdMiner-verbatim hot loop via sha256_hw_dport_per_nonce.
// prepare_job stores the header pointer; per_nonce re-hashes block1 every
// call because classic ESP32 has no writable H registers.

typedef struct {
    const uint8_t *header;
    uint32_t target_word0_max;  // MSB word of pool target for early-reject
} hw_backend_ctx_t;

static void hw_backend_init(hash_backend_t *b)
{
    (void)b;
    sha256_hw_dport_init();
}

static void hw_prepare_job(hash_backend_t *b,
                           const mining_work_t *work,
                           const uint8_t block2[64])
{
    hw_backend_ctx_t *ctx = (hw_backend_ctx_t *)b->ctx;
    (void)block2;
    ctx->header = work->header;
    ctx->target_word0_max = ((uint32_t)work->target[31] << 24) |
                            ((uint32_t)work->target[30] << 16) |
                            ((uint32_t)work->target[29] <<  8) |
                             (uint32_t)work->target[28];
    sha256_hw_dport_kernel_init(work->header);
}

static hash_result_t hw_dport_hash_nonce(hash_backend_t *b,
                                          uint32_t nonce,
                                          uint8_t hash_out[32])
{
    hw_backend_ctx_t *ctx = (hw_backend_ctx_t *)b->ctx;
    if (sha256_hw_dport_per_nonce(ctx->header, nonce, ctx->target_word0_max, hash_out)) {
        return HASH_CHECK;
    }
    return HASH_MISS;
}

static void hw_backend_setup(hash_backend_t *b, hw_backend_ctx_t *ctx)
{
    b->init = hw_backend_init;
    b->prepare_job = hw_prepare_job;
    b->hash_nonce = hw_dport_hash_nonce;
    b->ctx = ctx;
}

#endif // ESP_PLATFORM && CONFIG_IDF_TARGET_ESP32

bool IRAM_ATTR mine_nonce_range(hash_backend_t *backend,
                                 mining_work_t *work,
                                 const mine_params_t *params,
                                 mining_result_t *result_out,
                                 bool *found_out)
{
    /* Hot-loop data placement is tunable via CONFIG_MINING_PIN_HOTLOOP_DATA
     * (default n on classic ESP32 — see components/tm_mining/Kconfig and
     * sdkconfig/esp32-classic; the D0 reject-path working set is tiny post
     * TA-396, and aligned-static placement measured ~1.5% slower there). */
#if CONFIG_MINING_PIN_HOTLOOP_DATA
    static uint8_t __attribute__((aligned(64))) block2[64];
#else
    uint8_t block2[64];
#endif
    build_block2(block2, work->header);
    backend->prepare_job(backend, work, block2);

#ifdef ESP_PLATFORM
    int64_t start_us = (int64_t)bb_timer_now_us();
    uint32_t hashes = 0;
#if CONFIG_IDF_TARGET_ESP32
    hw_backend_ctx_t *hw_ctx = (hw_backend_ctx_t *)backend->ctx;
#endif
#endif

    for (uint32_t nonce = params->nonce_start; ; nonce++) {
        uint8_t hash[32];
#if defined(ESP_PLATFORM) && CONFIG_IDF_TARGET_ESP32
        // D0 hot loop: call kernel directly to skip the function-pointer indirection.
        hash_result_t hr = sha256_hw_dport_kernel(hw_ctx->header, nonce, hw_ctx->target_word0_max, hash) ? HASH_CHECK : HASH_MISS;
#else
        hash_result_t hr = backend->hash_nonce(backend, nonce, hash);
#endif

        if (hr == HASH_CHECK) {
#ifdef ESP_PLATFORM
            /* Fast-path: meets_target alone. is_target_valid + diff<0.001 are
             * already validated before the mining loop starts and don't
             * change mid-job. */
            bool may_be_share = meets_target(hash, work->target);
            double share_diff = 0.0;
            share_verdict_t verdict;
            if (!may_be_share) {
                verdict = SHARE_BELOW_TARGET;
            } else if (s_hash_is_all_zero(hash)) {
                /* Corrupt read (classic-ESP32 DPORT cross-bus erratum): drop
                 * it — never count it as a share, best_diff, or block. */
                verdict = SHARE_BELOW_TARGET;
                bb_log_w(TAG, "dropped all-zero hash (corrupt SHA read, nonce=%08" PRIx32 ")", nonce);
            } else {
                verdict = share_validate(work, hash, &share_diff);
                if (verdict == SHARE_VALID &&
                    !share_reverify(work, params->ver_bits, nonce, hash)) {
                    /* DPORT partial-corruption: HW hash passed target but the
                     * SW recompute disagrees — drop. */
                    verdict = SHARE_BELOW_TARGET;
                    bb_log_w(TAG, "dropped corrupt SHA read (reverify mismatch, nonce=%08" PRIx32 ")", nonce);
                }
            }

            if (verdict == SHARE_BELOW_TARGET) {
                // Normal miss.
            } else if (verdict == SHARE_INVALID_TARGET || verdict == SHARE_LOW_DIFFICULTY) {
                bb_log_e(TAG, "share sanity fail: share_diff=%.4f pool_diff=%.4f, skipping",
                         share_diff, work->difficulty);
            } else {
                // SHARE_VALID
                mining_result_t result;
                package_result(&result, work, nonce, params->ver_bits);
                bb_log_i(TAG, "share found! (nonce=%08" PRIx32 ")", nonce);

                result.share_diff = work->difficulty;

                /* Local candidate found -- best_diff/best_diff_ts only.
                 * session.shares/accepted_diff_sum are pool-acceptance
                 * counters, driven by the (not-yet-wired) stratum submit
                 * response, not by finding a locally-valid candidate here. */
                int64_t now_ts = s_wall_clock_or_zero();
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                    if (share_diff > mining_stats.session.best_diff) {
                        mining_stats.session.best_diff = share_diff;
                        mining_stats.session.best_diff_ts = now_ts;
                    }
                    xSemaphoreGive(mining_stats.mutex);
                }

                // Block detection: check if this share meets the network target.
                if (share_meets_network_target(hash, work->nbits)) {
                    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        mining_stats.session.blocks_found++;
                        mining_stats.session.last_block_ts = now_ts;
                        xSemaphoreGive(mining_stats.mutex);
                    }
                    mining_notify_block_found();
                }

                if (result_out) {
                    *result_out = result;
                }
                if (found_out) {
                    *found_out = true;
                    return false;  // device tests: stop after first hit
                }

                if (!mining_result_post(&result)) {
                    bb_log_d(TAG, "result post dropped (no delivery bound / sink full)");
                }
            }
#else
            // In host tests: lightweight check — meets_target only (no FreeRTOS).
            if (meets_target(hash, work->target)) {
                mining_result_t result;
                package_result(&result, work, nonce, params->ver_bits);
                if (result_out) {
                    *result_out = result;
                }
                if (found_out) {
                    *found_out = true;
                    return false;  // in tests, stop after first hit
                }
            }
#endif
        }

#ifdef ESP_PLATFORM
        hashes++;

        // Periodic yield + job refresh
        if (((nonce + 1) & params->yield_mask) == 0) {
            // Tier 1: lightweight new-work check (every 256K nonces)
            sha256_hw_release();
            mining_work_t new_work;
            if (mining_work_peek(&new_work) && new_work.work_seq != work->work_seq) {
                memcpy(work, &new_work, sizeof(*work));
                bb_log_i(TAG, "new job (%s)", work->job_id);
                build_block2(block2, work->header);
                sha256_hw_acquire();
                backend->prepare_job(backend, work, block2);
                start_us = (int64_t)bb_timer_now_us();
                hashes = 0;
                nonce = params->nonce_start - 1;
                continue;
            }

            sha256_hw_acquire();

            // Run-state gate (TA-563): checked on Tier-1 cadence (256K
            // nonces gives ample headroom vs a future ack-timeout window).
            if (s_mining_should_pause()) {
                sha256_hw_release();
                vTaskDelay(pdMS_TO_TICKS(50));
                sha256_hw_acquire();
                backend->prepare_job(backend, work, block2);
                start_us = (int64_t)bb_timer_now_us();
                hashes = 0;
                nonce = params->nonce_start - 1;
                continue;
            }

            // Tier 2: full yield (every 1M nonces)
            if (((nonce + 1) & params->log_mask) == 0) {
                int64_t elapsed_us = (int64_t)bb_timer_now_us() - start_us;
                if (elapsed_us > 0) {
                    double hashrate = (double)hashes / ((double)elapsed_us / 1000000.0);
                    uint32_t accepted = 0;
                    uint32_t rejected = 0;
                    if (xSemaphoreTake(mining_stats.mutex, 0) == pdTRUE) {
                        mining_stats.hw_hashrate = hashrate;
                        mining_stats_update_ema(&mining_stats.hw_ema, hashrate, (int64_t)bb_timer_now_us());
                        mining_stats.session.hashes += hashes;
                        accepted = mining_stats.session.shares;
                        rejected = mining_stats.session.rejected;
                        xSemaphoreGive(mining_stats.mutex);
                    }
                    double accepted_pct = (accepted + rejected) > 0
                        ? (double)accepted * 100.0 / (double)(accepted + rejected)
                        : 0.0;
                    bb_log_i(TAG, "hw: %.1f kH/s | shares: acc %" PRIu32 "/rej %" PRIu32 " = %.1f%%",
                             hashrate / 1000.0, accepted, rejected, accepted_pct);
                }

                mining_stats_sample_die_temp();

                // Release SHA lock so mbedTLS can use HW SHA during TLS/OTA
                sha256_hw_release();
                bb_wdt_task_feed();
                vTaskDelay(pdMS_TO_TICKS(5));
                sha256_hw_acquire();
            }
        }
#endif

        if (nonce == params->nonce_end) break;
    }
    return false;
}

#ifdef ESP_PLATFORM
void mining_task(void *arg)
{
    (void)arg;

    bb_log_i(TAG, "mining task started");

    if (mining_sha_self_test_failed()) {
        bb_log_e(TAG, "SHA self-test previously failed — mining task exiting");
        return;
    }

#if CONFIG_MINING_PIN_HOTLOOP_DATA
    static __attribute__((aligned(64))) hw_backend_ctx_t hw_ctx;
#else
    hw_backend_ctx_t hw_ctx;
#endif
    hash_backend_t backend;
    hw_backend_setup(&backend, &hw_ctx);

    if (backend.init) {
        backend.init(&backend);
    }

    // Subscribe mining task to TWDT — IDLE1 monitoring is disabled because
    // this task is CPU-bound on core 1 by design. Feed at each yield point.
    bb_wdt_task_subscribe();

    sha256_hw_acquire();

    for (;;) {
        mining_work_t work;
        sha256_hw_release();
        bool got = mining_work_peek(&work);
        sha256_hw_acquire();
        if (!got) {
            bb_wdt_task_feed();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (!is_target_valid(work.target)) {
            bb_log_w(TAG, "invalid target from peek, waiting for fresh work");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bb_log_i(TAG, "new job (%s)", work.job_id);

        uint32_t base_version = work.version;
        uint32_t ver_bits = 0;

        for (;;) {  // version rolling outer loop
            if (work.version_mask != 0 && ver_bits != 0) {
                uint32_t rolled = (base_version & ~work.version_mask) | (ver_bits & work.version_mask);
                work.header[0] = rolled & 0xFF;
                work.header[1] = (rolled >> 8) & 0xFF;
                work.header[2] = (rolled >> 16) & 0xFF;
                work.header[3] = (rolled >> 24) & 0xFF;
            }

            mine_params_t params = {
                .nonce_start = 0,
                .nonce_end = 0xFFFFFFFFU,
                .yield_mask = 0x3FFFF,
                .log_mask = 0xFFFFF,
                .ver_bits = ver_bits,
                .base_version = base_version,
                .version_mask = work.version_mask,
            };

            mine_nonce_range(&backend, &work, &params, NULL, NULL);

            if (work.version_mask == 0) break;
            ver_bits = next_version_roll(ver_bits, work.version_mask);
            if (ver_bits == 0) break;
            bb_log_i(TAG, "rolling version: mask=%08" PRIx32 " bits=%08" PRIx32, work.version_mask, ver_bits);
        }

        bb_log_w(TAG, "exhausted nonce range for job %s", work.job_id);
    }
}

const miner_config_t g_miner_config = {
    .init = NULL,
    .task_fn = mining_task,
    .name = "mining_hw",
    // 6144 keeps >=2560 B margin (HWM showed ~3300 B free at 8192).
    .stack_size = 6144,
    .priority = 20,
    .core = MINER_TASK_CORE,
    .extranonce2_roll = false,
    .roll_interval_ms = 0,
};
#endif // ESP_PLATFORM
