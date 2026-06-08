/*
 * test_routes_json_asic.c — golden tests for ASIC-gated JSON emitters (TA-292).
 *
 * /api/power and /api/fan are now BB-owned routes with TM extenders (P4b).
 * emit_power_json and emit_fan_json have been removed; their field coverage
 * (efficiency_jth, vin_low math) is already in test_mining.c.
 *
 * This file covers the ASIC-gated branches of emit_stats_json and verifies
 * that asic_temp_c is no longer emitted (moved to /api/thermal).
 *
 * All tests use the bb_http capture harness: emit_* functions write into a
 * streaming JSON object, and the captured body is verified with strstr.
 * This validates the same code path that runs in the production handler.
 */
#ifdef ASIC_CHIP
#include "unity.h"
#include "routes_json.h"
#include "bb_http.h"
#include "bb_http_host.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ============================================================================
 * Helpers — streaming capture
 * ========================================================================= */

/* Portable strdup — POSIX strdup is not declared under -std=c99 (glibc hides it
 * without _POSIX_C_SOURCE), and an implicit decl truncates the 64-bit pointer
 * return on LP64. */
static char *dupstr(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* /api/power and /api/fan are now BB-owned routes (P4b).
 * emit_power_json and emit_fan_json have been removed.
 * Efficiency and vin_low math coverage lives in test_mining.c. */

/* Capture emit_stats_json output into a heap string.
 * Caller must free() the returned pointer. */
static char *capture_stats(const stats_snapshot_t *snap)
{
    bb_http_request_t *req;
    bb_http_host_capture_begin(&req);
    bb_http_json_obj_stream_t obj;
    bb_http_resp_json_obj_begin(req, &obj);
    emit_stats_json(&obj, snap);
    bb_http_resp_json_obj_end(&obj);
    bb_http_host_capture_t cap;
    bb_http_host_capture_end(req, &cap);
    char *copy = dupstr(cap.body);
    bb_http_host_capture_free(&cap);
    return copy;
}

/* ============================================================================
 * /api/stats — ASIC-gated branches of emit_stats_json
 * asic_temp_c removed (P4b): now sourced from /api/thermal (ASIC die).
 * ========================================================================= */

/*
 * Base helper: zero-initialise a stats snapshot, fill non-ASIC fields with
 * realistic values, and leave ASIC fields for the caller to populate.
 */
static void make_stats_base(stats_snapshot_t *s)
{
    memset(s, 0, sizeof(*s));
    s->hw_rate             = 485e9;
    s->hw_ema              = 470e9;
    s->temp_c              = 45.0f;
    s->hw_shares           = 10;
    s->session_shares      = 8;
    s->session_rejected    = 0;
    s->session_rejected_other_last_code = -1;
    s->last_share_us       = 1000000LL;
    s->session_start_us    = 1000000LL;
    s->best_diff           = 262144.0;
    s->now_us              = 61000000LL;  /* 60 s uptime */
}

void test_stats_asic_total_valid_true(void)
{
    /* asic_total_valid=true → numeric fields emitted instead of nulls */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_rate           = 485e9;
    s.asic_ema            = 470e9;
    s.asic_shares         = 8;
    /* asic_temp_c removed from stats_snapshot_t — now in /api/thermal */
    s.asic_freq_cfg       = 525.0f;
    s.asic_freq_eff       = 520.0f;
    s.asic_total_ghs      = 485.0f;
    s.asic_hw_error_pct   = 0.1f;
    s.asic_total_ghs_1m   = 480.0f;
    s.asic_total_ghs_10m  = 478.0f;
    s.asic_total_ghs_1h   = 475.0f;
    s.asic_hw_error_pct_1m  = 0.1f;
    s.asic_hw_error_pct_10m = 0.2f;
    s.asic_hw_error_pct_1h  = 0.15f;
    s.asic_total_valid    = true;
    s.asic_small_cores    = 2040;
    s.asic_count          = 1;
    s.n_chips             = 0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);

    /* asic_total_valid=true → numeric values for all asic_total/hw_error fields */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs\":485"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hw_error_pct\":0.1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1m\":480"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_10m\":478"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1h\":475"));
    /* nulls must NOT appear for these fields */
    TEST_ASSERT_NULL(strstr(json, "\"asic_total_ghs\":null"));

    free(json);
}

