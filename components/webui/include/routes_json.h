#pragma once

/*
 * routes_json.h — pure JSON-builder functions for TaipanMiner-owned GET routes.
 *
 * Each builder takes a fully-populated snapshot struct (no ESP-IDF calls, no I/O,
 * no mutexes) and writes into a caller-provided bb_json_t root object/array.
 * The caller is responsible for serialising and freeing the root.
 *
 * Snapshot structs are declared here so host tests can construct them directly.
 *
 * Host-compilable: no ESP-IDF includes, only bb_json + plain C types.
 */

#include "bb_json.h"
#include "bb_core.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declaration for the streaming JSON object type used by emit_* fns.
 * Including bb_http.h here would cause a bb_json_t typedef conflict when
 * both bb_json.h and bb_http.h are in the same translation unit under -std=c99.
 * Callers that need the full type must include bb_http.h themselves. */
typedef struct bb_http_json_obj_stream_s bb_http_json_obj_stream_t;

/* Pull in ASIC_CHIP definition when building on target.
 * On host (native env) ASIC_BM13xx is never defined, so ASIC_CHIP
 * stays undefined and all ASIC-gated blocks are excluded. */
#ifdef ESP_PLATFORM
#include "asic_chip.h"
#endif

#include "knot.h"

/* Include mining.h for sha_overlap_state_t (TA-320 canary states).
 * On host builds (native env), this is a pure C enum with no dependencies. */
#ifdef ESP_PLATFORM
#include "mining.h"
#else
/* Host-testable stub: enum definition for host compilation */
typedef enum {
    SHA_OVERLAP_UNKNOWN = 0,
    SHA_OVERLAP_SAFE,
    SHA_OVERLAP_UNSAFE
} sha_overlap_state_t;

/* Forward declaration for diag_bench helpers — avoids pulling in all of mining.h
 * on host (which would conflict with the sha_overlap_state_t stub above). */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * /api/stats
 * ========================================================================= */

typedef struct {
    double   hw_rate;
    double   hw_ema;
    double   best_diff;
    uint32_t hw_shares;
    float    temp_c;
    uint32_t session_shares;
    uint32_t session_rejected;
    uint32_t session_rejected_job_not_found;
    uint32_t session_rejected_low_difficulty;
    uint32_t session_rejected_duplicate;
    uint32_t session_rejected_stale_prevhash;
    uint32_t session_rejected_other;
    int32_t  session_rejected_other_last_code;
    uint32_t session_blocks_found;  /* blocks meeting network target this boot */
    int64_t  session_best_diff_ts;  /* unix seconds; 0 = unset */
    int64_t  session_last_block_ts; /* unix seconds; 0 = unset */
    int64_t  last_share_us;    /* 0 = no share yet */
    int64_t  session_start_us; /* 0 = no session */
    double   expected_ghs;     /* < 0 = unavailable, emit null */
    int64_t  now_us;           /* esp_timer_get_time() at snapshot time */
#ifndef ASIC_CHIP
    /* Non-ASIC rolling hashrate windows (H/s). < 0 = unavailable, emit null. */
    double   hashrate_1m;
    double   hashrate_10m;
    double   hashrate_1h;
    double   pool_effective_hashrate; /* < 0 = unavailable, emit null */
    /* No HW-error source on HW SHA path; always < 0 → null. Reserved for parity. */
    double   hw_error_pct_1m;
    double   hw_error_pct_10m;
    double   hw_error_pct_1h;
#endif

#ifdef ASIC_CHIP
    double   asic_rate;
    double   asic_ema;
    uint32_t asic_shares;
    float    asic_freq_cfg;    /* -1 = not yet set */
    float    asic_freq_eff;    /* -1 = not yet set */
    float    asic_total_ghs;
    float    asic_hw_error_pct;
    float    asic_total_ghs_1m;
    float    asic_total_ghs_10m;
    float    asic_total_ghs_1h;
    float    asic_hw_error_pct_1m;
    float    asic_hw_error_pct_10m;
    float    asic_hw_error_pct_1h;
    double   pool_effective_hashrate; /* < 0 = unavailable, emit null */
    bool     asic_total_valid;
    int      asic_small_cores;
    int      asic_count;
    /* per-chip telemetry — copied out of asic_chip_telemetry_t */
    int      n_chips;
#define ROUTES_JSON_MAX_CHIPS 4
    struct {
        float    total_ghs;
        float    error_ghs;
        float    hw_err_pct;
        float    domain_ghs[4];
        uint32_t total_raw;
        uint32_t error_raw;
        uint32_t total_drops;
        uint32_t error_drops;
        uint32_t domain_drops[4];
        uint64_t last_drop_us;
    } chips[ROUTES_JSON_MAX_CHIPS];
#endif
    int64_t  ts_ms;       /* monotonic ms at gather time (bb_clock_now_ms64) */
} stats_snapshot_t;

