#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "asic_chip.h"
#include "work.h"
#include "mining_pause_io.h"

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
    double   share_diff;       // TA-344: pool-assigned difficulty at job-build time
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
void pack_double(double v, uint32_t *hi, uint32_t *lo);
double unpack_double(uint32_t hi, uint32_t lo);

// Cooperative mining pause (for OTA — avoids mid-hash vTaskSuspend)
void mining_pause_init(const mining_pause_sync_ops_t *ops);
bool mining_pause(void);
void mining_resume(void);
// Check if a pause has been requested and block until resumed; returns true if paused
bool mining_pause_check(void);
// Non-blocking query: returns true if a pause has been requested
bool mining_pause_pending(void);

// SHA self-test failure flag (set by mining task or asic_init on FAIL)
bool mining_sha_self_test_failed(void);
void mining_set_sha_self_test_failed(void);

// Run SHA self-tests (SW + HW for the active target). Synchronous, must
// be called from app_main before any task starts so the failure flag is
// committed before stratum / mining / etc. can query it (TA-341).
// Sets mining_set_sha_self_test_failed() on any failure.
void mining_run_self_tests(void);

// Boot-time SHA peripheral microbench result (TA-339).
// Set once by sha256_hw_microbench() at boot; read by /api/info.
// Returns false on boards without HW SHA microbench (e.g. D0/DPORT until
// equivalent probe lands there).
void mining_set_sha_microbench(double us_per_op, double khs_ceiling);
bool mining_get_sha_microbench(double *us_per_op, double *khs_ceiling);

// Per-board expected hashrate ceiling in GH/s.
// ASIC: asic_freq_mhz × BOARD_SMALL_CORES × BOARD_ASIC_COUNT / 1000.
// Non-ASIC: HW SHA microbench ceiling (sha_khs_ceiling / 1e6); param ignored.
// Returns false when no source is available (microbench not yet run, or
// ASIC freq not yet configured / <= 0).
bool mining_get_expected_ghs(float asic_freq_mhz, double *out_ghs);

// SHA TEXT-overlap canary state (TA-320a). Set by sha256_hw_overlap_canary
// at boot; read by /api/info. UNKNOWN means probe never ran (D0/ASIC boards).
typedef enum {
    SHA_OVERLAP_UNKNOWN = 0,
    SHA_OVERLAP_SAFE,
    SHA_OVERLAP_UNSAFE
} sha_overlap_state_t;
void mining_set_sha_overlap_safe(bool safe);
sha_overlap_state_t mining_get_sha_overlap_state(void);

// SHA H-write-during-compute canary state (TA-320a). Set by
// sha256_hw_hwrite_canary at boot; read by /api/info.
void mining_set_sha_hwrite_safe(bool safe);
sha_overlap_state_t mining_get_sha_hwrite_state(void);

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
    uint32_t rejected_job_not_found;    // stratum code 21
    uint32_t rejected_low_difficulty;   // stratum code 23
    uint32_t rejected_duplicate;        // stratum code 22
    uint32_t rejected_stale_prevhash;   // stratum code 25
    uint32_t rejected_other;            // any other code (or no code)
    int32_t  rejected_other_last_code;  // last seen "other" code (-1 if none/unparseable)
    int64_t  start_us;
    int64_t  last_share_us;   // 0 = no share yet
    double   best_diff;       // highest share difficulty (raw value)
    double   accepted_diff_sum; // TA-344: running total of accepted-share difficulties
} mining_session_t;

// Lifetime stats (persisted to NVS)
typedef struct {
    uint32_t total_shares;
    uint64_t total_hashes;
    double   best_diff;
} mining_lifetime_t;

// Update EMA with a new hashrate sample (pure math, no FreeRTOS)
void mining_stats_update_ema(hashrate_ema_t *ema, double sample, int64_t now_us);

// TA-344: Pure math helper — pool-effective H/s from accepted diff sum + uptime.
// Returns 0.0 if sum <= 0 or uptime_s < 1.
double mining_compute_pool_effective_hps(double accepted_diff_sum, double uptime_s);

// TA-344: Returns pool-effective H/s (accepted_diff_sum * 2^32 / uptime_s).
// Returns 0.0 when no shares yet, uptime < 1s, or mutex unavailable.
// ESP_PLATFORM only (reads FreeRTOS mutex + esp_timer).
double mining_get_pool_effective_hashrate(void);

// TA-363: Rolling 1m/10m/1h pool-effective hashrate windows.
// Returns 0.0 if value < 0 (unavailable) or mutex unavailable.
double mining_get_pool_effective_1m(void);
double mining_get_pool_effective_10m(void);
double mining_get_pool_effective_1h(void);

#ifdef ESP_PLATFORM
// Queues (created by main, used by stratum + mining tasks)
extern QueueHandle_t work_queue;
extern QueueHandle_t result_queue;

#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/temperature_sensor.h"

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
#ifndef ASIC_CHIP
    float               hashrate_1m;       // Rolling 1m avg of hw_hashrate (-1 = unavailable)
    float               hashrate_10m;      // Rolling 10m avg of hw_hashrate (-1 = unavailable)
    float               hashrate_1h;       // Rolling 1h avg of hw_hashrate (-1 = unavailable)
    float               hw_error_pct_1m;   // No source on HW SHA path; always -1 for now
    float               hw_error_pct_10m;
    float               hw_error_pct_1h;
#endif
    /* TA-363: rolling pool-effective windows (both non-ASIC and ASIC builds) */
    float               pool_eff_1m;       // Rolling 1m avg of pool-effective hashrate (-1 = unavailable)
    float               pool_eff_10m;      // Rolling 10m avg of pool-effective hashrate (-1 = unavailable)
    float               pool_eff_1h;       // Rolling 1h avg of pool-effective hashrate (-1 = unavailable)
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
    float               pcore_mw_1m;         // Rolling 1m average of pcore_mw
    float               pcore_mw_10m;        // Rolling 10m average of pcore_mw
    float               pcore_mw_1h;         // Rolling 1h average of pcore_mw
#endif
    uint32_t            hw_shares;
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
    // TA-223: drop counters for sanity-fail diagnostics
    uint32_t total_drops;
    uint32_t error_drops;
    uint32_t domain_drops[4];
    // TA-237: timestamp of most-recent drop (any kind); 0 if never. Drives UI
    // self-heal: corrupt badge clears when last_drop_ago_s exceeds threshold.
    uint64_t last_drop_us;
} asic_chip_telemetry_t;

// Fill `out[]` with current per-chip snapshot (BOARD_ASIC_COUNT entries).
// Returns number of entries filled.
int asic_task_get_chip_telemetry(asic_chip_telemetry_t *out, int max_chips);

// TA-141: Autofan thermal aggregation telemetry — retrieve filtered die/vr temps and which is driving PID.
// Any of the output pointers may be NULL to skip reading that value.
void asic_task_get_autofan_telemetry(float *die_ema_c, float *vr_ema_c, float *pid_input_c, const char **pid_input_src);

// TA-238: snapshot of recent telemetry-drop events declared in asic_drop_log.h.
#endif
