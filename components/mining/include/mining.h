#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "work.h"
#include "bb_serialize.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Hash backend seam (TA-234) — kept as-is. A hash_backend_t is a small
// function-pointer vtable so mine_nonce_range() is agnostic to SW vs
// HW-accelerated SHA-256.
// ---------------------------------------------------------------------------

// Mining result sent from the mining engine to the delivery layer via the
// result-post seam (see mining_result_post() below).
typedef struct {
    char     job_id[64];
    char     extranonce2_hex[17];
    char     ntime_hex[9];
    char     nonce_hex[9];
    char     version_hex[9];   // BIP 320: rolled version as hex; empty if no rolling
    double   share_diff;       // pool-assigned difficulty at job-build time
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

// Miner configuration — unified dispatch from the composition root.
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
// On device: posts results via mining_result_post(), peeks work via
// mining_work_peek().
// In tests: writes first hit to result_out/found_out and returns.
// Returns true if preempted by new work.
bool mine_nonce_range(hash_backend_t *backend,
                      mining_work_t *work,
                      const mine_params_t *params,
                      mining_result_t *result_out,
                      bool *found_out);

// SW hash backend context (for host tests + native SW-mining path)
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

// ---------------------------------------------------------------------------
// Queue seam (TA-562, gated on breadboard bb_bqueue / B1-821).
//
// mine_nonce_range()/mining_task() never touch a FreeRTOS queue directly.
// They call mining_work_peek()/mining_result_post(), which dispatch through
// this small ops vtable (same shape as hash_backend_t). The production
// ESP-IDF default (no ops bound) is a no-op: peek always reports "no work",
// post always reports "dropped" — NOT a hand-rolled FreeRTOS queue. A later
// PR binds real ops here once bb_bqueue ships:
//   work_queue   -> capacity-1 latest-value mailbox (xQueueOverwrite, two
//                   concurrent peekers, never drained)
//   result_queue -> capacity-16 bounded MPSC (two producers, one draining
//                   consumer, best-effort 0-tick)
// A host/test fake sets synthetic ops via mining_set_queue_ops() to inject
// work and capture posted results without any platform dependency.
// ---------------------------------------------------------------------------

typedef struct {
    // Non-blocking peek: fills *out and returns true if work is available
    // (new job, or the job currently being mined). Never blocks.
    bool (*peek_work)(void *ctx, mining_work_t *out);
    // Best-effort post: returns true if the result was accepted for
    // delivery. May drop (return false) if the sink is full or absent.
    bool (*post_result)(void *ctx, const mining_result_t *result);
    void *ctx;
} mining_queue_ops_t;

// Bind the queue backend. NULL disables both (peek always false, post
// always false/dropped) — the default until a later PR wires bb_bqueue.
void mining_set_queue_ops(const mining_queue_ops_t *ops);

// Seam wrappers mine_nonce_range()/mining_task() call through.
bool mining_work_peek(mining_work_t *out);
bool mining_result_post(const mining_result_t *result);

// ---------------------------------------------------------------------------
// Run-state gate seam (TA-563, gated on breadboard bb_lifecycle inhibit-set).
//
// The old mining_pause*.c mutex/ack/done handshake (used to quiesce mining
// during OTA) is NOT ported. This is the clean hook a later PR attaches
// bb_lifecycle to: mine_nonce_range() checks the gate at its existing
// Tier-1 yield cadence and, when it reports true, parks briefly and
// re-checks (no ack/resume protocol -- that lands with bb_lifecycle).
// Default (unbound) always reports false: mining runs continuously.
// ---------------------------------------------------------------------------

typedef bool (*mining_should_pause_fn)(void *ctx);

void mining_set_pause_gate(mining_should_pause_fn fn, void *ctx);

// ---------------------------------------------------------------------------
// Block-found notification hook — keeps mining decoupled from LED/UI/event
// delivery. Register a zero-argument callback; NULL disables it.
// ---------------------------------------------------------------------------

void mining_set_block_found_cb(void (*cb)(void));

// Testable seam: the mining task loop calls this on every block-found event.
// Invokes the registered callback (if any). Safe to call with no callback set.
void mining_notify_block_found(void);

// ---------------------------------------------------------------------------
// SHA self-test / boot-probe state
// ---------------------------------------------------------------------------

// SHA self-test failure flag (set by mining task or backend init on FAIL)
bool mining_sha_self_test_failed(void);
void mining_set_sha_self_test_failed(void);

// Run SHA self-tests (SW + HW for the active target). Synchronous, must be
// called before any task starts so the failure flag is committed before
// mining/stratum/etc. can query it.
// Sets mining_set_sha_self_test_failed() on any failure.
void mining_run_self_tests(void);

// SHA TEXT-overlap canary state. Set by the HW backend's boot probes at
// boot; read for future diagnostics. UNKNOWN means the probe never ran.
typedef enum {
    SHA_OVERLAP_UNKNOWN = 0,
    SHA_OVERLAP_SAFE,
    SHA_OVERLAP_UNSAFE
} sha_overlap_state_t;
void mining_set_sha_overlap_safe(bool safe);
sha_overlap_state_t mining_get_sha_overlap_state(void);

// SHA H-write-during-compute canary state. Same convention as the overlap
// canary above.
void mining_set_sha_hwrite_safe(bool safe);
sha_overlap_state_t mining_get_sha_hwrite_state(void);

// ---------------------------------------------------------------------------
// Exponential moving average state for hashrate smoothing
// ---------------------------------------------------------------------------

typedef struct {
    double   value;
    int64_t  last_us;
} hashrate_ema_t;

// Update EMA with a new hashrate sample (pure math, no FreeRTOS)
void mining_stats_update_ema(hashrate_ema_t *ema, double sample, int64_t now_us);

// Pure math helper — pool-effective H/s from accepted diff sum + uptime.
// Returns 0.0 if sum <= 0 or uptime_s < 1.
double mining_compute_pool_effective_hps(double accepted_diff_sum, double uptime_s);

// Returns pool-effective H/s (accepted_diff_sum * 2^32 / uptime_s).
// Returns 0.0 when no shares yet, uptime < 1s, or mutex unavailable.
// ESP_PLATFORM only (reads FreeRTOS mutex + bb_timer).
double mining_get_pool_effective_hashrate(void);

// Rolling 1m/10m/1h pool-effective hashrate windows.
// Returns 0.0 if value < 0 (unavailable) or mutex unavailable.
double mining_get_pool_effective_1m(void);
double mining_get_pool_effective_10m(void);
double mining_get_pool_effective_1h(void);

// J/TH efficiency helper — single source of truth for all efficiency
// computations. Inputs: core+board power in mW, hashrate in GH/s.
// Returns -1.0 when hashrate <= 0 or power <= 0 (caller emits null).
// Note: mW/(GH/s) == J/TH numerically (the milli- and giga->tera factors
// cancel).
double mining_efficiency_jth(double power_mw, double hashrate_ghs);

// ---------------------------------------------------------------------------
// Per-session stats (reset on reboot) and per-pool lifetime stats
// ---------------------------------------------------------------------------

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
    double   accepted_diff_sum; // running total of accepted-share difficulties
    uint32_t blocks_found;    // blocks meeting network target this boot
    // Wall-clock (unix seconds) timestamps for the "session" headline. 0 = unset.
    int64_t  best_diff_ts;
    int64_t  last_block_ts;
} mining_session_t;

