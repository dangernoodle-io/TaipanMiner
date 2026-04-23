#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "asic_chip.h"
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
                    uint32_t ver_bits);

// Exponential moving average state for hashrate smoothing
typedef struct {
    double   value;
    int64_t  last_us;
} hashrate_ema_t;

// Per-session stats (reset on reboot)
typedef struct {
    uint32_t shares;
    uint64_t hashes;
    uint32_t rejected;
    int64_t  start_us;
    int64_t  last_share_us;   // 0 = no share yet
    double   best_diff;       // highest share difficulty (raw value)
} mining_session_t;

// Lifetime stats (persisted to NVS)
typedef struct {
    uint32_t total_shares;
    uint64_t total_hashes;
} mining_lifetime_t;

// Update EMA with a new hashrate sample (pure math, no FreeRTOS)
void mining_stats_update_ema(hashrate_ema_t *ema, double sample, int64_t now_us);

#ifdef ESP_PLATFORM
// Queues (created by main, used by stratum + mining tasks)
extern QueueHandle_t work_queue;
extern QueueHandle_t result_queue;

#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/temperature_sensor.h"

// Cooperative mining pause (for OTA — avoids mid-hash vTaskSuspend)
void mining_pause_init(void);
bool mining_pause(void);
void mining_resume(void);
// Check if a pause has been requested and block until resumed; returns true if paused
bool mining_pause_check(void);

// Mining task handles
#ifdef ASIC_CHIP
extern TaskHandle_t asic_task_handle;
#else
extern TaskHandle_t mining_hw_task_handle;
#endif

// Shared mining stats (updated by mining/stratum tasks, read by HTTP + logging)
typedef struct {
    double              hw_hashrate;
    hashrate_ema_t      hw_ema;
    float               temp_c;          // ESP32-S3 die temperature
#ifdef ASIC_CHIP
    double              asic_hashrate;
    hashrate_ema_t      asic_ema;
    uint32_t            asic_shares;
    float               asic_temp_c;     // ASIC die temperature via EMC2101 external diode
    float               board_temp_c;    // Board/ambient temperature via EMC2101 internal sensor
    int                 vcore_mv;
    int                 icore_ma;
    int                 pcore_mw;
    int                 fan_rpm;
    int                 fan_duty_pct;
    int                 vin_mv;          // Input voltage (5V rail) via TPS546
    float               vr_temp_c;       // Voltage regulator die temperature via TPS546
    float               asic_freq_configured_mhz;
    float               asic_freq_effective_mhz;
    float               asic_total_ghs;      // ASIC-reported total hashrate (sum across chips), from REG_TOTAL_COUNT
    float               asic_hw_error_pct;   // HW error % from REG_ERROR_COUNT vs total
    float               asic_total_ghs_1m;   // Rolling 1m average of asic_total_ghs
    float               asic_total_ghs_10m;  // Rolling 10m average of asic_total_ghs
    float               asic_total_ghs_1h;   // Rolling 1h average of asic_total_ghs
    float               asic_hw_error_pct_1m;  // Rolling 1m average of asic_hw_error_pct
    float               asic_hw_error_pct_10m; // Rolling 10m average of asic_hw_error_pct
    float               asic_hw_error_pct_1h;  // Rolling 1h average of asic_hw_error_pct
#endif
    uint32_t            hw_shares;
    double              pool_difficulty;
    mining_session_t    session;
    mining_lifetime_t   lifetime;
    SemaphoreHandle_t   mutex;
} mining_stats_t;

extern mining_stats_t mining_stats;

// Initialize mining stats mutex. Call once from main before starting tasks.
void mining_stats_init(void);

void mining_stats_load_lifetime(void);
void mining_stats_save_lifetime(const mining_lifetime_t *snapshot);
temperature_sensor_handle_t mining_stats_temp_handle(void);
#endif

#ifdef ESP_PLATFORM
// Mining task — runs on Core 1, priority 20
void mining_task(void *arg);
#endif

#ifdef ASIC_CHIP
// Per-chip telemetry snapshot for /api/stats (TA-192 phase 2).
// Callers provide a buffer dimensioned by BOARD_ASIC_COUNT chips × 4 domains.
typedef struct {
    float    total_ghs;
    float    error_ghs;
    float    hw_err_pct;       // point-sample error_ghs / total_ghs * 100
    float    domain_ghs[4];
    // Raw BM1370 register values at last poll (diagnostic — see TA-198).
    uint32_t total_raw;
    uint32_t error_raw;
} asic_chip_telemetry_t;

// Fill `out[]` with current per-chip snapshot (BOARD_ASIC_COUNT entries).
// Returns number of entries filled.
int asic_task_get_chip_telemetry(asic_chip_telemetry_t *out, int max_chips);
#endif
