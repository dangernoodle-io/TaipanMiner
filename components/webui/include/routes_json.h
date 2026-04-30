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
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Pull in ASIC_CHIP definition when building on target.
 * On host (native env) ASIC_BM13xx is never defined, so ASIC_CHIP
 * stays undefined and all ASIC-gated blocks are excluded. */
#ifdef ESP_PLATFORM
#include "asic_chip.h"
#include "bb_mdns.h"  /* Needed before knot.h so bb_mdns_txt_t is defined */
#endif

#include "knot.h"

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
    int64_t  last_share_us;    /* 0 = no share yet */
    int64_t  session_start_us; /* 0 = no session */
    uint32_t lifetime_shares;
    int64_t  now_us;           /* esp_timer_get_time() at snapshot time */

#ifdef ASIC_CHIP
    double   asic_rate;
    double   asic_ema;
    uint32_t asic_shares;
    float    asic_temp_c;
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
} stats_snapshot_t;

void build_stats_json(const stats_snapshot_t *s, bb_json_t root);

/* ============================================================================
 * /api/pool
 * ========================================================================= */

#define ROUTES_JSON_EXTRANONCE1_MAX 8
#define ROUTES_JSON_MAX_MERKLE      16
#define ROUTES_JSON_MAX_COINB       256

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

    /* Configured pools (TA-290/TA-202). */
    pool_cfg_summary_t configured[2];
    int                active_pool_idx;  /* -1 if not connected */
} pool_snapshot_t;

void build_pool_json(const pool_snapshot_t *s, bb_json_t root);

/* ============================================================================
 * /api/diag/asic
 * ========================================================================= */

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

void build_diag_asic_json(const diag_asic_snapshot_t *s, bb_json_t root);

/* ============================================================================
 * /api/knot
 * ========================================================================= */

#define ROUTES_JSON_MAX_PEERS 32


/* Writes a JSON array into root (which must be bb_json_arr_new()).
 * Walks knot_peer_t directly, mapping field names at JSON write time. */
void build_knot_json(const knot_peer_t *peers, size_t n_peers, int64_t now_us, bb_json_t root);

/* ============================================================================
 * /api/settings GET
 * ========================================================================= */

typedef struct {
    char     hostname[33];
    bool     display_en;
    bool     ota_skip_check;
} settings_snapshot_t;

void build_settings_json(const settings_snapshot_t *s, bb_json_t root);

/* ============================================================================
 * /api/power  (ASIC_CHIP only)
 * ========================================================================= */

#ifdef ASIC_CHIP
typedef struct {
    int    vcore_mv;        /* < 0 → null */
    int    icore_ma;        /* < 0 → null */
    int    pcore_mw;        /* < 0 → null */
    int    vin_mv;          /* < 0 → null */
    double asic_hashrate;   /* 0 → efficiency_jth = null */
    float  board_temp_c;    /* < 0 → null */
    float  vr_temp_c;       /* < 0 → null */
    int    nominal_vin_mv;  /* board constant (BOARD_NOMINAL_VIN_MV) */
} power_snapshot_t;

void build_power_json(const power_snapshot_t *s, bb_json_t root);

/* ============================================================================
 * /api/fan  (ASIC_CHIP only)
 * ========================================================================= */

typedef struct {
    int fan_rpm;       /* < 0 → null */
    int fan_duty_pct;  /* < 0 → null */
} fan_snapshot_t;

void build_fan_json(const fan_snapshot_t *s, bb_json_t root);
#endif /* ASIC_CHIP */

#ifdef __cplusplus
}
#endif