// Per-pool lifetime stats. In-RAM only in this PR (see mining_pool_stats.h).
typedef struct {
    char     host[64];
    uint16_t port;
    uint32_t shares;         // pool-accepted only
    uint64_t hashes;
    double   best_diff;      // raw firmware best for any locally-validated share
    uint32_t blocks_found;
    int64_t  last_seen_us;   // LRU key; 0 = empty slot
    // Wall-clock (unix seconds) timestamps. 0 = unset.
    int64_t  best_diff_ts;
    int64_t  last_block_ts;
} mining_pool_stat_t;

#define MINING_POOL_STATS_MAX 8

typedef struct {
    mining_pool_stat_t slots[MINING_POOL_STATS_MAX];
    /* Device-lifetime block counter that survives LRU slot eviction. Per-slot
     * blocks_found is informational; this is the durable "real solo wins
     * ever" count. */
    uint32_t           lifetime_blocks_total;
    int64_t            lifetime_last_block_ts;  // unix seconds, 0 = unset
} mining_pool_stats_t;

#ifdef ESP_PLATFORM

// Shared mining stats (updated by the mining task, read under mutex by the
// gather seam below).
typedef struct {
    double              hw_hashrate;
    hashrate_ema_t      hw_ema;
    float               temp_c;          // ESP32 die temperature
    float               hashrate_1m;       // Rolling 1m avg of hw_hashrate (-1 = unavailable)
    float               hashrate_10m;      // Rolling 10m avg of hw_hashrate (-1 = unavailable)
    float               hashrate_1h;       // Rolling 1h avg of hw_hashrate (-1 = unavailable)
    float               pool_eff_1m;       // Rolling 1m avg of pool-effective hashrate (-1 = unavailable)
    float               pool_eff_10m;      // Rolling 10m avg of pool-effective hashrate (-1 = unavailable)
    float               pool_eff_1h;       // Rolling 1h avg of pool-effective hashrate (-1 = unavailable)
    uint32_t            hw_shares;
    mining_session_t    session;
    SemaphoreHandle_t   mutex;
} mining_stats_t;

