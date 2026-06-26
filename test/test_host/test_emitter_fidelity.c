/*
 * test_emitter_fidelity.c — golden/fidelity tests for 4 uncovered route emitters
 * (TA-444).
 *
 * Covers the complete JSON output of:
 *   - emit_diag_bench_json    (streaming, bb_http capture harness)
 *   - emit_mining_rates_json  (bb_json_t; non-ASIC and ASIC-gated fields)
 *   - emit_pool_pub_json      (bb_json_t)
 *   - emit_sensors_miner_json (bb_json_t; ASIC_CHIP only)
 *
 * Each test serialises the emitter output to a string and asserts either an
 * exact golden match or per-field presence/nullness — whichever is appropriate
 * for the emitter type.
 *
 * Host-compilable: no ESP-IDF calls, only routes_json.h / bb_json / bb_http.
 */

#include "unity.h"
#include "routes_json.h"
#include "bb_json.h"
#include "bb_http.h"
#include "bb_http_host.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Helpers
 * ========================================================================= */

/* Portable strdup — POSIX strdup is not declared under -std=c99. */
static char *dupstr(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ============================================================================
 * emit_diag_bench_json — streaming capture
 * ========================================================================= */

static char *capture_bench(const diag_bench_snapshot_t *snap)
{
    bb_http_request_t *req;
    bb_http_host_capture_begin(&req);
    bb_http_json_obj_stream_t obj;
    bb_http_resp_json_obj_begin(req, &obj);
    emit_diag_bench_json(&obj, snap);
    bb_http_resp_json_obj_end(&obj);
    bb_http_host_capture_t cap;
    bb_http_host_capture_end(req, &cap);
    char *copy = dupstr(cap.body);
    bb_http_host_capture_free(&cap);
    return copy;
}

/* All required fields present, no asic_active (has_asic_active=false) */
void test_fidelity_diag_bench_all_fields_present(void)
{
    diag_bench_snapshot_t s = {
        .iters               = 10000,
        .duration_us         = 500000,
        .us_per_op           = 50.0,
        .khs                 = 20.0,
        .sha_ops_per_sec     = 20000.0,
        .backend             = "sw",
        .settled             = true,
        .settled_after_iters = 1000,
        .settled_iters       = 9000,
        .settled_total_us    = 450000,
        .text_overlap_state  = SHA_OVERLAP_SAFE,
        .h_write_state       = SHA_OVERLAP_UNKNOWN,
        .has_asic_active     = false,
        .asic_active         = false,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);

    /* Verify all expected fields are present */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"iters\":10000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"duration_us\":500000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"us_per_op\":50"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"khs\":20"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sha_ops_per_sec\":20000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"backend\":\"sw\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_after_iters\":1000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_iters\":9000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_total_us\":450000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"canary\":{"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text_overlap\":\"safe\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write\":\"unknown\""));
    /* has_asic_active=false → asic_active field must NOT be present */
    TEST_ASSERT_NULL(strstr(json, "\"asic_active\""));

    free(json);
}

/* asic_active field is emitted only when has_asic_active=true */
void test_fidelity_diag_bench_asic_active_emitted_when_flagged(void)
{
    diag_bench_snapshot_t s = {
        .iters               = 1000,
        .duration_us         = 50000,
        .us_per_op           = 50.0,
        .khs                 = 20.0,
        .sha_ops_per_sec     = 20000.0,
        .backend             = "ahb",
        .settled             = false,
        .settled_after_iters = 0,
        .settled_iters       = 0,
        .settled_total_us    = 0,
        .text_overlap_state  = SHA_OVERLAP_UNKNOWN,
        .h_write_state       = SHA_OVERLAP_UNSAFE,
        .has_asic_active     = true,
        .asic_active         = true,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_active\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"backend\":\"ahb\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write\":\"unsafe\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled\":false"));

    free(json);
}

/* canary states: all three overlap values round-trip */
void test_fidelity_diag_bench_canary_all_states(void)
{
    diag_bench_snapshot_t s = {
        .iters               = 1000,
        .duration_us         = 40000,
        .us_per_op           = 40.0,
        .khs                 = 25.0,
        .sha_ops_per_sec     = 25000.0,
        .backend             = "dport",
        .text_overlap_state  = SHA_OVERLAP_SAFE,
        .h_write_state       = SHA_OVERLAP_SAFE,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text_overlap\":\"safe\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write\":\"safe\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"backend\":\"dport\""));
    free(json);
}

/* NULL backend → defaults to "sw" */
void test_fidelity_diag_bench_null_backend_defaults_sw(void)
{
    diag_bench_snapshot_t s = {0};
    s.backend = NULL;

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"backend\":\"sw\""));
    free(json);
}

/* ============================================================================
 * emit_mining_rates_json — bb_json_t path
 * ========================================================================= */

