#include "mining.h"
#ifdef ASIC_CHIP
#include "board.h"
#endif
#include "sha256.h"
#include "stratum.h"
#include "work.h"
#include "bb_log.h"
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

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "sha256_hw.h"
#include "bb_nv.h"
#include "driver/temperature_sensor.h"

static const char *TAG = "mining";

QueueHandle_t work_queue = NULL;
QueueHandle_t result_queue = NULL;

mining_stats_t mining_stats = {0};

static temperature_sensor_handle_t s_temp_handle = NULL;
static volatile bool s_pause_requested = false;
static volatile bool s_pause_active = false;
static SemaphoreHandle_t s_pause_ack = NULL;
static SemaphoreHandle_t s_pause_done = NULL;
static SemaphoreHandle_t s_pause_mutex = NULL;

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
    bb_nv_set_u32("taipanminer", "lt_shares", snapshot->total_shares);
    bb_nv_set_u32("taipanminer", "lt_hashes_lo", (uint32_t)(snapshot->total_hashes & 0xFFFFFFFF));
    bb_nv_set_u32("taipanminer", "lt_hashes_hi", (uint32_t)(snapshot->total_hashes >> 32));
}

temperature_sensor_handle_t mining_stats_temp_handle(void)
{
    return s_temp_handle;
}

void mining_stats_init(void)
{
    mining_stats.mutex = xSemaphoreCreateMutex();
    mining_stats.session.start_us = esp_timer_get_time();
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

    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    ESP_ERROR_CHECK(temperature_sensor_install(&cfg, &s_temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(s_temp_handle));
}
#endif

// Store 32-bit big-endian value
static inline void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}

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
        store_be32(hash_out,      state[0]);
        store_be32(hash_out + 4,  state[1]);
        store_be32(hash_out + 8,  state[2]);
        store_be32(hash_out + 12, state[3]);
        store_be32(hash_out + 16, state[4]);
        store_be32(hash_out + 20, state[5]);
        store_be32(hash_out + 24, state[6]);
        store_be32(hash_out + 28, state[7]);
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
// --- Hardware SHA hash backend (ESP32-S3 SHA peripheral) ---

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
}

static hash_result_t hw_hash_nonce(hash_backend_t *b,
                                   uint32_t nonce,
                                   uint8_t hash_out[32])
{
    hw_backend_ctx_t *ctx = (hw_backend_ctx_t *)b->ctx;
    uint32_t digest_hw[8];
    uint32_t h7_raw = sha256_hw_mine_nonce(ctx->midstate_hw, ctx->block2_words,
                                           nonce, digest_hw);

    if ((h7_raw >> 16) == 0) {
        for (int i = 0; i < 8; i++) {
            uint32_t w = __builtin_bswap32(digest_hw[i]);
            store_be32(hash_out + i * 4, w);
        }
        return HASH_CHECK;
    }
    return HASH_MISS;
}

static void hw_backend_setup(hash_backend_t *b, hw_backend_ctx_t *ctx)
{
    b->init = hw_backend_init;
    b->prepare_job = hw_prepare_job;
    b->hash_nonce = hw_hash_nonce;
    b->ctx = ctx;
}
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
    hw_backend_ctx_t *hw_ctx = (hw_backend_ctx_t *)backend->ctx;
