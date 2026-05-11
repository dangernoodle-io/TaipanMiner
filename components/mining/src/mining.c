#include "mining.h"
#include "share_validate.h"
#include "diag.h"
#ifdef ASIC_CHIP
#include "board.h"
#endif
#include "sha256.h"
#include "stratum.h"
#include "work.h"
#include "bb_log.h"
#include "bb_byte_order.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>

void mining_stats_update_ema(hashrate_ema_t *ema, double sample, int64_t now_us)
{
    if (ema->value == 0.0) {
        ema->value = sample;
    } else {
        ema->value = 0.2 * sample + 0.8 * ema->value;
    }
    ema->last_us = now_us;
}

// TA-344: pure math, no FreeRTOS — usable in host tests directly.
double mining_compute_pool_effective_hps(double accepted_diff_sum, double uptime_s)
{
    if (accepted_diff_sum <= 0.0 || uptime_s < 1.0) return 0.0;
    return accepted_diff_sum * 4294967296.0 / uptime_s;
}

#ifndef ESP_PLATFORM
// Host-test stubs — callers that need the live value use mining_compute_pool_effective_hps directly.
double mining_get_pool_effective_hashrate(void) { return 0.0; }
double mining_get_pool_effective_1m(void) { return 0.0; }
double mining_get_pool_effective_10m(void) { return 0.0; }
double mining_get_pool_effective_1h(void) { return 0.0; }
#endif

// SHA self-test flag (process-static, exposed for host tests)
static bool s_sha_self_test_failed = false;

bool mining_sha_self_test_failed(void) { return s_sha_self_test_failed; }

void mining_set_sha_self_test_failed(void) {
    s_sha_self_test_failed = true;
}

// TA-339: cached HW SHA peripheral microbench result (set once at boot).
static bool   s_sha_microbench_valid = false;
static double s_sha_us_per_op        = 0.0;
static double s_sha_khs_ceiling      = 0.0;

void mining_set_sha_microbench(double us_per_op, double khs_ceiling) {
    s_sha_us_per_op = us_per_op;
    s_sha_khs_ceiling = khs_ceiling;
    s_sha_microbench_valid = true;
}

bool mining_get_sha_microbench(double *us_per_op, double *khs_ceiling) {
    if (!s_sha_microbench_valid) return false;
    if (us_per_op) *us_per_op = s_sha_us_per_op;
    if (khs_ceiling) *khs_ceiling = s_sha_khs_ceiling;
    return true;
}

// Per-board expected hashrate ceiling in GH/s (TA-339).
// ASIC: configured freq × small_cores × asic_count / 1000.
// Non-ASIC: sha_khs_ceiling / 1e6 from boot microbench.
// Returns false when no source is available.
bool mining_get_expected_ghs(float asic_freq_mhz, double *out_ghs) {
    if (!out_ghs) return false;
#ifdef ASIC_CHIP
    (void)s_sha_microbench_valid;
    if (asic_freq_mhz <= 0.0f) return false;
    *out_ghs = (double)asic_freq_mhz * (double)BOARD_SMALL_CORES * (double)BOARD_ASIC_COUNT / 1000.0;
    return true;
#else
    (void)asic_freq_mhz;
    if (!s_sha_microbench_valid) return false;
    *out_ghs = s_sha_khs_ceiling / 1e6;
    return true;
#endif
}

// TA-320a: SHA TEXT-overlap canary state.
static sha_overlap_state_t s_sha_overlap_state = SHA_OVERLAP_UNKNOWN;

void mining_set_sha_overlap_safe(bool safe) {
    s_sha_overlap_state = safe ? SHA_OVERLAP_SAFE : SHA_OVERLAP_UNSAFE;
}

sha_overlap_state_t mining_get_sha_overlap_state(void) {
    return s_sha_overlap_state;
}

// TA-320a: SHA H-write-during-compute canary state.
static sha_overlap_state_t s_sha_hwrite_state = SHA_OVERLAP_UNKNOWN;

void mining_set_sha_hwrite_safe(bool safe) {
    s_sha_hwrite_state = safe ? SHA_OVERLAP_SAFE : SHA_OVERLAP_UNSAFE;
}

