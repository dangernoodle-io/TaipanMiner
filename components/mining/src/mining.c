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
    uint32_t block3_words[16];
    memset(block3_words, 0, sizeof(block3_words));
    block3_words[8]  = 0x80000000U;  // 0x80 padding byte in BE word
    block3_words[15] = 0x00000100U;  // bit length 256 in BE word
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
        memcpy(midstate, sha256_H0, sizeof(sha256_H0));
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

        // Version rolling outer loop (BIP 320)
        uint32_t base_version = work.version;
        uint32_t ver_bits = 0;  // current version roll offset

        for (;;) {  // version rolling outer loop
            // Apply version roll
            if (work.version_mask != 0 && ver_bits != 0) {
                uint32_t rolled = (base_version & ~work.version_mask) | (ver_bits & work.version_mask);
                // Update version in header (bytes 0-3, little-endian)
                work.header[0] = rolled & 0xFF;
                work.header[1] = (rolled >> 8) & 0xFF;
                work.header[2] = (rolled >> 16) & 0xFF;
                work.header[3] = (rolled >> 24) & 0xFF;
                // Recompute midstate (version is in first 64 bytes)
                sha256_hw_midstate(work.header, midstate_hw);
            }

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
                    if (work.version_mask != 0 && ver_bits != 0) {
                        uint32_t rolled = (base_version & ~work.version_mask) | (ver_bits & work.version_mask);
                        sprintf(result.version_hex, "%08" PRIx32, rolled);
                    } else {
                        result.version_hex[0] = '\0';
                    }

                    ESP_LOGI(TAG, "SHARE FOUND! nonce=%08" PRIx32, nonce);
#ifdef STICKMINER_DEBUG
                    // Cross-check with software SHA using standard midstate
                    uint32_t sw_midstate[8];
                    memcpy(sw_midstate, sha256_H0, sizeof(sha256_H0));
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
                    memcpy(sw_state, sha256_H0, 32);
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

            // Write first hash into block3_words (big-endian)
            block3_words[0] = state[0];
            block3_words[1] = state[1];
            block3_words[2] = state[2];
            block3_words[3] = state[3];
            block3_words[4] = state[4];
            block3_words[5] = state[5];
            block3_words[6] = state[6];
            block3_words[7] = state[7];

            // Second SHA-256: H0 + transform block3_words
            memcpy(state, sha256_H0, 32);
            sha256_transform_words(state, block3_words);

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
            if (((nonce + 1) & 0x3FFFF) == 0) {
                if (((nonce + 1) & 0xFFFFF) == 0) {
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
                    memcpy(midstate, sha256_H0, sizeof(sha256_H0));
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

                vTaskDelay(pdMS_TO_TICKS(1));
            }

            }  // end nonce loop

            // Nonce range exhausted — try rolling version
            if (work.version_mask == 0) break;  // no rolling, done
            ver_bits = next_version_roll(ver_bits, work.version_mask);
            if (ver_bits == 0) break;  // wrapped around, all versions exhausted
            ESP_LOGI(TAG, "rolling version: mask=%08" PRIx32 " bits=%08" PRIx32, work.version_mask, ver_bits);
        }  // end version rolling outer loop

        ESP_LOGW(TAG, "exhausted hw nonce range for job %s", work.job_id);
    }
}

void mining_task_sw(void *arg)
{
    mining_work_t work;
    uint32_t midstate[8];
    uint8_t block2[64];
    uint32_t block3_words[16];

    memset(block3_words, 0, sizeof(block3_words));
    block3_words[8]  = 0x80000000U;  // 0x80 padding byte in BE word
    block3_words[15] = 0x00000100U;  // bit length 256 in BE word

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
        memcpy(midstate, sha256_H0, sizeof(sha256_H0));
        sha256_transform(midstate, work.header);

        // Build block2
        memset(block2, 0, 64);
        memcpy(block2, work.header + 64, 16);
        block2[16] = 0x80;
        block2[62] = 0x02;
        block2[63] = 0x80;

        // Version rolling outer loop (BIP 320)
        uint32_t base_version = work.version;
        uint32_t ver_bits = 0;  // current version roll offset

        for (;;) {  // version rolling outer loop
            // Apply version roll
            if (work.version_mask != 0 && ver_bits != 0) {
                uint32_t rolled = (base_version & ~work.version_mask) | (ver_bits & work.version_mask);
                // Update version in header (bytes 0-3, little-endian)
                work.header[0] = rolled & 0xFF;
                work.header[1] = (rolled >> 8) & 0xFF;
                work.header[2] = (rolled >> 16) & 0xFF;
                work.header[3] = (rolled >> 24) & 0xFF;
                // Recompute midstate (version is in first 64 bytes)
                memcpy(midstate, sha256_H0, sizeof(sha256_H0));
                sha256_transform(midstate, work.header);
            }

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

            // Write first hash into block3_words
            block3_words[0] = state[0];
            block3_words[1] = state[1];
            block3_words[2] = state[2];
            block3_words[3] = state[3];
            block3_words[4] = state[4];
            block3_words[5] = state[5];
            block3_words[6] = state[6];
            block3_words[7] = state[7];

            // Second SHA-256
            memcpy(state, sha256_H0, 32);
            sha256_transform_words(state, block3_words);

            // Quick reject
            if ((state[7] >> 16) == 0 && state[7] <= target_word0) {
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
                    if (work.version_mask != 0 && ver_bits != 0) {
                        uint32_t rolled = (base_version & ~work.version_mask) | (ver_bits & work.version_mask);
                        sprintf(result.version_hex, "%08" PRIx32, rolled);
                    } else {
                        result.version_hex[0] = '\0';
                    }

                    ESP_LOGI(TAG, "SW SHARE FOUND! nonce=%08" PRIx32, nonce);
                    xQueueSend(result_queue, &result, 0);
                }
            }

            hashes++;

            // Every 65536 hashes: check for new work and yield
            if (((nonce + 1) & 0x3FFFF) == 0) {
                if (((nonce + 1) & 0xFFFFF) == 0) {
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
                    memcpy(midstate, sha256_H0, sizeof(sha256_H0));
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

                vTaskDelay(pdMS_TO_TICKS(1));
            }
            }  // end sw nonce loop

            // Nonce range exhausted — try rolling version
            if (work.version_mask == 0) break;  // no rolling, done
            ver_bits = next_version_roll(ver_bits, work.version_mask);
            if (ver_bits == 0) break;  // wrapped around, all versions exhausted
            ESP_LOGI(TAG, "sw rolling version: mask=%08" PRIx32 " bits=%08" PRIx32, work.version_mask, ver_bits);
        }  // end version rolling outer loop

        ESP_LOGW(TAG, "exhausted sw nonce range for job %s", work.job_id);
    }
}