#endif

    for (uint32_t nonce = params->nonce_start; ; nonce++) {
        uint8_t hash[32];
#ifdef ESP_PLATFORM
        uint32_t digest_hw[8];
        uint32_t h7_raw = sha256_hw_mine_nonce(hw_ctx->midstate_hw,
                                                hw_ctx->block2_words,
                                                nonce, digest_hw);
        hash_result_t hr;
        if ((h7_raw >> 16) == 0) {
            for (int i = 0; i < 8; i++) {
                uint32_t w = __builtin_bswap32(digest_hw[i]);
                store_be32(hash + i * 4, w);
            }
            hr = HASH_CHECK;
        } else {
            hr = HASH_MISS;
        }
#else
        hash_result_t hr = backend->hash_nonce(backend, nonce, hash);
#endif

        if (hr == HASH_CHECK && meets_target(hash, work->target)) {
            mining_result_t result;
            package_result(&result, work, nonce, params->ver_bits);

#ifdef ESP_PLATFORM
            double share_diff = hash_to_difficulty(hash);

            // Sanity check: target/difficulty must be valid and share must meet pool diff
            if (work->difficulty < 0.001 || !is_target_valid(work->target) ||
                share_diff < work->difficulty * 0.5) {
                bb_log_e(TAG, "share sanity fail: share_diff=%.4f pool_diff=%.4f, skipping",
                         share_diff, work->difficulty);
                continue;
            }

            bb_log_i(TAG, "share found! (nonce=%08" PRIx32 ")", nonce);

            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                if (share_diff > mining_stats.session.best_diff) {
                    mining_stats.session.best_diff = share_diff;
                }
                xSemaphoreGive(mining_stats.mutex);
            }

            if (!stratum_is_connected()) {
                bb_log_d(TAG, "stratum disconnected, discarding share");
            } else if (xQueueSend(result_queue, &result, 0) != pdTRUE) {
                bb_log_w(TAG, "result queue full, share dropped");
            }
#endif
            if (result_out) {
                *result_out = result;
            }
            if (found_out) {
                *found_out = true;
                return false;  // in tests, stop after first hit
            }
        }

#ifdef ESP_PLATFORM
        hashes++;

        // Periodic yield + job refresh
        if (((nonce + 1) & params->yield_mask) == 0) {
            // Tier 1: lightweight new-work check (every 256K nonces)
            sha256_hw_release();
            mining_work_t new_work;
            if (xQueuePeek(work_queue, &new_work, 0) == pdTRUE &&
                new_work.work_seq != work->work_seq) {
                memcpy(work, &new_work, sizeof(*work));
                bb_log_i(TAG, "new job (%s)", work->job_id);
                build_block2(block2, work->header);
                sha256_hw_acquire();
                backend->prepare_job(backend, work, block2);
                start_us = esp_timer_get_time();
                hashes = 0;
                nonce = params->nonce_start - 1;
                continue;
            }

            sha256_hw_acquire();

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
                bool paused = mining_pause_check();
                sha256_hw_acquire();

                if (paused) {
                    backend->prepare_job(backend, work, block2);
                    start_us = esp_timer_get_time();
                    hashes = 0;
                    nonce = params->nonce_start - 1;
                    continue;
                }
            }
        }
#endif

        if (nonce == params->nonce_end) break;
    }
    return false;
}

#ifdef ESP_PLATFORM
void mining_pause_init(void)
{
    s_pause_ack = xSemaphoreCreateBinary();
    s_pause_done = xSemaphoreCreateBinary();
    s_pause_mutex = xSemaphoreCreateMutex();
}

bool mining_pause(void)
{
    if (xSemaphoreTake(s_pause_mutex, pdMS_TO_TICKS(30000)) != pdTRUE) {
        bb_log_w(TAG, "mining pause mutex timeout — another caller holds pause");
        return false;
    }
    s_pause_requested = true;
    if (xSemaphoreTake(s_pause_ack, pdMS_TO_TICKS(5000)) != pdTRUE) {
        bb_log_w(TAG, "mining pause acknowledge timeout, resetting state");
        s_pause_requested = false;
        xSemaphoreGive(s_pause_mutex);
        return false;
    }
    return true;
}

void mining_resume(void)
{
    s_pause_requested = false;
    if (s_pause_active) {
        xSemaphoreGive(s_pause_done);
    }
    xSemaphoreGive(s_pause_mutex);
}

bool mining_pause_check(void)
{
    if (!s_pause_requested) return false;
    bb_log_i(TAG, "mining paused for maintenance");
    s_pause_active = true;
    xSemaphoreGive(s_pause_ack);
    if (xSemaphoreTake(s_pause_done, pdMS_TO_TICKS(30000)) != pdTRUE) {
        bb_log_e(TAG, "mining resume timeout, resuming anyway");
    }
    s_pause_active = false;
    bb_log_i(TAG, "mining resumed (stack high water: %" PRIu32 ")",
             (uint32_t)uxTaskGetStackHighWaterMark(NULL));
    return true;
}

void mining_task(void *arg)
{
    (void)arg;

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

    bb_log_i(TAG, "mining task started");

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

        // Update pool difficulty
        if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            mining_stats.pool_difficulty = work.difficulty;
            xSemaphoreGive(mining_stats.mutex);
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