sha_overlap_state_t mining_get_sha_hwrite_state(void) {
    return s_sha_hwrite_state;
}

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "mining_avg.h"
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3
#include "sha256_hw_ahb.h"
#elif CONFIG_IDF_TARGET_ESP32
#include "sha256_hw_dport.h"
#endif
#include "bb_nv.h"
#include "bb_system.h"
#include "driver/temperature_sensor.h"

static const char *TAG = "mining";

QueueHandle_t work_queue = NULL;
QueueHandle_t result_queue = NULL;

mining_stats_t mining_stats = {0};

static temperature_sensor_handle_t s_temp_handle = NULL;

#ifndef ASIC_CHIP
/* Non-ASIC rolling 1m/10m/1h hashrate sampler. ASIC builds feed their own
 * buffers from asic_task.c via the same mining_avg helper. */
static unsigned long    s_hw_avg_poll_count = 0;
static float            s_hw_hr_1m[MINING_AVG_1M_SIZE];
static float            s_hw_hr_10m[MINING_AVG_10M_SIZE];
static float            s_hw_hr_1h[MINING_AVG_1H_SIZE];
static float            s_hw_hr_10m_prev = NAN;
static float            s_hw_hr_1h_prev  = NAN;
static esp_timer_handle_t s_hw_avg_timer = NULL;

/* Pool-effective rolling sampler (TA-363) */
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

    /* TA-363: pool-effective rolling sampler */
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
#endif

