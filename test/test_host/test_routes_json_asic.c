/*
 * test_routes_json_asic.c — golden tests for ASIC-gated JSON builders (TA-292).
 *
 * build_power_json, build_fan_json, and the ASIC-gated branches of
 * build_stats_json are only compiled when ASIC_CHIP is defined.  The native
 * env now defines ASIC_CHIP (see [env:native] build_flags in platformio.ini),
 * so all three builders are reachable on the host.
 */
#include "unity.h"
#include "routes_json.h"
#include "bb_json.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ============================================================================
 * Helpers
 * ========================================================================= */

static char *serialize_and_free(bb_json_t root)
{
    char *s = bb_json_serialize(root);
    bb_json_free(root);
    return s;
}

/* ============================================================================
 * /api/power — build_power_json
 * ========================================================================= */

void test_power_all_sensors_populated(void)
{
    /* Typical bitaxe values: all fields present, efficiency computable */
    power_snapshot_t s = {0};
    s.vcore_mv      = 1200;
    s.icore_ma      = 15000;
    s.pcore_mw      = 18000;
    s.vin_mv        = 5000;
    s.asic_hashrate = 485e9;     /* 485 GH/s */
    s.board_temp_c  = 55.0f;
    s.vr_temp_c     = 60.0f;
    s.nominal_vin_mv = 5000;
    /* vin_low: vin_mv=5000, threshold=(5000+500)*87/100=4785 → 5000>=4785 → false
     * efficiency_jth: (18000/1000.0) / (485e9/1e12) = 18.0 / 0.485 ≈ 37.11340... */

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    /* Verify key fields are present and vcore/icore/pcore/vin are numbers, not null */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vcore_mv\":1200"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"icore_ma\":15000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pcore_mw\":18000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_mv\":5000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_low\":false"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"board_temp_c\":55"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vr_temp_c\":60"));
    /* efficiency_jth should be a number (not null) */
    TEST_ASSERT_NULL(strstr(json, "\"efficiency_jth\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth\":"));

    bb_json_free_str(json);
}

void test_power_all_sensors_null(void)
{
    /* All sentinel values: everything emits null */
    power_snapshot_t s = {0};
    s.vcore_mv      = -1;
    s.icore_ma      = -1;
    s.pcore_mw      = -1;
    s.vin_mv        = -1;
    s.asic_hashrate = 0.0;
    s.board_temp_c  = -1.0f;
    s.vr_temp_c     = -1.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"vcore_mv\":null,\"icore_ma\":null,\"pcore_mw\":null,"
        "\"efficiency_jth\":null,\"vin_mv\":null,\"vin_low\":null,"
        "\"board_temp_c\":null,\"vr_temp_c\":null}",
        json);
    bb_json_free_str(json);
}

void test_power_efficiency_null_when_hashrate_zero(void)
{
    /* pcore_mw > 0 but asic_hashrate = 0 → efficiency_jth = null */
    power_snapshot_t s = {0};
    s.vcore_mv      = 1200;
    s.icore_ma      = 15000;
    s.pcore_mw      = 18000;
    s.vin_mv        = 5000;
    s.asic_hashrate = 0.0;
    s.board_temp_c  = 55.0f;
    s.vr_temp_c     = 60.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth\":null"));
    bb_json_free_str(json);
}

void test_power_efficiency_null_when_pcore_zero(void)
{
    /* pcore_mw = 0, asic_hashrate > 0 → efficiency_jth = null */
    power_snapshot_t s = {0};
    s.vcore_mv      = 1200;
    s.icore_ma      = 15000;
    s.pcore_mw      = 0;
    s.vin_mv        = 5000;
    s.asic_hashrate = 485e9;
    s.board_temp_c  = 55.0f;
    s.vr_temp_c     = 60.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"efficiency_jth\":null"));
    bb_json_free_str(json);
}

void test_power_vin_low_true(void)
{
    /* vin_mv=4000, nominal=5000: threshold=(5000+500)*87/100=4785; 4000<4785 → vin_low=true */
    power_snapshot_t s = {0};
    s.vcore_mv      = 1200;
    s.icore_ma      = -1;
    s.pcore_mw      = -1;
    s.vin_mv        = 4000;
    s.asic_hashrate = 0.0;
    s.board_temp_c  = -1.0f;
    s.vr_temp_c     = -1.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_low\":true"));
    bb_json_free_str(json);
}

void test_power_vin_low_false_above_threshold(void)
{
    /* vin_mv=4800, nominal=5000: threshold=4785; 4800>=4785 → vin_low=false */
    power_snapshot_t s = {0};
    s.vcore_mv      = 1200;
    s.icore_ma      = -1;
    s.pcore_mw      = -1;
    s.vin_mv        = 4800;
    s.asic_hashrate = 0.0;
    s.board_temp_c  = -1.0f;
    s.vr_temp_c     = -1.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_low\":false"));
    bb_json_free_str(json);
}

void test_power_vin_low_false_at_threshold(void)
{
    /* vin_mv=4785 equals threshold → not strictly less → vin_low=false */
    power_snapshot_t s = {0};
    s.vcore_mv      = -1;
    s.icore_ma      = -1;
    s.pcore_mw      = -1;
    s.vin_mv        = 4785;
    s.asic_hashrate = 0.0;
    s.board_temp_c  = -1.0f;
    s.vr_temp_c     = -1.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"vin_low\":false"));
    bb_json_free_str(json);
}

