/*
 * test_sensors_miner_sag.c -- host tests for VIN-sag fields in emit_sensors_miner_json.
 *
 * Validates the 5 new sag/restart fields added to sensors_miner_snapshot_t:
 *   sag_count, vin_min_mv, vin_uv_latched, last_sag_ms, vcore_last_restart_ms
 *
 * Pure host-testable: exercises emit_sensors_miner_json only; no ESP-IDF calls.
 */
#ifdef ASIC_CHIP

#include "unity.h"
#include "routes_json.h"
#include "bb_json.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Helper: build snapshot with all double fields at -1 (null), then call
 * emit_sensors_miner_json, serialize, free obj, parse + return result.
 * Caller must bb_json_free() the returned object.
 * ---------------------------------------------------------------------- */
static sensors_miner_snapshot_t s_base_snap(void)
{
    sensors_miner_snapshot_t s = {
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
        .vin_low_valid         = false,
        .sag_count             = 0,
        .vin_min_mv            = INT_MAX,
        .vin_uv_latched        = false,
        .last_sag_ms           = 0,
        .vcore_last_restart_ms = 0,
    };
    return s;
}

static bb_json_t emit_snap_and_parse(const sensors_miner_snapshot_t *snap)
{
    bb_json_t obj = bb_json_obj_new();
    TEST_ASSERT_NOT_NULL(obj);
    emit_sensors_miner_json(obj, snap);
    char *buf = bb_json_serialize(obj);
    bb_json_free(obj);
    TEST_ASSERT_NOT_NULL(buf);
    bb_json_t parsed = bb_json_parse(buf, 0);
    free(buf);
    return parsed;
}

/* -------------------------------------------------------------------------
 * sag_count: always emitted as a number (zero is a valid value)
 * ---------------------------------------------------------------------- */

void test_sensors_miner_sag_count_zero(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.sag_count = 0;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "sag_count");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_FALSE(bb_json_item_is_null(item));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}

void test_sensors_miner_sag_count_nonzero(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.sag_count = 7;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "sag_count");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(7.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}

/* -------------------------------------------------------------------------
 * vin_min_mv: INT_MAX → null; valid value → number
 * ---------------------------------------------------------------------- */

void test_sensors_miner_vin_min_mv_null_when_int_max(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.vin_min_mv = INT_MAX;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "vin_min_mv");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    bb_json_free(parsed);
}

void test_sensors_miner_vin_min_mv_emits_value(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.vin_min_mv = 11800;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "vin_min_mv");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_FALSE(bb_json_item_is_null(item));
    TEST_ASSERT_EQUAL_DOUBLE(11800.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}

/* -------------------------------------------------------------------------
 * vin_uv_latched: bool field, always emitted
 * ---------------------------------------------------------------------- */

void test_sensors_miner_vin_uv_latched_false(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.vin_uv_latched = false;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "vin_uv_latched");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_FALSE(bb_json_item_is_true(item));

    bb_json_free(parsed);
}

void test_sensors_miner_vin_uv_latched_true(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.vin_uv_latched = true;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "vin_uv_latched");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_true(item));

    bb_json_free(parsed);
}

/* -------------------------------------------------------------------------
 * last_sag_ms: 0 → null; nonzero → number
 * ---------------------------------------------------------------------- */

void test_sensors_miner_last_sag_ms_null_when_zero(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.last_sag_ms = 0;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "last_sag_ms");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    bb_json_free(parsed);
}

void test_sensors_miner_last_sag_ms_emits_value(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.last_sag_ms = 123456789ULL;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "last_sag_ms");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_FALSE(bb_json_item_is_null(item));
    TEST_ASSERT_EQUAL_DOUBLE(123456789.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}

/* -------------------------------------------------------------------------
 * vcore_last_restart_ms: 0 → null; nonzero → number
 * ---------------------------------------------------------------------- */

void test_sensors_miner_vcore_last_restart_ms_null_when_zero(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.vcore_last_restart_ms = 0;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "vcore_last_restart_ms");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    bb_json_free(parsed);
}

void test_sensors_miner_vcore_last_restart_ms_emits_value(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.vcore_last_restart_ms = 987654321ULL;
    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "vcore_last_restart_ms");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_FALSE(bb_json_item_is_null(item));
    TEST_ASSERT_EQUAL_DOUBLE(987654321.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}

/* -------------------------------------------------------------------------
 * All sag fields populated together (integration check)
 * ---------------------------------------------------------------------- */

void test_sensors_miner_sag_all_fields_populated(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.sag_count             = 3;
    snap.vin_min_mv            = 11500;
    snap.vin_uv_latched        = true;
    snap.last_sag_ms           = 500000ULL;
    snap.vcore_last_restart_ms = 600000ULL;

    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item;

    item = bb_json_obj_get_item(parsed, "sag_count");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(3.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "vin_min_mv");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(11500.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "vin_uv_latched");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_true(item));

    item = bb_json_obj_get_item(parsed, "last_sag_ms");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(500000.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "vcore_last_restart_ms");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(600000.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}

/* -------------------------------------------------------------------------
 * All non-sag fields populated with values -> exercises the value (number/bool)
 * branch of every field (s_base_snap leaves them at sentinel -> null branch).
 * ---------------------------------------------------------------------- */

void test_sensors_miner_all_value_fields(void)
{
    sensors_miner_snapshot_t snap = s_base_snap();
    snap.vcore_mv           = 1200.0;
    snap.icore_ma           = 15000.0;
    snap.pcore_mw           = 18000.0;
    snap.vr_temp_c          = 55.0;
    snap.efficiency_jth     = 20.0;
    snap.efficiency_jth_1m  = 21.0;
    snap.efficiency_jth_10m = 22.0;
    snap.efficiency_jth_1h  = 23.0;
    snap.vin_low_valid      = true;
    snap.vin_low            = true;

    bb_json_t parsed = emit_snap_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    static const char *const num_fields[] = {
        "vcore_mv", "icore_ma", "pcore_mw", "vr_temp_c",
        "efficiency_jth", "efficiency_jth_1m", "efficiency_jth_10m", "efficiency_jth_1h",
    };
    for (size_t i = 0; i < sizeof(num_fields) / sizeof(num_fields[0]); i++) {
        bb_json_t item = bb_json_obj_get_item(parsed, num_fields[i]);
        TEST_ASSERT_NOT_NULL(item);
        TEST_ASSERT_FALSE(bb_json_item_is_null(item));
    }

    bb_json_t vl = bb_json_obj_get_item(parsed, "vin_low");
    TEST_ASSERT_NOT_NULL(vl);
    TEST_ASSERT_TRUE(bb_json_item_is_true(vl));

    bb_json_free(parsed);
}

#endif /* ASIC_CHIP */