// TA-341 + TA-320f: run SHA self-tests + boot probes synchronously in
// app_main, before any task starts. Self-tests gate mining (failure flag
// committed before stratum / http / ui can read it). Probes are
// informational — moved here so the boot log shows them in order before
// "Returned from app_main()".
void mining_run_self_tests(void)
{
    if (sha256_sw_self_test() != BB_OK) {
        bb_log_e(TAG, "SHA SW self-test FAILED — mining will not start");
        mining_set_sha_self_test_failed();
        return;
    }
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3
    if (sha256_hw_ahb_self_test() != BB_OK) {
        bb_log_e(TAG, "SHA AHB self-test FAILED — mining will not start");
        mining_set_sha_self_test_failed();
        return;
    }
    sha256_hw_ahb_boot_probes();
#elif CONFIG_IDF_TARGET_ESP32
    /* DPORT self-test touches SHA peripheral registers and requires the
     * peripheral clock to be enabled — caller must hold the SHA lock. */
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

void mining_stats_load_lifetime(void)
{
    uint32_t lo = 0, hi = 0;

    bb_nv_get_u32("taipanminer", "lt_shares", &mining_stats.lifetime.total_shares, 0);
    bb_nv_get_u32("taipanminer", "lt_hashes_lo", &lo, 0);
    bb_nv_get_u32("taipanminer", "lt_hashes_hi", &hi, 0);

    mining_stats.lifetime.total_hashes = ((uint64_t)hi << 32) | lo;

    bb_log_i(TAG, "loaded lifetime stats: shares=%" PRIu32 " hashes=%" PRIu64,
             mining_stats.lifetime.total_shares, (uint64_t)mining_stats.lifetime.total_hashes);
}

void mining_stats_save_lifetime(const mining_lifetime_t *snapshot)
{
    /* Three logically-atomic fields → one batched open/commit/close so the
     * SPI flash bus is held once instead of three times. Without this,
     * concurrent flash work (TLS handshake, OTA) on the share-accept hot
     * path stalled mining_hw long enough to trip the task watchdog on
     * single-core ESP32 WROOM-32 (TA-347). */
    bb_nv_batch_t batch;
    bb_err_t err = bb_nv_batch_begin(&batch, "taipanminer");
    if (err != BB_OK) {
        bb_log_w(TAG, "save_lifetime: batch_begin failed (%d)", (int)err);
        return;
    }
    bb_nv_batch_set_u32(&batch, "lt_shares",    snapshot->total_shares);
    bb_nv_batch_set_u32(&batch, "lt_hashes_lo", (uint32_t)(snapshot->total_hashes & 0xFFFFFFFFu));
    bb_nv_batch_set_u32(&batch, "lt_hashes_hi", (uint32_t)(snapshot->total_hashes >> 32));
    err = bb_nv_batch_commit(&batch);
    if (err != BB_OK) {
        bb_log_w(TAG, "save_lifetime: batch_commit failed (%d)", (int)err);
    }
}

temperature_sensor_handle_t mining_stats_temp_handle(void)
{
    return s_temp_handle;
}

// TA-344: live accessor — reads shared stats under mutex, then calls pure helper.
double mining_get_pool_effective_hashrate(void)
{
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0.0;
    double sum    = mining_stats.session.accepted_diff_sum;
    int64_t start = mining_stats.session.start_us;
    xSemaphoreGive(mining_stats.mutex);
    if (sum <= 0.0 || start <= 0) return 0.0;
    int64_t now = esp_timer_get_time();
    double uptime_s = (double)(now - start) / 1e6;
    return mining_compute_pool_effective_hps(sum, uptime_s);
}

// TA-363: rolling 1m/10m/1h pool-effective hashrate accessors
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
    mining_stats.session.start_us = esp_timer_get_time();
    mining_stats.session.accepted_diff_sum = 0.0;
#ifndef ASIC_CHIP
    mining_stats.hashrate_1m  = -1.0f;
    mining_stats.hashrate_10m = -1.0f;
    mining_stats.hashrate_1h  = -1.0f;
    mining_stats.hw_error_pct_1m  = -1.0f;
    mining_stats.hw_error_pct_10m = -1.0f;
    mining_stats.hw_error_pct_1h  = -1.0f;
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
    const esp_timer_create_args_t hw_avg_args = {
        .callback = &hw_avg_timer_cb,
        .name = "hw_avg",
    };
    BB_ERROR_CHECK(esp_timer_create(&hw_avg_args, &s_hw_avg_timer));
    BB_ERROR_CHECK(esp_timer_start_periodic(s_hw_avg_timer, 5000000ULL));
#endif
#ifdef ASIC_CHIP
    mining_stats.vcore_mv = -1;
    mining_stats.icore_ma = -1;
    mining_stats.pcore_mw = -1;
    mining_stats.fan_rpm = -1;
    mining_stats.fan_duty_pct = -1;
    mining_stats.board_temp_c = -1.0f;
    mining_stats.vin_mv = -1;
    mining_stats.vr_temp_c = -1.0f;
#ifdef ASIC_BM1370
    mining_stats.asic_freq_configured_mhz = (float)BM1370_DEFAULT_FREQ_MHZ;
#else
    mining_stats.asic_freq_configured_mhz = (float)BM1368_DEFAULT_FREQ_MHZ;
#endif
    mining_stats.asic_freq_effective_mhz = -1.0f;
#endif

    mining_stats_load_lifetime();

#if CONFIG_IDF_TARGET_ESP32S3
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    BB_ERROR_CHECK(temperature_sensor_install(&cfg, &s_temp_handle));
    BB_ERROR_CHECK(temperature_sensor_enable(s_temp_handle));
#endif
}
#endif

// Build the 64-byte block2 from header tail + SHA padding
void build_block2(uint8_t block2[64], const uint8_t header[80])
{
    memset(block2, 0, 64);
    memcpy(block2, header + 64, 16);
    block2[16] = 0x80;
    block2[62] = 0x02;
    block2[63] = 0x80;
}

// Pack target bytes [28-31] into a single word for early reject comparison
uint32_t pack_target_word0(const uint8_t target[32])
{
    return ((uint32_t)target[28] << 24) |
           ((uint32_t)target[29] << 16) |
           ((uint32_t)target[30] <<  8) |
            (uint32_t)target[31];
}

