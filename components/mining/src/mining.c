#include "mining.h"
#include "sha256.h"
#include "work.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#ifdef ESP_PLATFORM
#include "sha256_hw.h"
#endif

static const char *TAG = "mining";

// SHA-256 initial hash values
static const uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

QueueHandle_t work_queue = NULL;
QueueHandle_t result_queue = NULL;

// Store 32-bit big-endian value
static inline void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}

void mining_task(void *arg)
{
    mining_work_t work;

#ifdef ESP_PLATFORM
    uint32_t midstate_hw[8];
#else
    uint32_t midstate[8];
#endif

    // Block 2: tail[16] + 0x80 + zeros + bit_length(640) = 64 bytes
    uint8_t block2[64];

#ifndef ESP_PLATFORM
    // Block 3: first_hash[32] + 0x80 + zeros + bit_length(256) = 64 bytes
    // Only needed for software SHA path; hardware path handles this internally.
    uint8_t block3[64];
    memset(block3, 0, 64);
    block3[32] = 0x80;
    block3[62] = 0x01;
    block3[63] = 0x00;
#endif

    ESP_LOGI(TAG, "mining task started");

#ifdef ESP_PLATFORM
    sha256_hw_init();
#endif

    for (;;) {
        if (xQueuePeek(work_queue, &work, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "new job: %s", work.job_id);

        // Precompute MSB target word for early reject (needed only for software path).
        // state[7] is in SHA-256 BE word order: (hash[28]<<24 | hash[29]<<16 | hash[30]<<8 | hash[31])
        // Pack target the same way so the <= comparison works.
        uint32_t target_word0 = ((uint32_t)work.target[28] << 24) |
                                ((uint32_t)work.target[29] << 16) |
                                ((uint32_t)work.target[30] << 8)  |
                                (uint32_t)work.target[31];
        ESP_LOGD(TAG, "target LE MSB: %02x%02x%02x%02x %02x%02x%02x%02x (tw0=%08" PRIx32 ")",
                 work.target[31], work.target[30], work.target[29], work.target[28],
                 work.target[27], work.target[26], work.target[25], work.target[24],
                 target_word0);

        // Compute midstate from first 64 bytes of header
#ifdef ESP_PLATFORM
        sha256_hw_midstate(work.header, midstate_hw);
#else
        memcpy(midstate, H0, sizeof(H0));
        sha256_transform(midstate, work.header);
#endif

        // Pre-build block2: tail[16] + padding for 80-byte message
        memset(block2, 0, 64);
        memcpy(block2, work.header + 64, 16);
        block2[16] = 0x80;
        block2[62] = 0x02;
        block2[63] = 0x80;

#ifdef ESP_PLATFORM
        uint32_t *block2_words = (uint32_t *)block2;
#endif

        int64_t start_us = esp_timer_get_time();
        uint32_t nonce = 0;
        uint32_t hashes = 0;

        for (nonce = 0; nonce <= 0x7FFFFFFFU; nonce++) {
#ifdef ESP_PLATFORM
            // Hardware SHA path (Phase 3 optimized: zero-bswap HW-format pipeline)
            uint32_t digest_hw[8];
            uint32_t h7_raw = sha256_hw_mine_nonce(midstate_hw, block2_words, nonce, digest_hw);

            if ((h7_raw >> 16) == 0) {
                // Potential hit — convert HW format digest to BE hash bytes
                uint8_t hash[32];
                for (int i = 0; i < 8; i++) {
                    // digest_hw is in HW LE format; bswap to get standard SHA words, then store BE
                    uint32_t w = __builtin_bswap32(digest_hw[i]);
                    store_be32(hash + i * 4, w);
                }

                if (meets_target(hash, work.target)) {
                    mining_result_t result;
                    strncpy(result.job_id, work.job_id, sizeof(result.job_id) - 1);
                    result.job_id[sizeof(result.job_id) - 1] = '\0';
                    strncpy(result.extranonce2_hex, work.extranonce2_hex, sizeof(result.extranonce2_hex) - 1);
                    result.extranonce2_hex[sizeof(result.extranonce2_hex) - 1] = '\0';
                    sprintf(result.ntime_hex, "%08" PRIx32, work.ntime);
                    sprintf(result.nonce_hex, "%08" PRIx32, nonce);

                    ESP_LOGI(TAG, "SHARE FOUND! nonce=%08" PRIx32, nonce);
#ifdef STICKMINER_DEBUG
                    // Cross-check with software SHA using standard midstate
                    uint32_t sw_midstate[8];
                    memcpy(sw_midstate, H0, sizeof(H0));
                    sha256_transform(sw_midstate, work.header);

                    uint8_t sw_block2[64];
                    memcpy(sw_block2, block2, 64);
                    sw_block2[12] = (uint8_t)(nonce & 0xff);
                    sw_block2[13] = (uint8_t)((nonce >> 8) & 0xff);
                    sw_block2[14] = (uint8_t)((nonce >> 16) & 0xff);
                    sw_block2[15] = (uint8_t)((nonce >> 24) & 0xff);
                    uint32_t sw_state[8];
                    memcpy(sw_state, sw_midstate, 32);
                    sha256_transform(sw_state, sw_block2);
                    uint8_t sw_block3[64];
                    memset(sw_block3, 0, 64);
                    store_be32(sw_block3,      sw_state[0]);
                    store_be32(sw_block3 + 4,  sw_state[1]);
                    store_be32(sw_block3 + 8,  sw_state[2]);
                    store_be32(sw_block3 + 12, sw_state[3]);
                    store_be32(sw_block3 + 16, sw_state[4]);
                    store_be32(sw_block3 + 20, sw_state[5]);
                    store_be32(sw_block3 + 24, sw_state[6]);
                    store_be32(sw_block3 + 28, sw_state[7]);
                    sw_block3[32] = 0x80;
                    sw_block3[62] = 0x01;
                    sw_block3[63] = 0x00;
                    memcpy(sw_state, H0, 32);
                    sha256_transform(sw_state, sw_block3);
                    uint8_t sw_hash[32];
                    store_be32(sw_hash,      sw_state[0]);
                    store_be32(sw_hash + 4,  sw_state[1]);
                    store_be32(sw_hash + 8,  sw_state[2]);
                    store_be32(sw_hash + 12, sw_state[3]);
                    store_be32(sw_hash + 16, sw_state[4]);
                    store_be32(sw_hash + 20, sw_state[5]);
                    store_be32(sw_hash + 24, sw_state[6]);
                    store_be32(sw_hash + 28, sw_state[7]);

                    // Full sha256d verification (no midstate optimization)
                    uint8_t verify_header[80];
                    memcpy(verify_header, work.header, 80);
                    verify_header[76] = (uint8_t)(nonce & 0xff);
                    verify_header[77] = (uint8_t)((nonce >> 8) & 0xff);
                    verify_header[78] = (uint8_t)((nonce >> 16) & 0xff);
                    verify_header[79] = (uint8_t)((nonce >> 24) & 0xff);
                    uint8_t full_hash[32];
                    sha256d(verify_header, 80, full_hash);

                    ESP_LOGI(TAG, "  HW hash:   %02x%02x%02x%02x %02x%02x%02x%02x",
                             hash[0], hash[1], hash[2], hash[3],
                             hash[4], hash[5], hash[6], hash[7]);
                    ESP_LOGI(TAG, "  SW hash:   %02x%02x%02x%02x %02x%02x%02x%02x",
                             sw_hash[0], sw_hash[1], sw_hash[2], sw_hash[3],
                             sw_hash[4], sw_hash[5], sw_hash[6], sw_hash[7]);
                    ESP_LOGI(TAG, "  full hash: %02x%02x%02x%02x %02x%02x%02x%02x",
                             full_hash[0], full_hash[1], full_hash[2], full_hash[3],
                             full_hash[4], full_hash[5], full_hash[6], full_hash[7]);
#endif
                    xQueueSend(result_queue, &result, 0);
                }
            }
#else
            // Software SHA path (native build / fallback)
            // Set nonce in block2 (bytes 12-15, little-endian)
            block2[12] = (uint8_t)(nonce & 0xff);
            block2[13] = (uint8_t)((nonce >> 8) & 0xff);
            block2[14] = (uint8_t)((nonce >> 16) & 0xff);
            block2[15] = (uint8_t)((nonce >> 24) & 0xff);

            // First SHA-256: clone midstate + transform block2
            uint32_t state[8];
            memcpy(state, midstate, 32);
            sha256_transform(state, block2);

            // Write first hash into block3 (big-endian)
            store_be32(block3,      state[0]);
            store_be32(block3 + 4,  state[1]);
            store_be32(block3 + 8,  state[2]);
            store_be32(block3 + 12, state[3]);
            store_be32(block3 + 16, state[4]);
            store_be32(block3 + 20, state[5]);
            store_be32(block3 + 24, state[6]);
            store_be32(block3 + 28, state[7]);

            // Second SHA-256: H0 + transform block3
            memcpy(state, H0, 32);
            sha256_transform(state, block3);

            // Quick reject: check MSB word (LE convention: state[7])
            if (state[7] <= target_word0) {
                uint8_t hash[32];
                store_be32(hash,      state[0]);
                store_be32(hash + 4,  state[1]);
                store_be32(hash + 8,  state[2]);
                store_be32(hash + 12, state[3]);
                store_be32(hash + 16, state[4]);
                store_be32(hash + 20, state[5]);
                store_be32(hash + 24, state[6]);
                store_be32(hash + 28, state[7]);

                if (meets_target(hash, work.target)) {
                    mining_result_t result;
                    strncpy(result.job_id, work.job_id, sizeof(result.job_id) - 1);
                    result.job_id[sizeof(result.job_id) - 1] = '\0';
                    strncpy(result.extranonce2_hex, work.extranonce2_hex, sizeof(result.extranonce2_hex) - 1);
                    result.extranonce2_hex[sizeof(result.extranonce2_hex) - 1] = '\0';
                    sprintf(result.ntime_hex, "%08" PRIx32, work.ntime);
                    sprintf(result.nonce_hex, "%08" PRIx32, nonce);

                    ESP_LOGI(TAG, "SHARE FOUND! nonce=%08" PRIx32, nonce);
                    xQueueSend(result_queue, &result, 0);
                }
            }
#endif

            hashes++;

            // Every 65536 hashes: check for new work and yield for WDT
            if (((nonce + 1) & 0xFFFF) == 0) {
                if (((nonce + 1) & 0x1FFFFF) == 0) {
                    int64_t elapsed_us = esp_timer_get_time() - start_us;
                    if (elapsed_us > 0) {
                        double hashrate = (double)hashes / ((double)elapsed_us / 1000000.0);
                        ESP_LOGI(TAG, "%.1f H/s (nonce=%08" PRIx32 ")", hashrate, nonce + 1);
                    }
                }

                mining_work_t new_work;
                if (xQueuePeek(work_queue, &new_work, 0) == pdTRUE &&
                    strcmp(new_work.job_id, work.job_id) != 0) {
                    memcpy(&work, &new_work, sizeof(work));
                    ESP_LOGI(TAG, "new job: %s", work.job_id);
                    target_word0 = ((uint32_t)work.target[28] << 24) |
                                   ((uint32_t)work.target[29] << 16) |
                                   ((uint32_t)work.target[30] << 8)  |
                                   (uint32_t)work.target[31];
#ifdef ESP_PLATFORM
                    sha256_hw_midstate(work.header, midstate_hw);
#else
                    memcpy(midstate, H0, sizeof(H0));
                    sha256_transform(midstate, work.header);
#endif
                    memset(block2, 0, 64);
                    memcpy(block2, work.header + 64, 16);
                    block2[16] = 0x80;
                    block2[62] = 0x02;
                    block2[63] = 0x80;
#ifdef ESP_PLATFORM
                    // block2_words pointer still valid since block2 is static
#endif
                    start_us = esp_timer_get_time();
                    nonce = (uint32_t)-1;
                    hashes = 0;
                    continue;
                }

                vTaskDelay(1);
            }
        }

        ESP_LOGW(TAG, "exhausted hw nonce range for job %s", work.job_id);
    }
}

void mining_task_sw(void *arg)
{
    mining_work_t work;
    uint32_t midstate[8];
    uint8_t block2[64];
    uint8_t block3[64];

    memset(block3, 0, 64);
    block3[32] = 0x80;
    block3[62] = 0x01;
    block3[63] = 0x00;

    ESP_LOGI(TAG, "software mining task started (core %d)", xPortGetCoreID());

    for (;;) {
        // Peek for work (non-destructive — HW task also reads from same queue)
        if (xQueuePeek(work_queue, &work, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "sw new job: %s", work.job_id);

        // Precompute target word for early reject
        uint32_t target_word0 = ((uint32_t)work.target[28] << 24) |
                                ((uint32_t)work.target[29] << 16) |
                                ((uint32_t)work.target[30] << 8)  |
                                (uint32_t)work.target[31];

        // Compute midstate (software SHA)
        memcpy(midstate, H0, sizeof(H0));
        sha256_transform(midstate, work.header);

        // Build block2
        memset(block2, 0, 64);
        memcpy(block2, work.header + 64, 16);
        block2[16] = 0x80;
        block2[62] = 0x02;
        block2[63] = 0x80;

        int64_t start_us = esp_timer_get_time();
        uint32_t hashes = 0;

        // SW task mines nonces 0x80000000 - 0xFFFFFFFF
        for (uint32_t nonce = 0x80000000U; nonce != 0; nonce++) {
            // Set nonce in block2
            block2[12] = (uint8_t)(nonce & 0xff);
            block2[13] = (uint8_t)((nonce >> 8) & 0xff);
            block2[14] = (uint8_t)((nonce >> 16) & 0xff);
            block2[15] = (uint8_t)((nonce >> 24) & 0xff);

            // First SHA-256
            uint32_t state[8];
            memcpy(state, midstate, 32);
            sha256_transform(state, block2);

            // Write first hash into block3
            store_be32(block3,      state[0]);
            store_be32(block3 + 4,  state[1]);
            store_be32(block3 + 8,  state[2]);
            store_be32(block3 + 12, state[3]);
            store_be32(block3 + 16, state[4]);
            store_be32(block3 + 20, state[5]);
            store_be32(block3 + 24, state[6]);
            store_be32(block3 + 28, state[7]);

            // Second SHA-256
            memcpy(state, H0, 32);
            sha256_transform(state, block3);

            // Quick reject
            if (state[7] <= target_word0) {
                uint8_t hash[32];
                store_be32(hash,      state[0]);
                store_be32(hash + 4,  state[1]);
                store_be32(hash + 8,  state[2]);
                store_be32(hash + 12, state[3]);
                store_be32(hash + 16, state[4]);
                store_be32(hash + 20, state[5]);
                store_be32(hash + 24, state[6]);
                store_be32(hash + 28, state[7]);

                if (meets_target(hash, work.target)) {
                    mining_result_t result;
                    strncpy(result.job_id, work.job_id, sizeof(result.job_id) - 1);
                    result.job_id[sizeof(result.job_id) - 1] = '\0';
                    strncpy(result.extranonce2_hex, work.extranonce2_hex, sizeof(result.extranonce2_hex) - 1);
                    result.extranonce2_hex[sizeof(result.extranonce2_hex) - 1] = '\0';
                    sprintf(result.ntime_hex, "%08" PRIx32, work.ntime);
                    sprintf(result.nonce_hex, "%08" PRIx32, nonce);

                    ESP_LOGI(TAG, "SW SHARE FOUND! nonce=%08" PRIx32, nonce);
                    xQueueSend(result_queue, &result, 0);
                }
            }

            hashes++;

            // Every 65536 hashes: check for new work and yield
            if (((nonce + 1) & 0xFFFF) == 0) {
                if (((nonce + 1) & 0x1FFFFF) == 0) {
                    int64_t elapsed_us = esp_timer_get_time() - start_us;
                    if (elapsed_us > 0) {
                        double hashrate = (double)hashes / ((double)elapsed_us / 1000000.0);
                        ESP_LOGI(TAG, "sw: %.1f H/s (nonce=%08" PRIx32 ")", hashrate, nonce + 1);
                    }
                }

                // Check for new work (peek, don't consume)
                mining_work_t new_work;
                if (xQueuePeek(work_queue, &new_work, 0) == pdTRUE &&
                    strcmp(new_work.job_id, work.job_id) != 0) {
                    memcpy(&work, &new_work, sizeof(work));
                    ESP_LOGI(TAG, "sw new job: %s", work.job_id);
                    target_word0 = ((uint32_t)work.target[28] << 24) |
                                   ((uint32_t)work.target[29] << 16) |
                                   ((uint32_t)work.target[30] << 8)  |
                                   (uint32_t)work.target[31];
                    memcpy(midstate, H0, sizeof(H0));
                    sha256_transform(midstate, work.header);
                    memset(block2, 0, 64);
                    memcpy(block2, work.header + 64, 16);
                    block2[16] = 0x80;
                    block2[62] = 0x02;
                    block2[63] = 0x80;
                    start_us = esp_timer_get_time();
                    nonce = 0x80000000U - 1;  // will increment to 0x80000000
                    hashes = 0;
                    continue;
                }

                vTaskDelay(1);
            }
        }

        ESP_LOGW(TAG, "exhausted sw nonce range for job %s", work.job_id);
    }
}