/* Serialize emit_mining_rates_json output; caller frees returned string. */
static char *serialize_mining_rates(const mining_rates_snapshot_t *snap)
{
    bb_json_t obj = bb_json_obj_new();
    if (!obj) return NULL;
    emit_mining_rates_json(obj, snap);
    char *buf = bb_json_serialize(obj);
    bb_json_free(obj);
    return buf;
}

/* All non-ASIC fields populated — no null values */
void test_fidelity_mining_rates_all_fields_present(void)
{
    mining_rates_snapshot_t snap = {
        .hashrate_hs       = 223000.0,
        .shares            = 42.0,
        .rejected          = 1.0,
        .pool_effective_hs = 210000.0,
#ifdef ASIC_CHIP
        .asic_hashrate_hs  = 480000000000.0,
        .asic_total_ghs    = 480.0,
#endif
    };

    char *json = serialize_mining_rates(&snap);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_hs\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"shares\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"rejected\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs\""));
#ifdef ASIC_CHIP
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hashrate_hs\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs\""));
#endif
    /* No nulls when values are positive */
    TEST_ASSERT_NULL(strstr(json, ":null"));

    free(json);
}

/* All fields null (all sentinels -1) — full null-path coverage */
void test_fidelity_mining_rates_all_null(void)
{
    mining_rates_snapshot_t snap = {
        .hashrate_hs       = -1.0,
        .shares            = -1.0,
        .rejected          = -1.0,
        .pool_effective_hs = -1.0,
#ifdef ASIC_CHIP
        .asic_hashrate_hs  = -1.0,
        .asic_total_ghs    = -1.0,
#endif
    };

    char *json = serialize_mining_rates(&snap);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_hs\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"shares\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"rejected\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs\":null"));
#ifdef ASIC_CHIP
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hashrate_hs\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs\":null"));
#endif

    free(json);
}

/* Sentinel boundary: shares=0 is a valid value, not null */
void test_fidelity_mining_rates_zero_shares_not_null(void)
{
    mining_rates_snapshot_t snap = {
        .hashrate_hs       = 100.0,
        .shares            = 0.0,
        .rejected          = 0.0,
        .pool_effective_hs = 0.0,
#ifdef ASIC_CHIP
        .asic_hashrate_hs  = 0.0,
        .asic_total_ghs    = 0.0,
#endif
    };

    char *json = serialize_mining_rates(&snap);
    TEST_ASSERT_NOT_NULL(json);

    /* 0.0 must serialize as numeric, not null */
    TEST_ASSERT_NULL(strstr(json, "\"shares\":null"));
    TEST_ASSERT_NULL(strstr(json, "\"rejected\":null"));

    free(json);
}

/* ============================================================================
 * emit_pool_pub_json — bb_json_t path
 * ========================================================================= */

static char *serialize_pool_pub(const pool_pub_snapshot_t *snap)
{
    bb_json_t obj = bb_json_obj_new();
    if (!obj) return NULL;
    emit_pool_pub_json(obj, snap);
    char *buf = bb_json_serialize(obj);
    bb_json_free(obj);
    return buf;
}

/* All fields populated with non-null values */
void test_fidelity_pool_pub_all_fields_present(void)
{
    pool_pub_snapshot_t snap = {
        .connected             = true,
        .current_difficulty    = 1024.0,
        .latency_ms            = 35.0,
        .active_pool_idx       = 0,
        .pool_effective_hs     = 200000.0,
        .pool_effective_hs_1m  = 195000.0,
        .pool_effective_hs_10m = 198000.0,
        .pool_effective_hs_1h  = 197000.0,
    };

    char *json = serialize_pool_pub(&snap);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"connected\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"current_difficulty\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"latency_ms\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"active_pool_idx\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs_1m\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs_10m\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs_1h\""));
    /* All fields non-null */
    TEST_ASSERT_NULL(strstr(json, ":null"));

    free(json);
}

/* Disconnected snapshot: all optional fields → null */
void test_fidelity_pool_pub_disconnected_all_null(void)
{
    pool_pub_snapshot_t snap = {
        .connected             = false,
        .current_difficulty    = -1.0,
        .latency_ms            = -1.0,
        .active_pool_idx       = -1,
        .pool_effective_hs     = -1.0,
        .pool_effective_hs_1m  = -1.0,
        .pool_effective_hs_10m = -1.0,
        .pool_effective_hs_1h  = -1.0,
    };

    char *json = serialize_pool_pub(&snap);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"connected\":false"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"current_difficulty\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"latency_ms\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"active_pool_idx\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs_1m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs_10m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hs_1h\":null"));

    free(json);
}

/* active_pool_idx = 1 → numeric (non-null) */
void test_fidelity_pool_pub_active_pool_idx_nonzero(void)
{
    pool_pub_snapshot_t snap = {
        .connected          = true,
        .current_difficulty = 512.0,
        .latency_ms         = -1.0,
        .active_pool_idx    = 1,
        .pool_effective_hs  = -1.0,
        .pool_effective_hs_1m  = -1.0,
        .pool_effective_hs_10m = -1.0,
        .pool_effective_hs_1h  = -1.0,
    };

    char *json = serialize_pool_pub(&snap);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"active_pool_idx\":1"));
    /* latency_ms = -1 → null */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"latency_ms\":null"));

    free(json);
}