/* P4b: asic_temp_c must NOT appear in /api/stats (moved to /api/thermal) */
void test_stats_no_asic_temp_c(void)
{
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 525.0f;
    s.asic_freq_eff    = 520.0f;
    s.asic_total_valid = false;
    s.asic_small_cores = 2040;
    s.asic_count       = 1;
    s.n_chips          = 0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NULL(strstr(json, "asic_temp_c"));
    free(json);
}

void test_stats_asic_total_valid_false(void)
{
    /* asic_total_valid=false → all asic rate/error fields emit null */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 525.0f;
    s.asic_freq_eff    = -1.0f;  /* not yet set */
    s.asic_total_valid = false;
    s.asic_small_cores = 2040;
    s.asic_count       = 1;
    s.n_chips          = 0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hw_error_pct\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_10m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1h\":null"));

    free(json);
}

void test_stats_expected_ghs_populated(void)
{
    /* expected_ghs field directly populated in snapshot */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 500.0f;  /* MHz */
    s.asic_freq_eff    = 500.0f;
    s.asic_small_cores = 2000;
    s.asic_count       = 1;
    s.asic_total_valid = false;
    s.n_chips          = 0;
    s.expected_ghs     = 1000.0;  /* precomputed by routes.c:mining_get_expected_ghs() */

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"expected_ghs\":1000"));
    free(json);
}

void test_stats_expected_ghs_null_when_unavailable(void)
{
    /* expected_ghs < 0 → emit null */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 0.0f;
    s.asic_freq_eff    = -1.0f;
    s.asic_small_cores = 2040;
    s.asic_count       = 1;
    s.asic_total_valid = false;
    s.n_chips          = 0;
    s.expected_ghs     = -1.0;  /* unavailable sentinel from routes.c */

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"expected_ghs\":null"));
    free(json);
}

void test_stats_freq_cfg_negative_emits_null(void)
{
    /* asic_freq_cfg = -1 → asic_freq_configured_mhz = null */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = -1.0f;
    s.asic_freq_eff    = -1.0f;
    s.asic_total_valid = false;
    s.n_chips          = 0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_freq_configured_mhz\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_freq_effective_mhz\":null"));
    free(json);
}

void test_stats_chip_array_two_chips(void)
{
    /* n_chips=2 → asic_chips array has 2 objects */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 525.0f;
    s.asic_freq_eff    = 520.0f;
    s.asic_total_valid = true;
    s.asic_total_ghs   = 970.0f;
    s.asic_hw_error_pct = 0.05f;
    s.asic_total_ghs_1m = 960.0f;
    s.asic_total_ghs_10m = 955.0f;
    s.asic_total_ghs_1h = 950.0f;
    s.asic_hw_error_pct_1m = 0.05f;
    s.asic_hw_error_pct_10m = 0.06f;
    s.asic_hw_error_pct_1h = 0.055f;
    s.asic_small_cores = 2040;
    s.asic_count       = 2;
    s.n_chips          = 2;

    s.chips[0].total_ghs  = 485.0f;
    s.chips[0].error_ghs  = 0.2f;
    s.chips[0].hw_err_pct = 0.04f;
    s.chips[0].total_raw  = 1000;
    s.chips[0].error_raw  = 5;
    s.chips[0].total_drops = 1;
    s.chips[0].error_drops = 0;
    s.chips[0].last_drop_us = 0;  /* no drop → null */
    s.chips[0].domain_ghs[0] = 121.0f;
    s.chips[0].domain_ghs[1] = 122.0f;
    s.chips[0].domain_ghs[2] = 120.0f;
    s.chips[0].domain_ghs[3] = 122.0f;
    s.chips[0].domain_drops[0] = 0;
    s.chips[0].domain_drops[1] = 0;
    s.chips[0].domain_drops[2] = 0;
    s.chips[0].domain_drops[3] = 0;

    s.chips[1].total_ghs  = 485.0f;
    s.chips[1].error_ghs  = 0.3f;
    s.chips[1].hw_err_pct = 0.06f;
    s.chips[1].total_raw  = 1000;
    s.chips[1].error_raw  = 6;
    s.chips[1].total_drops = 2;
    s.chips[1].error_drops = 1;
    s.chips[1].last_drop_us = 51000000ULL;  /* 10 s before now_us=61000000 */
    s.chips[1].domain_ghs[0] = 120.0f;
    s.chips[1].domain_ghs[1] = 121.0f;
    s.chips[1].domain_ghs[2] = 122.0f;
    s.chips[1].domain_ghs[3] = 122.0f;
    s.chips[1].domain_drops[0] = 0;
    s.chips[1].domain_drops[1] = 0;
    s.chips[1].domain_drops[2] = 1;
    s.chips[1].domain_drops[3] = 0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);

    /* chip 0: last_drop_us=0 → null */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":null"));
    /* chip 1: last_drop_us=51000000, now_us=61000000 → ago=10s */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":10"));
    /* both chips present in array */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"idx\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"idx\":1"));

    free(json);
}