void test_power_vcore_null_others_populated(void)
{
    /* vcore_mv < 0 → null, but icore_ma and pcore_mw are valid */
    power_snapshot_t s = {0};
    s.vcore_mv      = -1;
    s.icore_ma      = 15000;
    s.pcore_mw      = 18000;
    s.vin_mv        = 5000;
    s.asic_hashrate = 485e9;
    s.board_temp_c  = 55.0f;
    s.vr_temp_c     = 60.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"vcore_mv\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"icore_ma\":15000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pcore_mw\":18000"));
    bb_json_free_str(json);
}

void test_power_icore_null(void)
{
    /* icore_ma < 0 → null */
    power_snapshot_t s = {0};
    s.vcore_mv      = 1200;
    s.icore_ma      = -1;
    s.pcore_mw      = 18000;
    s.vin_mv        = 5000;
    s.asic_hashrate = 485e9;
    s.board_temp_c  = 55.0f;
    s.vr_temp_c     = 60.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"icore_ma\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vcore_mv\":1200"));
    bb_json_free_str(json);
}

void test_power_board_temp_null(void)
{
    /* board_temp_c < 0 → null */
    power_snapshot_t s = {0};
    s.vcore_mv      = 1200;
    s.icore_ma      = 15000;
    s.pcore_mw      = 18000;
    s.vin_mv        = 5000;
    s.asic_hashrate = 485e9;
    s.board_temp_c  = -1.0f;
    s.vr_temp_c     = 60.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"board_temp_c\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"vr_temp_c\":60"));
    bb_json_free_str(json);
}

void test_power_vr_temp_null(void)
{
    /* vr_temp_c < 0 → null */
    power_snapshot_t s = {0};
    s.vcore_mv      = 1200;
    s.icore_ma      = 15000;
    s.pcore_mw      = 18000;
    s.vin_mv        = 5000;
    s.asic_hashrate = 485e9;
    s.board_temp_c  = 55.0f;
    s.vr_temp_c     = -1.0f;
    s.nominal_vin_mv = 5000;

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"vr_temp_c\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"board_temp_c\":55"));
    bb_json_free_str(json);
}

/* ============================================================================
 * /api/fan — build_fan_json
 * ========================================================================= */

void test_fan_both_populated(void)
{
    fan_snapshot_t s = {0};
    s.fan_rpm      = 2400;
    s.fan_duty_pct = 75;

    bb_json_t root = bb_json_obj_new();
    build_fan_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING("{\"rpm\":2400,\"duty_pct\":75}", json);
    bb_json_free_str(json);
}

void test_fan_rpm_null(void)
{
    fan_snapshot_t s = {0};
    s.fan_rpm      = -1;
    s.fan_duty_pct = 75;

    bb_json_t root = bb_json_obj_new();
    build_fan_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING("{\"rpm\":null,\"duty_pct\":75}", json);
    bb_json_free_str(json);
}

void test_fan_duty_null(void)
{
    fan_snapshot_t s = {0};
    s.fan_rpm      = 2400;
    s.fan_duty_pct = -1;

    bb_json_t root = bb_json_obj_new();
    build_fan_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING("{\"rpm\":2400,\"duty_pct\":null}", json);
    bb_json_free_str(json);
}

void test_fan_both_null(void)
{
    fan_snapshot_t s = {0};
    s.fan_rpm      = -1;
    s.fan_duty_pct = -1;

    bb_json_t root = bb_json_obj_new();
    build_fan_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING("{\"rpm\":null,\"duty_pct\":null}", json);
    bb_json_free_str(json);
}

/* ============================================================================
 * /api/stats — ASIC-gated branches of build_stats_json
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
    s->lifetime_shares     = 50;
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
    s.asic_temp_c         = 60.0f;
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

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    /* asic_total_valid=true → numeric values for all asic_total/hw_error fields */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs\":485"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hw_error_pct\":0.1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1m\":480"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_10m\":478"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1h\":475"));
    /* nulls must NOT appear for these fields */
    TEST_ASSERT_NULL(strstr(json, "\"asic_total_ghs\":null"));

    bb_json_free_str(json);
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

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_hw_error_pct\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_10m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_total_ghs_1h\":null"));

    bb_json_free_str(json);
}

void test_stats_expected_ghs_populated(void)
{
    /* asic_freq_cfg > 0 → expected_ghs = freq_cfg * small_cores * count / 1000 */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 500.0f;  /* MHz */
    s.asic_freq_eff    = 500.0f;
    s.asic_small_cores = 2000;
    s.asic_count       = 1;
    s.asic_total_valid = false;
    s.n_chips          = 0;
    /* expected_ghs = 500 * 2000 * 1 / 1000 = 1000 */

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"expected_ghs\":1000"));
    bb_json_free_str(json);
}

void test_stats_expected_ghs_null_when_freq_zero(void)
{
    /* asic_freq_cfg = 0 → expected_ghs = null */
    stats_snapshot_t s;
    make_stats_base(&s);
    s.asic_freq_cfg    = 0.0f;
    s.asic_freq_eff    = -1.0f;
    s.asic_small_cores = 2040;
    s.asic_count       = 1;
    s.asic_total_valid = false;
    s.n_chips          = 0;

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"expected_ghs\":null"));
    bb_json_free_str(json);
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

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_freq_configured_mhz\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_freq_effective_mhz\":null"));
    bb_json_free_str(json);
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

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    /* chip 0: last_drop_us=0 → null */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":null"));
    /* chip 1: last_drop_us=51000000, now_us=61000000 → ago=10s */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":10"));
    /* both chips present in array */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"idx\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"idx\":1"));

    bb_json_free_str(json);
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

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_chips\":[]"));
    bb_json_free_str(json);
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

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":null"));
    bb_json_free_str(json);
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

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"last_drop_ago_s\":30"));
    bb_json_free_str(json);
}