// Fill a mining_result_t from work + nonce + version info
// Stratum mining.submit version field: pool expects the rolled bits only
// (XOR delta from base version), not the full rolled version.  The pool
// reconstructs the block version by applying the mask to its base template.
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
        sprintf(result->version_hex, "%08" PRIx32, ver_bits);
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

    // Set nonce in block2 (bytes 12-15, little-endian)
    ctx->block2[12] = (uint8_t)(nonce & 0xff);
    ctx->block2[13] = (uint8_t)((nonce >> 8) & 0xff);
    ctx->block2[14] = (uint8_t)((nonce >> 16) & 0xff);
    ctx->block2[15] = (uint8_t)((nonce >> 24) & 0xff);

    // First SHA-256: clone midstate + transform block2
    uint32_t state[8];
    memcpy(state, ctx->midstate, 32);
    sha256_transform(state, ctx->block2);

    // Write first hash into block3_words
    for (int i = 0; i < 8; i++) {
        ctx->block3_words[i] = state[i];
    }

    // Second SHA-256: H0 + transform block3_words
    memcpy(state, sha256_H0, 32);
    sha256_transform_words(state, ctx->block3_words);

    // Early reject: check MSB word
    if (state[7] <= ctx->target_word0) {
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

#ifdef ESP_PLATFORM

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3
// --- Hardware SHA hash backend (AHB-bus: ESP32-S3/S2/C3) ---

typedef struct {
    uint32_t midstate_hw[8];
    uint32_t *block2_words;
} hw_backend_ctx_t;

static void hw_backend_init(hash_backend_t *b)
{
    (void)b;
    sha256_hw_init();
}

static void hw_prepare_job(hash_backend_t *b,
                           const mining_work_t *work,
                           const uint8_t block2[64])
{
    hw_backend_ctx_t *ctx = (hw_backend_ctx_t *)b->ctx;
    sha256_hw_midstate(work->header, ctx->midstate_hw);
    ctx->block2_words = (uint32_t *)block2;
    sha256_hw_pipeline_prep();  // TA-320b: prime persistent zero slots
}

static inline __attribute__((always_inline))
hash_result_t hw_hot_loop_kernel(uint32_t *midstate_hw,
                                 uint32_t *block2_words,
                                 uint32_t nonce,
                                 uint8_t hash_out[32])
{
    uint32_t digest_hw[8];
    uint32_t h7_raw = sha256_hw_mine_nonce(midstate_hw, block2_words, nonce, digest_hw);
    if ((h7_raw >> 16) == 0) {
        uint32_t state[8];
        for (int i = 0; i < 8; i++) state[i] = __builtin_bswap32(digest_hw[i]);
        mining_hash_from_state(state, hash_out);
        return HASH_CHECK;
    }
    return HASH_MISS;
}

static hash_result_t hw_hash_nonce(hash_backend_t *b,
                                   uint32_t nonce,
                                   uint8_t hash_out[32])
{
    hw_backend_ctx_t *ctx = (hw_backend_ctx_t *)b->ctx;
    return hw_hot_loop_kernel(ctx->midstate_hw, ctx->block2_words, nonce, hash_out);
}

static void hw_backend_setup(hash_backend_t *b, hw_backend_ctx_t *ctx)
{
    b->init = hw_backend_init;
    b->prepare_job = hw_prepare_job;
    b->hash_nonce = hw_hash_nonce;
    b->ctx = ctx;
}

#elif CONFIG_IDF_TARGET_ESP32
// --- Hardware SHA hash backend (DPORT-bus: classic ESP32) ---
// TA-271 step B: NerdMiner-verbatim hot loop via sha256_hw_dport_per_nonce.
// prepare_job stores the header pointer; per_nonce re-hashes block1 every call
// because classic ESP32 has no writable H registers (no midstate injection).

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
    // Early-reject threshold: MSB word of pool target (bytes 28-31 in BE byte order)
    ctx->target_word0_max = ((uint32_t)work->target[28] << 24) |
                            ((uint32_t)work->target[29] << 16) |
                            ((uint32_t)work->target[30] <<  8) |
                             (uint32_t)work->target[31];
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

#endif // CONFIG_IDF_TARGET_ESP32S3 || ... / CONFIG_IDF_TARGET_ESP32

#endif // ESP_PLATFORM

bool mine_nonce_range(hash_backend_t *backend,
                      mining_work_t *work,
                      const mine_params_t *params,
                      mining_result_t *result_out,
                      bool *found_out)
{
    uint8_t block2[64];
    build_block2(block2, work->header);
    backend->prepare_job(backend, work, block2);

#ifdef ESP_PLATFORM
    int64_t start_us = esp_timer_get_time();
    uint32_t hashes = 0;
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3
    hw_backend_ctx_t *hw_ctx = (hw_backend_ctx_t *)backend->ctx;
#endif
#endif

    for (uint32_t nonce = params->nonce_start; ; nonce++) {
        uint8_t hash[32];
#if defined(ESP_PLATFORM) && (CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3)
        // S3/S2/C3 hot loop: call kernel directly to skip the function-pointer indirection.
        hash_result_t hr = hw_hot_loop_kernel(hw_ctx->midstate_hw, hw_ctx->block2_words, nonce, hash);
#else
        hash_result_t hr = backend->hash_nonce(backend, nonce, hash);
#endif

        if (hr == HASH_CHECK) {
#ifdef ESP_PLATFORM
            // On device: full ordered validation — is_target_valid first to avoid
            // operating on corrupt targets (fixes SW-path ordering bug).
            double share_diff = 0.0;
            share_verdict_t verdict = share_validate(work, hash, &share_diff);

            if (verdict == SHARE_BELOW_TARGET) {
                // Normal miss — hash passed HW pre-filter but missed the real target.
            } else if (verdict == SHARE_INVALID_TARGET || verdict == SHARE_LOW_DIFFICULTY) {
                bb_log_e(TAG, "share sanity fail: share_diff=%.4f pool_diff=%.4f, skipping",
                         share_diff, work->difficulty);
            } else {
                // SHARE_VALID
                mining_result_t result;
                package_result(&result, work, nonce, params->ver_bits);
                bb_log_i(TAG, "share found! (nonce=%08" PRIx32 ")", nonce);

                result.share_diff = work->difficulty;  // TA-344: pool-assigned diff at issue time

                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                    if (share_diff > mining_stats.session.best_diff) {
                        mining_stats.session.best_diff = share_diff;
                    }
                    xSemaphoreGive(mining_stats.mutex);
                }

                if (result_out) {
                    *result_out = result;
                }
                if (found_out) {
                    *found_out = true;
                    return false;  // device tests: stop after first hit
                }

                if (!stratum_is_connected()) {
                    bb_log_d(TAG, "stratum disconnected, discarding share");
                } else if (xQueueSend(result_queue, &result, 0) != pdTRUE) {
                    bb_log_w(TAG, "result queue full, share dropped");
                }
            }
#else
            // In host tests: lightweight check — meets_target only (no FreeRTOS/stratum).
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
            int64_t tier1_start_us = esp_timer_get_time();  // Diag: dwell timer starts here
            mining_work_t new_work;
            if (xQueuePeek(work_queue, &new_work, 0) == pdTRUE &&
                new_work.work_seq != work->work_seq) {
                // Diag: time the swap path (acquire + prepare_job)
                int64_t swap_start_us = esp_timer_get_time();
                memcpy(work, &new_work, sizeof(*work));
                {
                    int64_t job_elapsed_us = swap_start_us - start_us;
                    double job_hashrate = (double)hashes / ((double)job_elapsed_us / 1000000.0);
                    bb_log_i(DIAG, "prev job %s: %" PRIu32 " nonces in %.2fs (%.1f kH/s)",
                             work->job_id, hashes, (double)job_elapsed_us / 1000000.0,
                             job_hashrate / 1000.0);
                }
                bb_log_i(TAG, "new job (%s)", work->job_id);
                build_block2(block2, work->header);
                sha256_hw_acquire();
                backend->prepare_job(backend, work, block2);
                start_us = esp_timer_get_time();
                int64_t swap_dur_us = start_us - swap_start_us;
                if (swap_dur_us > 50000) {
                    bb_log_w(DIAG, "job_swap: %lldms (acquire + prepare_job)", swap_dur_us / 1000);
                }
                hashes = 0;
                nonce = params->nonce_start - 1;
                continue;
            }

            sha256_hw_acquire();

            // TA-199: pause check on Tier 1 cadence (256K nonces ≈ 1.1s at tdongle SW-mining
            // speed) so the mining_pause() ACK timeout (5s) has headroom. Previously this lived
            // inside the Tier 2 block (every 1M nonces ≈ 4.5s), which was right at the edge and
            // failed under scheduling jitter — letting OTA run with mining still at full load.
            sha256_hw_release();
            if (mining_pause_check()) {
                sha256_hw_acquire();
                backend->prepare_job(backend, work, block2);
                start_us = esp_timer_get_time();
                hashes = 0;
                nonce = params->nonce_start - 1;
                continue;
            }
            sha256_hw_acquire();

            // Diag: measure time spent inside the Tier-1 dance (release → peek → acquire →
            // release → pause_check → acquire). Captures real internal stalls; immune to job-swap
            // and pause-resume paths that continue before reaching this point.
            {
                int64_t tier1_dwell_us = esp_timer_get_time() - tier1_start_us;
                if (tier1_dwell_us > 50000) {  // 50ms threshold
                    bb_log_w(DIAG, "tier1_dwell: %lldms (acq/rel + peek + pause)", tier1_dwell_us / 1000);
                }
            }

            // Tier 2: full yield (every 1M nonces)
            if (((nonce + 1) & params->log_mask) == 0) {
                int64_t elapsed_us = esp_timer_get_time() - start_us;
                if (elapsed_us > 0) {
                    double hashrate = (double)hashes / ((double)elapsed_us / 1000000.0);
                    uint32_t shares = 0;
                    if (xSemaphoreTake(mining_stats.mutex, 0) == pdTRUE) {
                        mining_stats.hw_hashrate = hashrate;
                        mining_stats_update_ema(&mining_stats.hw_ema, hashrate, esp_timer_get_time());
                        mining_stats.session.hashes += hashes;
                        shares = mining_stats.hw_shares;
                        xSemaphoreGive(mining_stats.mutex);
                    }
                    bb_log_i(TAG, "hw: %.1f kH/s | shares: %" PRIu32, hashrate / 1000.0, shares);
                }

                {
                    float temp = 0;
                    if (s_temp_handle && temperature_sensor_get_celsius(s_temp_handle, &temp) == ESP_OK) {
                        if (xSemaphoreTake(mining_stats.mutex, 0) == pdTRUE) {
                            mining_stats.temp_c = temp;
                            xSemaphoreGive(mining_stats.mutex);
                        }
                    }
                }

                // Release SHA lock so mbedTLS can use HW SHA during TLS/OTA
                sha256_hw_release();
                esp_task_wdt_reset();
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

    // SHA self-tests now run in app_main before any task starts (TA-341).
    // The gate at main.c is what skips creating this task on failure;
    // double-check here in case someone calls mining_task directly.
    if (mining_sha_self_test_failed()) {
        bb_log_e(TAG, "SHA self-test previously failed — mining task exiting");
        return;
    }

    // Set up hash backend
    hw_backend_ctx_t hw_ctx;
    hash_backend_t backend;
    hw_backend_setup(&backend, &hw_ctx);

    if (backend.init) {
        backend.init(&backend);
    }

    // Subscribe mining task to TWDT — IDLE1 monitoring is disabled because
    // this task is CPU-bound on core 1 by design. Feed at each yield point.
    esp_task_wdt_add(NULL);

    sha256_hw_acquire();

    for (;;) {
        mining_work_t work;
        sha256_hw_release();
        BaseType_t got = xQueuePeek(work_queue, &work, pdMS_TO_TICKS(5000));
        sha256_hw_acquire();
        if (got != pdTRUE) {
            esp_task_wdt_reset();
            continue;
        }

        if (!is_target_valid(work.target)) {
            bb_log_w(TAG, "invalid target from queue, waiting for fresh work");
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

#ifndef ASIC_CHIP
const miner_config_t g_miner_config = {
    .init = NULL,
    .task_fn = mining_task,
    .name = "mining_hw",
    .stack_size = 8192,
    .priority = 20,
    .core = 1,
    .extranonce2_roll = false,
    .roll_interval_ms = 0,
};
#endif
#endif // ESP_PLATFORM