/* Emit /api/stats fields into an already-opened streaming JSON object.
 * Caller does obj_begin / obj_end; this function emits fields only. */
void emit_stats_json(bb_http_json_obj_stream_t *obj, const stats_snapshot_t *snap);

/* ============================================================================
 * /api/pool
 * ========================================================================= */

#define ROUTES_JSON_EXTRANONCE1_MAX 8
#define ROUTES_JSON_MAX_MERKLE      16
#define ROUTES_JSON_MAX_COINB       256
#define ROUTES_JSON_MAX_POOL_STATS  8

/* Per-pool statistics snapshot (mirrors mining_pool_stat_t for JSON emission) */
typedef struct {
    char     host[64];
    uint16_t port;
    uint32_t shares;
    uint64_t hashes;
    double   best_diff;
    uint32_t blocks_found;
    int64_t  last_seen_us;   /* LRU key; 0 = empty */
    int64_t  best_diff_ts;   /* unix seconds; 0 = unset */
    int64_t  last_block_ts;  /* unix seconds; 0 = unset */
} pool_stat_snapshot_t;

/* Configured pools (TA-290/TA-202). Top-level host/port/worker/wallet
 * keep reflecting the *active* connection; these expose the persisted
 * config so the UI can render both rows even when fallback is idle. */
typedef struct {
    char     host[64];
    uint16_t port;
    char     worker[64];
    char     wallet[64];
    bool     configured;  /* false → emit null sub-object */
    /* TA-306: per-pool user pref to send mining.extranonce.subscribe */
    bool     extranonce_subscribe;
    /* TA-307: per-pool user pref for UI coinbase decoding */
    bool     decode_coinbase;
} pool_cfg_summary_t;

typedef struct {
    char     host[64];
    uint16_t port;
    char     worker[64];
    char     wallet[64];

    bool     connected;
    uint32_t session_start_ago_s; /* 0 if not connected */
    bool     has_session_start;   /* false → emit null */
    double   current_difficulty;
    double   pool_effective_hashrate; /* < 0 = unavailable, emit null */
    double   pool_effective_hashrate_1m;   /* TA-363: rolling 1m window (< 0 = unavailable, emit null) */
    double   pool_effective_hashrate_10m;  /* < 0 = unavailable, emit null */
    double   pool_effective_hashrate_1h;   /* < 0 = unavailable, emit null */
    int32_t  latency_ms;          /* -1 → emit null (TA-118) */

    /* extranonce / version_mask — valid only when extranonce1_len > 0 */
    uint8_t  extranonce1[ROUTES_JSON_EXTRANONCE1_MAX];
    size_t   extranonce1_len;       /* 0 → emit nulls */
    int      extranonce2_size;
    uint32_t version_mask;          /* 0 → emit null for version_mask */

    /* notify sub-object — valid only when has_notify */
    bool     has_notify;
    char     job_id[64];
    uint8_t  prevhash[32];
    uint8_t  coinb1[ROUTES_JSON_MAX_COINB];
    size_t   coinb1_len;
    uint8_t  coinb2[ROUTES_JSON_MAX_COINB];
    size_t   coinb2_len;
    uint8_t  merkle_branches[ROUTES_JSON_MAX_MERKLE][32];
    size_t   merkle_count;
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    bool     clean_jobs;

    /* Device-lifetime block counter (never evicted on slot LRU rotation).
     * Per-pool stats array is emitted separately via emit_pool_stats_json()
     * to keep this snapshot stack-friendly on ESP32 (heap-alloc churn fix). */
    uint32_t             lifetime_blocks_total;
    int64_t              lifetime_last_block_ts;  /* unix seconds; 0 = unset */

    /* Configured pools (TA-290/TA-202). */
    pool_cfg_summary_t configured[2];
    int                active_pool_idx;  /* -1 if not connected */
    /* TA-306: enum value (off/pending/active/rejected) for the active session */
    int                extranonce_subscribe_status;
} pool_snapshot_t;

/* Emit /api/pool fields into an already-opened streaming JSON object.
 * Emits the full pool object including configured{}, notify{}, and stats[].
 * Caller does obj_begin / obj_end; this function emits fields only. */
void emit_pool_json(bb_http_json_obj_stream_t *obj,
                    const pool_snapshot_t *s,
                    const pool_stat_snapshot_t *stats,
                    size_t stats_count);

