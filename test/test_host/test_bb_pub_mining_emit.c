/*
 * test_bb_pub_mining_emit.c -- host tests for B1-352 emit builders.
 *
 * Tests the three new emit builders (mining_rates, pool_pub, sensors_miner)
 * independently of ESP-IDF. Validates SSOT: REST and bb_pub source call the
 * same builder and produce identical fields.
 */
#include "unity.h"
#include "routes_json.h"
#include "bb_json.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/* Allocate obj, call emit, serialize, free obj, parse result. Caller frees. */
static bb_json_t emit_mining_rates_and_parse(const mining_rates_snapshot_t *snap)
{
    bb_json_t obj = bb_json_obj_new();
    TEST_ASSERT_NOT_NULL(obj);
    emit_mining_rates_json(obj, snap);
    char *buf = bb_json_serialize(obj);
    bb_json_free(obj);
    TEST_ASSERT_NOT_NULL(buf);
    bb_json_t parsed = bb_json_parse(buf, 0);
    free(buf);
    return parsed;
}

static bb_json_t emit_pool_pub_and_parse(const pool_pub_snapshot_t *snap)
{
    bb_json_t obj = bb_json_obj_new();
    TEST_ASSERT_NOT_NULL(obj);
    emit_pool_pub_json(obj, snap);
    char *buf = bb_json_serialize(obj);
    bb_json_free(obj);
    TEST_ASSERT_NOT_NULL(buf);
    bb_json_t parsed = bb_json_parse(buf, 0);
    free(buf);
    return parsed;
}

/* -------------------------------------------------------------------------
 * mining_rates tests
 * ---------------------------------------------------------------------- */

void test_emit_mining_rates_all_fields_present(void)
{
    mining_rates_snapshot_t snap = {
        .hashrate_hs       = 223000.0,
        .shares            = 42.0,
        .rejected          = 1.0,
        .pool_effective_hs = 210000.0,
    };
    bb_json_t parsed = emit_mining_rates_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item;

    item = bb_json_obj_get_item(parsed, "hashrate_hs");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_FALSE(bb_json_item_is_null(item));
    TEST_ASSERT_EQUAL_DOUBLE(223000.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "shares");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(42.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "rejected");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "pool_effective_hs");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(210000.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}

void test_emit_mining_rates_hashrate_null_when_unavailable(void)
{
    mining_rates_snapshot_t snap = {
        .hashrate_hs       = -1.0,
        .shares            = 0.0,
        .rejected          = 0.0,
        .pool_effective_hs = -1.0,
    };
    bb_json_t parsed = emit_mining_rates_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "hashrate_hs");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    item = bb_json_obj_get_item(parsed, "pool_effective_hs");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    bb_json_free(parsed);
}

void test_emit_mining_rates_shares_zero_emits_as_number(void)
{
    mining_rates_snapshot_t snap = {
        .hashrate_hs       = 100.0,
        .shares            = 0.0,
        .rejected          = 0.0,
        .pool_effective_hs = -1.0,
    };
    bb_json_t parsed = emit_mining_rates_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item = bb_json_obj_get_item(parsed, "shares");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_FALSE(bb_json_item_is_null(item));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}

/* shares/rejected (+ ASIC asic_hashrate_hs/asic_total_ghs) < 0 -> null branch */
void test_emit_mining_rates_all_null(void)
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
    bb_json_t parsed = emit_mining_rates_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item;
    item = bb_json_obj_get_item(parsed, "shares");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    item = bb_json_obj_get_item(parsed, "rejected");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

#ifdef ASIC_CHIP
    item = bb_json_obj_get_item(parsed, "asic_hashrate_hs");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    item = bb_json_obj_get_item(parsed, "asic_total_ghs");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));
#endif

    bb_json_free(parsed);
}

/* -------------------------------------------------------------------------
 * pool_pub tests
 * ---------------------------------------------------------------------- */

void test_emit_pool_pub_connected(void)
{
    pool_pub_snapshot_t snap = {
        .connected             = true,
        .current_difficulty    = 512.0,
        .latency_ms            = 45.0,
        .active_pool_idx       = 0,
        .pool_effective_hs     = 200000.0,
        .pool_effective_hs_1m  = -1.0,
        .pool_effective_hs_10m = -1.0,
        .pool_effective_hs_1h  = -1.0,
    };
    bb_json_t parsed = emit_pool_pub_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item;

    item = bb_json_obj_get_item(parsed, "connected");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_true(item));

    item = bb_json_obj_get_item(parsed, "current_difficulty");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "latency_ms");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(45.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "active_pool_idx");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "pool_effective_hs");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(200000.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "pool_effective_hs_1m");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    bb_json_free(parsed);
}

void test_emit_pool_pub_disconnected_null_fields(void)
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
    bb_json_t parsed = emit_pool_pub_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item;

    item = bb_json_obj_get_item(parsed, "connected");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_FALSE(bb_json_item_is_true(item));

    item = bb_json_obj_get_item(parsed, "current_difficulty");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    item = bb_json_obj_get_item(parsed, "latency_ms");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    item = bb_json_obj_get_item(parsed, "active_pool_idx");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(bb_json_item_is_null(item));

    bb_json_free(parsed);
}

void test_emit_pool_pub_rolling_windows(void)
{
    pool_pub_snapshot_t snap = {
        .connected             = true,
        .current_difficulty    = 1024.0,
        .latency_ms            = -1.0,
        .active_pool_idx       = 1,
        .pool_effective_hs     = 150000.0,
        .pool_effective_hs_1m  = 140000.0,
        .pool_effective_hs_10m = 145000.0,
        .pool_effective_hs_1h  = 148000.0,
    };
    bb_json_t parsed = emit_pool_pub_and_parse(&snap);
    TEST_ASSERT_NOT_NULL(parsed);

    bb_json_t item;
    item = bb_json_obj_get_item(parsed, "pool_effective_hs_1m");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(140000.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "pool_effective_hs_10m");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(145000.0, bb_json_item_get_double(item));

    item = bb_json_obj_get_item(parsed, "pool_effective_hs_1h");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_DOUBLE(148000.0, bb_json_item_get_double(item));

    bb_json_free(parsed);
}