void test_stats_chip_array_empty(void)
{
    /* n_chips=0 → asic_chips = [] */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 525.0f;
    s.asic_freq_eff    = 520.0f;
    s.asic_total_valid = false;
    s.asic_small_cores = 2040;
    s.asic_count       = 1;
    s.n_chips          = 0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_chips\":[]"));
    free(json);
}

void test_stats_last_drop_null_when_zero(void)
{
    /* Chip with last_drop_us=0 → last_drop_ago_s = null */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 525.0f;
    s.asic_freq_eff    = 520.0f;
    s.asic_total_valid = false;
    s.asic_small_cores = 2040;
    s.asic_count       = 1;
    s.n_chips          = 1;
    s.chips[0].total_ghs   = 485.0f;
    s.chips[0].last_drop_us = 0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":null"));
    free(json);
}

void test_stats_last_drop_nonzero_computes_age(void)
{
    /* Chip with last_drop_us=31000000, now_us=61000000 → ago=30s */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 525.0f;
    s.asic_freq_eff    = 520.0f;
    s.asic_total_valid = false;
    s.asic_small_cores = 2040;
    s.asic_count       = 1;
    s.n_chips          = 1;
    s.chips[0].total_ghs    = 485.0f;
    s.chips[0].last_drop_us = 31000000ULL;  /* now_us - 30s */

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":30"));
    free(json);
}

void test_stats_asic_rolling_windows_null_when_invalid(void)
{
    /* asic_total_valid=true but rolling windows set to negative sentinels → null */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg          = 525.0f;
    s.asic_freq_eff          = 520.0f;
    s.asic_total_ghs         = 485.0f;
    s.asic_hw_error_pct      = 0.1f;
    s.asic_total_ghs_1m      = -1.0f;
    s.asic_total_ghs_10m     = -1.0f;
    s.asic_total_ghs_1h      = -1.0f;
    s.asic_hw_error_pct_1m   = -1.0f;
    s.asic_hw_error_pct_10m  = -1.0f;
    s.asic_hw_error_pct_1h   = -1.0f;
    s.asic_total_valid       = true;
    s.asic_small_cores       = 2040;
    s.asic_count             = 1;
    s.n_chips                = 0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_10m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1h\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hw_error_pct_1m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hw_error_pct_10m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hw_error_pct_1h\":null"));
    /* asic_total_ghs and asic_hw_error_pct must NOT be null */
    TEST_ASSERT_NULL(strstr(json, "\"asic_total_ghs\":null"));
    TEST_ASSERT_NULL(strstr(json, "\"asic_hw_error_pct\":null"));
    free(json);
}

void test_stats_now_us_less_than_last_drop_emits_null(void)
{
    /* now_us < last_drop_us (future timestamp) → last_drop_ago_s = null */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 525.0f;
    s.asic_freq_eff    = 520.0f;
    s.asic_total_valid = false;
    s.asic_small_cores = 2040;
    s.asic_count       = 1;
    s.n_chips          = 1;
    s.chips[0].total_ghs    = 485.0f;
    s.chips[0].last_drop_us = 99999999ULL;  /* future: now_us=61000000 < this */

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":null"));
    free(json);
}
#endif /* ASIC_CHIP */