// ---------------------------------------------------------------------------
// mining_rates -- periodic hashrate + shares snapshot (B1-352 bb_pub source)
// All fields: double, sentinel -1.0 = unavailable/null.
// ---------------------------------------------------------------------------
typedef struct {
    double  hashrate_hs;            // EMA hashrate in H/s
    double  shares;                 // accepted shares this session
    double  rejected;               // rejected shares this session
    double  pool_effective_hs;      // pool-effective hashrate in H/s (-1 = unavailable)
#ifdef ASIC_CHIP
    double  asic_hashrate_hs;       // ASIC-reported hashrate in H/s (-1 = unavailable)
    double  asic_total_ghs;         // ASIC total GH/s from reg (-1 = unavailable)
#endif
    int64_t ts_ms;                  // monotonic ms at gather time (bb_clock_now_ms64)
} mining_rates_snapshot_t;

void emit_mining_rates_json(bb_json_t obj, const mining_rates_snapshot_t *snap);

// ---------------------------------------------------------------------------
// pool_pub -- periodic pool connection state snapshot (B1-352 bb_pub source)
// Fields: low-churn connection state + difficulty + extranonce negotiation.
// Intentionally excludes per-pool stats array (those are low-churn config data
// served only via REST /api/pool). sentinel: int -1 = null, bool/string as-is.
// ---------------------------------------------------------------------------
typedef struct {
    bool    connected;
    double  current_difficulty;    // -1.0 = unavailable
    double  latency_ms;            // -1.0 = unavailable
    int     active_pool_idx;       // -1 = none
    double  pool_effective_hs;     // -1.0 = unavailable
    double  pool_effective_hs_1m;  // -1.0 = unavailable
    double  pool_effective_hs_10m; // -1.0 = unavailable
    double  pool_effective_hs_1h;  // -1.0 = unavailable
    int64_t ts_ms;                 // monotonic ms at gather time (bb_clock_now_ms64)
} pool_pub_snapshot_t;

void emit_pool_pub_json(bb_json_t obj, const pool_pub_snapshot_t *snap);

// ---------------------------------------------------------------------------
// sensors_miner -- periodic ASIC power-extender live fields (B1-352 bb_pub source)
// ASIC_CHIP only. Mirrors the taipan_power_extender fields that change on every
// bb_pub tick: vcore/icore/pcore/vr_temp + efficiency. Excludes capability
// flags (vcore_restart_count, vcore_fault_held) -- those are in health.alerts.
// sentinel: -1.0 = null
// ---------------------------------------------------------------------------
#ifdef ASIC_CHIP
typedef struct {
    double vcore_mv;              // VR output voltage in mV (-1 = null)
    double icore_ma;              // VR output current in mA (-1 = null)
    double pcore_mw;              // Total board power in mW (-1 = null)
    double vr_temp_c;             // VR temperature in degrees C (-1 = null)
    double efficiency_jth;        // instantaneous efficiency J/TH (-1 = null)
    double efficiency_jth_1m;     // 1m rolling average (-1 = null)
    double efficiency_jth_10m;    // 10m rolling average (-1 = null)
    double efficiency_jth_1h;     // 1h rolling average (-1 = null)
    double   vin_mv;              // Input voltage in mV (-1 = null)
    bool     vin_low;             // true when VIN below threshold
    bool     vin_low_valid;       // false when vin_mv is -1
    // VIN-sag observability (ASIC_CHIP only; from bb_power_tps546_read_status)
    uint16_t sag_count;           // cumulative sag event count
    int      vin_min_mv;          // lowest VIN seen in mV (INT_MAX = none seen)
    bool     vin_uv_latched;      // true when TPS546_FAULT_VIN_UV bit is set
    uint64_t last_sag_ms;         // uptime-ms of last sag (0 = none)
    uint64_t vcore_last_restart_ms; // uptime-ms of last vcore WD recovery (0 = none)
    // Fields from taipan_power_extender not in the base bb_pub topic:
    double   expected_efficiency_jth; // < 0 = null
    uint32_t vcore_restart_count;
    bool     vcore_fault_held;
    int64_t  ts_ms;                   // monotonic ms at gather time (bb_clock_now_ms64)
} sensors_miner_snapshot_t;

void emit_sensors_miner_json(bb_json_t obj, const sensors_miner_snapshot_t *snap);
#endif /* ASIC_CHIP */

/* ============================================================================
 * /api/diag/asic  (ASIC_CHIP only)
 * ========================================================================= */

#ifdef ASIC_CHIP
typedef enum {
    ROUTES_JSON_DROP_KIND_TOTAL  = 0,
    ROUTES_JSON_DROP_KIND_ERROR  = 1,
    ROUTES_JSON_DROP_KIND_DOMAIN = 2,
} routes_json_drop_kind_t;