/* Guard against re-embedding large blobs (e.g. pool_stats, ~976 bytes) into
 * mining_stats_t: it sits in the hot-loop BSS region, and TA-413 measured a
 * ~1-3% hashrate regression on classic ESP32 from a layout shift caused by
 * exactly that. mining_pool_stats lives in its own file-scope global
 * (mining_pool_stats.c) for this reason. */
_Static_assert(sizeof(mining_stats_t) <= 512,
    "mining_stats_t grew beyond 512 bytes — check that pool_stats or "
    "another large blob was not re-embedded; BSS layout shift hurts hashrate");

extern mining_stats_t mining_stats;

// Initialize mining stats mutex + rolling-average state. Call once from the
// composition root before starting the mining task.
void mining_stats_init(void);

// Reset the in-RAM session stats to their boot-time defaults.
// Takes the mining_stats mutex internally.
void mining_stats_session_reset(void);

// Sample the SoC die temperature into mining_stats.temp_c. Best-effort:
// no-op on parts without a sensor and when the stats mutex is busy.
void mining_stats_sample_die_temp(void);

// Mining task — runs on Core 1 (or Core 0 on single-core targets), priority 20.
void mining_task(void *arg);

#endif // ESP_PLATFORM

// ---------------------------------------------------------------------------
// Producer surface (TA-561) — the ENTIRE delivery-facing shape of this
// component. mining.c never references bb_pub/bb_sink_*/bb_egress/bb_event/
// bb_cache/cJSON/any transport, and never registers itself anywhere.
// Registration of mining_desc + mining_gather happens in the composition
// root (src/main.c PRODUCER REGISTRATION SLOT), not here — adding a new
// delivery path (UDP push, etc.) later is one binding added at the root;
// this file is never reopened for it.
// ---------------------------------------------------------------------------

// NOTE: every numeric field here is int64_t/uint64_t/double (never a 32-bit
// int) because bb_serialize's walker reads exactly sizeof(that C type) at
// each field's offset per its bb_type_t (BB_TYPE_U64 == 8 bytes, always) --
// a uint32_t field described as BB_TYPE_U64 would be a silent OOB read.
typedef struct {
    uint64_t hashrate_hs;        // instantaneous hashrate, hashes/sec
    double   hashrate_1m_hs;     // rolling 1m average (-1 = unavailable)
    double   hashrate_10m_hs;    // rolling 10m average (-1 = unavailable)
    double   hashrate_1h_hs;     // rolling 1h average (-1 = unavailable)
    uint64_t accepted;           // session.shares
    uint64_t rejected;           // session.rejected
    double   best_diff;          // session.best_diff
    uint64_t hashes;             // session.hashes
    uint64_t blocks_found;       // session.blocks_found
    double   temp_c;             // die temperature (-1.0 = unavailable)
    int64_t  uptime_s;           // seconds since mining_stats_init()
    bool     sha_self_test_failed;
} mining_snap_t;

// Static bb_serialize descriptor (declarative offsets + types) for
// mining_snap_t. Format-agnostic — no wire-format knowledge lives here.
extern const bb_serialize_desc_t mining_desc;

// Pure gather: fills *out (a mining_snap_t) from live mining state. `ctx` is
// unused (reserved for a future multi-instance producer registry) and may
// be NULL. NO I/O, NO allocation, NO delivery knowledge. On ESP_PLATFORM
// this takes the mining_stats mutex internally (bounded, non-blocking);
// on host it reads process-static test state directly.
void mining_gather(void *ctx, void *out);

#ifndef ESP_PLATFORM
// Test hook: seed the host-side mirror mining_gather() reads from -- there
// is no live mining_stats/FreeRTOS on host, so tests inject a snapshot
// directly instead of driving the (device-only) mining task.
void mining_gather_set_snapshot_for_test(const mining_snap_t *snap);
#endif

#ifdef __cplusplus
}
#endif