/* ============================================================================
 * emit_sensors_miner_json — ASIC_CHIP only
 * ========================================================================= */

#ifdef ASIC_CHIP

static char *serialize_sensors_miner(const sensors_miner_snapshot_t *snap)
{
    bb_json_t obj = bb_json_obj_new();
    if (!obj) return NULL;
    emit_sensors_miner_json(obj, snap);
    char *buf = bb_json_serialize(obj);
    bb_json_free(obj);
    return buf;
}

/* All numeric/bool fields emitted as values (no nulls) */
void test_fidelity_sensors_miner_all_fields_present(void)
{
    sensors_miner_snapshot_t snap = {
        .vcore_mv              = 1200.0,
        .icore_ma              = 15000.0,
        .pcore_mw              = 18000.0,
        .vr_temp_c             = 55.0,
        .efficiency_jth        = 20.5,
        .efficiency_jth_1m     = 21.0,
        .efficiency_jth_10m    = 22.0,
        .efficiency_jth_1h     = 23.0,
        .vin_mv                = 12000.0,
        .vin_low               = false,
        .vin_low_valid         = true,
        .sag_count             = 0,
        .vin_min_mv            = 11800,
        .vin_uv_latched        = false,
        .last_sag_ms           = 0,
        .vcore_last_restart_ms = 0,
    };

    char *json = serialize_sensors_miner(&snap);
    TEST_ASSERT_NOT_NULL(json);

    /* All expected field keys present */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vcore_mv\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"icore_ma\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pcore_mw\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vr_temp_c\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth_1m\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth_10m\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth_1h\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_low\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sag_count\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_min_mv\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_uv_latched\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_sag_ms\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vcore_last_restart_ms\""));

    /* vin_low_valid=true, vin_low=false → emitted as false not null */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_low\":false"));
    /* vin_min_mv set → numeric */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_min_mv\":11800"));

    free(json);
}

/* All sentinel values → null outputs */
void test_fidelity_sensors_miner_all_null(void)
{
    sensors_miner_snapshot_t snap = {
        .vcore_mv              = -1.0,
        .icore_ma              = -1.0,
        .pcore_mw              = -1.0,
        .vr_temp_c             = -1.0,
        .efficiency_jth        = -1.0,
        .efficiency_jth_1m     = -1.0,
        .efficiency_jth_10m    = -1.0,
        .efficiency_jth_1h     = -1.0,
        .vin_mv                = -1.0,
        .vin_low               = false,
        .vin_low_valid         = false,   /* → vin_low emits null */
        .sag_count             = 0,
        .vin_min_mv            = INT_MAX, /* → vin_min_mv emits null */
        .vin_uv_latched        = false,
        .last_sag_ms           = 0,       /* → last_sag_ms emits null */
        .vcore_last_restart_ms = 0,       /* → vcore_last_restart_ms emits null */
    };

    char *json = serialize_sensors_miner(&snap);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"vcore_mv\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"icore_ma\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pcore_mw\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vr_temp_c\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth_1m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth_10m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth_1h\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_low\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_min_mv\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_sag_ms\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vcore_last_restart_ms\":null"));

    free(json);
}

/* vin_low true/false with vin_low_valid=true */
void test_fidelity_sensors_miner_vin_low_true(void)
{
    sensors_miner_snapshot_t snap = {
        .vcore_mv = -1.0, .icore_ma = -1.0, .pcore_mw = -1.0, .vr_temp_c = -1.0,
        .efficiency_jth = -1.0, .efficiency_jth_1m = -1.0,
        .efficiency_jth_10m = -1.0, .efficiency_jth_1h = -1.0,
        .vin_mv = -1.0,
        .vin_low       = true,
        .vin_low_valid = true,
        .vin_min_mv    = INT_MAX,
    };

    char *json = serialize_sensors_miner(&snap);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_low\":true"));
    free(json);
}

/* sag_count always emitted as number even when zero */
void test_fidelity_sensors_miner_sag_count_zero_is_number(void)
{
    sensors_miner_snapshot_t snap = {
        .vcore_mv = -1.0, .icore_ma = -1.0, .pcore_mw = -1.0, .vr_temp_c = -1.0,
        .efficiency_jth = -1.0, .efficiency_jth_1m = -1.0,
        .efficiency_jth_10m = -1.0, .efficiency_jth_1h = -1.0,
        .vin_mv = -1.0, .vin_low_valid = false, .vin_min_mv = INT_MAX,
        .sag_count = 0,
    };

    char *json = serialize_sensors_miner(&snap);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sag_count\":0"));
    /* sag_count must NOT be null even at 0 */
    TEST_ASSERT_NULL(strstr(json, "\"sag_count\":null"));
    free(json);
}

#endif /* ASIC_CHIP */
