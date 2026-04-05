#include "mining.h"
#include "sha256.h"
#include "work.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "sha256_hw.h"

static const char *TAG = "mining";

QueueHandle_t work_queue = NULL;
QueueHandle_t result_queue = NULL;

mining_stats_t mining_stats = {0};

void mining_stats_init(void)
{
    mining_stats.mutex = xSemaphoreCreateMutex();
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
void package_result(mining_result_t *result,
                    const mining_work_t *work,
                    uint32_t nonce,
                    uint32_t base_version,
                    uint32_t ver_bits,
                    uint32_t version_mask)
{
    strncpy(result->job_id, work->job_id, sizeof(result->job_id) - 1);
    result->job_id[sizeof(result->job_id) - 1] = '\0';
    strncpy(result->extranonce2_hex, work->extranonce2_hex, sizeof(result->extranonce2_hex) - 1);
    result->extranonce2_hex[sizeof(result->extranonce2_hex) - 1] = '\0';
    sprintf(result->ntime_hex, "%08" PRIx32, work->ntime);
    sprintf(result->nonce_hex, "%08" PRIx32, nonce);
    if (version_mask != 0 && ver_bits != 0) {
        uint32_t rolled = (base_version & ~version_mask) | (ver_bits & version_mask);
        sprintf(result->version_hex, "%08" PRIx32, rolled);
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
#endif

    for (uint32_t nonce = params->nonce_start; ; nonce++) {
        uint8_t hash[32];
        hash_result_t hr = backend->hash_nonce(backend, nonce, hash);

        if (hr == HASH_CHECK && meets_target(hash, work->target)) {
            mining_result_t result;
            package_result(&result, work, nonce,
                           params->base_version, params->ver_bits,
                           params->version_mask);

#ifdef ESP_PLATFORM
            ESP_LOGI(TAG, "share found! (nonce=%08" PRIx32 ")", nonce);

            xQueueSend(result_queue, &result, 0);
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
            if (((nonce + 1) & params->log_mask) == 0) {
                int64_t elapsed_us = esp_timer_get_time() - start_us;
                if (elapsed_us > 0) {
                    double hashrate = (double)hashes / ((double)elapsed_us / 1000000.0);
                    uint32_t shares = 0;
                    if (xSemaphoreTake(mining_stats.mutex, 0) == pdTRUE) {
                        mining_stats.hw_hashrate = hashrate;
                        shares = mining_stats.hw_shares;
                        xSemaphoreGive(mining_stats.mutex);
                    }
                    ESP_LOGI(TAG, "hw: %.1f kH/s | shares: %" PRIu32, hashrate / 1000.0, shares);
                }
            }

            // Check for new work
            mining_work_t new_work;
            if (xQueuePeek(work_queue, &new_work, 0) == pdTRUE &&
                new_work.work_seq != work->work_seq) {
                memcpy(work, &new_work, sizeof(*work));
                ESP_LOGI(TAG, "new job (%s)", work->job_id);
                build_block2(block2, work->header);
                backend->prepare_job(backend, work, block2);
                start_us = esp_timer_get_time();
                hashes = 0;
                // Reset nonce to start (will increment to nonce_start on next iteration)
                nonce = params->nonce_start - 1;
                continue;
            }

            vTaskDelay(pdMS_TO_TICKS(1));
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

    // Set up hash backend
    hw_backend_ctx_t hw_ctx;
    hash_backend_t backend;
    hw_backend_setup(&backend, &hw_ctx);

    if (backend.init) {
        backend.init(&backend);
    }

    ESP_LOGI(TAG, "mining task started");

    for (;;) {
        mining_work_t work;
        if (xQueuePeek(work_queue, &work, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "new job (%s)", work.job_id);

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
            ESP_LOGI(TAG, "rolling version: mask=%08" PRIx32 " bits=%08" PRIx32, work.version_mask, ver_bits);
        }

        ESP_LOGW(TAG, "exhausted nonce range for job %s", work.job_id);
    }
}

#ifndef ASIC_BM1370
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

