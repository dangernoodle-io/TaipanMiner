#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "work.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#endif

// Mining result sent from mining task to stratum task
typedef struct {
    char     job_id[64];
    char     extranonce2_hex[17];
    char     ntime_hex[9];
    char     nonce_hex[9];
    char     version_hex[9];   // BIP 320: rolled version as hex; empty if no rolling
} mining_result_t;

// Hash backend result
typedef enum { HASH_MISS = 0, HASH_CHECK = 1 } hash_result_t;

// Hash backend — function pointers for per-nonce operations
typedef struct hash_backend {
    void (*init)(struct hash_backend *b);
    void (*prepare_job)(struct hash_backend *b,
                        const mining_work_t *work,
                        const uint8_t block2[64]);
    hash_result_t (*hash_nonce)(struct hash_backend *b,
                                uint32_t nonce,
                                uint8_t hash_out[32]);
    void *ctx;
} hash_backend_t;

// Nonce range parameters for mine_nonce_range()
typedef struct {
    uint32_t nonce_start;
    uint32_t nonce_end;
    uint32_t yield_mask;         // e.g. 0x3FFFF — yield every N nonces
    uint32_t log_mask;           // e.g. 0xFFFFF — log hashrate every N nonces
    uint32_t ver_bits;           // current version roll offset
    uint32_t base_version;       // original version before rolling
    uint32_t version_mask;       // BIP 320 version mask
} mine_params_t;

// Miner configuration — unified dispatch from main.c
typedef struct {
    void (*init)(void);               // hardware init (NULL if none)
    void (*task_fn)(void *arg);       // FreeRTOS task entry point
    const char *name;                 // task name
    uint32_t stack_size;
    uint32_t priority;
    int core;
    bool extranonce2_roll;            // stratum needs periodic re-feed?
    uint32_t roll_interval_ms;        // 0 if not needed
} miner_config_t;

extern const miner_config_t g_miner_config;

// Mine a nonce range using the given backend.
// On device: submits results to result_queue, peeks work_queue for new jobs.
// In tests: writes first hit to result_out/found_out and returns.
// Returns true if preempted by new work.
bool mine_nonce_range(hash_backend_t *backend,
                      mining_work_t *work,
                      const mine_params_t *params,
                      mining_result_t *result_out,
                      bool *found_out);

// SW hash backend context (for host tests)
typedef struct {
    uint32_t midstate[8];
    uint32_t block3_words[16];
    uint32_t target_word0;
    uint8_t block2[64];       // local copy for nonce patching
} sw_backend_ctx_t;

// SW hash backend setup (for host tests)
void sw_backend_setup(hash_backend_t *b, sw_backend_ctx_t *ctx);
void sw_prepare_job(hash_backend_t *b,
                    const mining_work_t *work,
                    const uint8_t block2[64]);
hash_result_t sw_hash_nonce(hash_backend_t *b,
                            uint32_t nonce,
                            uint8_t hash_out[32]);

// Helper functions exposed for unit tests
uint32_t pack_target_word0(const uint8_t target[32]);
void build_block2(uint8_t block2[64], const uint8_t header[80]);
void package_result(mining_result_t *result,
                    const mining_work_t *work,
                    uint32_t nonce,
                    uint32_t base_version,
                    uint32_t ver_bits,
                    uint32_t version_mask);

#ifdef ESP_PLATFORM
// Queues (created by main, used by stratum + mining tasks)
extern QueueHandle_t work_queue;
extern QueueHandle_t result_queue;

#include "freertos/task.h"
#include "freertos/semphr.h"

// Mining task handles (for suspend/resume during OTA verification)
#ifdef ASIC_BM1370
extern TaskHandle_t asic_task_handle;
#else
extern TaskHandle_t mining_hw_task_handle;
#endif

// Shared hashrate stats (updated by both mining tasks, read for logging)
typedef struct {
    double hw_hashrate;    // latest HW hashrate (H/s)
    uint32_t hw_shares;    // hardware shares found
#ifdef ASIC_BM1370
    double asic_hashrate;  // ASIC hashrate (H/s)
    uint32_t asic_shares;  // ASIC shares found
    float asic_temp_c;     // ASIC die temperature
#endif
    SemaphoreHandle_t mutex;
} mining_stats_t;

extern mining_stats_t mining_stats;

// Initialize mining stats mutex. Call once from main before starting tasks.
void mining_stats_init(void);
#endif

#ifdef ESP_PLATFORM
// Mining task — runs on Core 1, priority 20
void mining_task(void *arg);
#endif