#define ROUTES_JSON_DROP_LOG_CAP 16

typedef struct {
    uint64_t ts_us;
    uint8_t  chip_idx;
    uint8_t  kind;         /* routes_json_drop_kind_t */
    uint8_t  domain_idx;
    uint8_t  asic_addr;
    float    ghs;
    uint32_t delta;
    float    elapsed_s;
} routes_json_drop_event_t;

typedef struct {
    routes_json_drop_event_t drops[ROUTES_JSON_DROP_LOG_CAP];
    size_t   n_drops;
    uint64_t now_us;
} diag_asic_snapshot_t;

/* Emit /api/diag/asic fields into an already-opened streaming JSON object.
 * Emits the recent_drops[] array. Caller does obj_begin / obj_end. */
void emit_diag_asic_json(bb_http_json_obj_stream_t *obj, const diag_asic_snapshot_t *s);
#endif /* ASIC_CHIP */

/* ============================================================================
 * /api/knot
 * ========================================================================= */

#define ROUTES_JSON_MAX_PEERS 32


/* Build a JSON object for a single peer. Returns a fresh bb_json_t object
 * that the caller must free. Used by the live knot_handler runtime path. */
bb_json_t build_knot_peer_json(const knot_peer_t *peer, int64_t now_us);

/* ============================================================================
 * /api/settings GET
 * ========================================================================= */

typedef struct {
    char     hostname[33];
    bool     display_en;
    bool     ota_skip_check;
    bool     mdns_en;
    bool     knot_en;
    bool     led_heartbeat_en;
    /* Read-only mirror of the bb_cfg NVS "provisioned" u8 key. Exposed so
     * taipan-cli (direct-NVS-flash workflow) can read/round-trip against
     * the canonical schema; not settable via HTTP. */
    bool     provisioned;
} settings_snapshot_t;

/* Emit /api/settings fields into an already-opened streaming JSON object.
 * Caller does obj_begin / obj_end; this function emits fields only. */
void emit_settings_json(bb_http_json_obj_stream_t *obj, const settings_snapshot_t *snap);

/* /api/power and /api/fan are now owned by BB (bb_power_routes, bb_fan_routes).
 * TM injects mining-specific fields via route-extenders registered in routes.c.
 * power_snapshot_t, fan_snapshot_t, emit_power_json, emit_fan_json are removed. */

/* ============================================================================
 * /api/diag/benchmark  (TA-33)
 * ========================================================================= */

#define DIAG_BENCH_ITERS_DEFAULT  10000U
#define DIAG_BENCH_ITERS_MIN       1000U
#define DIAG_BENCH_ITERS_MAX     100000U

typedef struct {
    uint32_t    iters;
    int64_t     duration_us;
    double      us_per_op;          /* per-SHA-block-op — steady-state only when settled=true */
    double      khs;                /* nonce-domain: settled_iters*1000/settled_us (or fallback) */
    double      sha_ops_per_sec;    /* per-SHA-block-op ops/s: 1e6 / us_per_op */
    const char *backend;        /* "sw", "ahb", or "dport" — static string */
    sha_overlap_state_t text_overlap_state;  /* SHA_OVERLAP_UNKNOWN | SAFE | UNSAFE */
    sha_overlap_state_t h_write_state;       /* SHA_OVERLAP_UNKNOWN | SAFE | UNSAFE */
    bool        asic_active;    /* only meaningful when ASIC_CHIP is defined */
    bool        has_asic_active;/* true only on ASIC boards */
    /* Adaptive convergence fields (from sha_bench_result_t): */
    bool        settled;             /* true if steady-state was detected */
    uint32_t    settled_after_iters; /* 0 if not settled */
    uint32_t    settled_iters;       /* count of iters in settled portion */
    int64_t     settled_total_us;    /* wall time of settled portion */
} diag_bench_snapshot_t;

/* Parse JSON body and extract iters (default DIAG_BENCH_ITERS_DEFAULT if absent).
 * Returns BB_OK and sets *out_iters on success.
 * Returns BB_ERR_INVALID_ARG when body is not valid JSON or field is wrong type.
 * Returns BB_ERR_NO_SPACE when iters is out of range [ITERS_MIN, ITERS_MAX]. */
bb_err_t diag_bench_parse_request(const char *body, int body_len, uint32_t *out_iters);

/* Emit /api/diag/benchmark fields into an already-opened streaming JSON object.
 * Caller does obj_begin / obj_end; this function emits fields only. */
void emit_diag_bench_json(bb_http_json_obj_stream_t *obj, const diag_bench_snapshot_t *s);

#ifdef __cplusplus
}
#endif
