#include "unity.h"
#include "tm_pool_stats.h"
#include "bb_config.h"
#include "bb_storage.h"
#include "fake_nvs_backend.h"

#include <string.h>

// tm_pool_stats's field tables target backend="nvs" (the real ESP-IDF
// backend) -- host tests register the shared fake in-memory "nvs" vtable
// (see fake_nvs_backend.h) under that same name, exercising the SAME
// production field-table/addr code path the device build uses, with only
// the backend's storage swapped for a host-safe stand-in.

static void reset_all(void)
{
    bb_storage_test_reset();
    fake_nvs_reset();
    bb_storage_register_backend("nvs", &s_fake_nvs_vtable, NULL);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_init());
}

// Probe field mirroring tm_pool_stats.c's own per-slot BLOB field table --
// lets a test read/write slot 0's raw storage bytes directly, bypassing
// tm_pool_stats's active-slot cache, to assert what was (or wasn't) actually
// persisted.
static const bb_config_field_t s_probe_pool0_stat = {
    .id      = "pool0.stat.probe",
    .type    = BB_CONFIG_BLOB,
    .addr    = { .backend = "nvs", .ns_or_dir = "tm_pool", .key = "pool0_stat" },
    .max_len = sizeof(tm_pool_lifetime_stat_t),
};

/* ---------------------------------------------------------------------------
 * record_share
 * ---------------------------------------------------------------------------*/

void test_tm_pool_stats_record_share_updates_shares_best_diff_and_last_seen(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(0, 100.0, 1700000001));

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(0, &out));
    TEST_ASSERT_EQUAL_UINT32(1, out.accepted_shares);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, out.best_diff);
    TEST_ASSERT_EQUAL_INT64(1700000001, out.best_diff_ts);
    TEST_ASSERT_EQUAL_INT64(1700000001, out.last_seen_ts);
}

void test_tm_pool_stats_record_share_best_diff_only_advances_on_improvement(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(0, 100.0, 1700000001));

    // Lower diff: shares/last_seen still update, best_diff/ts do not.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(0, 50.0, 1700000002));
    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(0, &out));
    TEST_ASSERT_EQUAL_UINT32(2, out.accepted_shares);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, out.best_diff);
    TEST_ASSERT_EQUAL_INT64(1700000001, out.best_diff_ts);
    TEST_ASSERT_EQUAL_INT64(1700000002, out.last_seen_ts);

    // Genuine improvement: best_diff/ts advance.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(0, 150.0, 1700000003));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(0, &out));
    TEST_ASSERT_EQUAL_DOUBLE(150.0, out.best_diff);
    TEST_ASSERT_EQUAL_INT64(1700000003, out.best_diff_ts);
}

/* ---------------------------------------------------------------------------
 * record_hashes / record_block
 * ---------------------------------------------------------------------------*/

void test_tm_pool_stats_record_hashes_accumulates(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 1000));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 2500));

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(0, &out));
    TEST_ASSERT_EQUAL_UINT64(3500, out.hashes);
}

void test_tm_pool_stats_record_block_increments_blocks_and_last_seen(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_block(0, 1700000005));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_block(0, 1700000006));

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(0, &out));
    TEST_ASSERT_EQUAL_UINT32(2, out.blocks_found);
    TEST_ASSERT_EQUAL_INT64(1700000006, out.last_seen_ts);
}

/* ---------------------------------------------------------------------------
 * Active-slot cache discipline
 * ---------------------------------------------------------------------------*/

void test_tm_pool_stats_switching_active_slot_flushes_prior_dirty_slot(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 12345));
    // Switching the active slot to 1 must flush slot 0's dirty record first.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(1, 999));

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(0, &out));
    TEST_ASSERT_EQUAL_UINT64(12345, out.hashes);
}

void test_tm_pool_stats_record_hashes_dirties_but_does_not_persist_until_flush(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 500));

    // Not flushed yet: the raw storage key must not exist.
    tm_pool_lifetime_stat_t raw;
    size_t                  len = 0;
    TEST_ASSERT_EQUAL(BB_ERR_NOT_FOUND, bb_config_get_blob(&s_probe_pool0_stat, &raw, sizeof(raw), &len));

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(0));
    TEST_ASSERT_EQUAL(BB_OK, bb_config_get_blob(&s_probe_pool0_stat, &raw, sizeof(raw), &len));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(raw), (uint32_t)len);
    TEST_ASSERT_EQUAL_UINT64(500, raw.hashes);
}

void test_tm_pool_stats_flush_on_inactive_slot_is_a_noop(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 111)); // slot 0 active
    // Slot 1 was never touched -- flushing it must be a harmless no-op.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(1));

    size_t                  len = 0;
    tm_pool_lifetime_stat_t raw;
    static const bb_config_field_t probe1 = {
        .id      = "pool1.stat.probe",
        .type    = BB_CONFIG_BLOB,
        .addr    = { .backend = "nvs", .ns_or_dir = "tm_pool", .key = "pool1_stat" },
        .max_len = sizeof(tm_pool_lifetime_stat_t),
    };
    TEST_ASSERT_EQUAL(BB_ERR_NOT_FOUND, bb_config_get_blob(&probe1, &raw, sizeof(raw), &len));
}

void test_tm_pool_stats_switch_flush_failure_propagates_and_preserves_dirty_slot_for_retry(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 12345)); // slot 0 active, dirty

    // Switching to slot 1 must flush slot 0 first; arm that flush to fail.
    fake_nvs_backend_fail_set_key("pool0_stat");
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_stats_record_hashes(1, 999));

    // Slot 1 was never activated: its accumulated 999 hashes never landed
    // anywhere (not persisted, and slot 0 is still the cached/active slot,
    // so a direct storage read of slot 1 must show nothing).
    size_t                  len = 0;
    tm_pool_lifetime_stat_t raw;
    static const bb_config_field_t probe1 = {
        .id      = "pool1.stat.probe",
        .type    = BB_CONFIG_BLOB,
        .addr    = { .backend = "nvs", .ns_or_dir = "tm_pool", .key = "pool1_stat" },
        .max_len = sizeof(tm_pool_lifetime_stat_t),
    };
    TEST_ASSERT_EQUAL(BB_ERR_NOT_FOUND, bb_config_get_blob(&probe1, &raw, sizeof(raw), &len));

    // Slot 0's dirty data is still intact in the cache (the failed flush
    // never cleared s_dirty) -- a later flush retry recovers it.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(0));
    TEST_ASSERT_EQUAL(BB_OK, bb_config_get_blob(&s_probe_pool0_stat, &raw, sizeof(raw), &len));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(raw), (uint32_t)len);
    TEST_ASSERT_EQUAL_UINT64(12345, raw.hashes);
}

/* ---------------------------------------------------------------------------
 * Round-trip / unset / reset
 * ---------------------------------------------------------------------------*/

void test_tm_pool_stats_round_trips_after_flush(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(2, 42.5, 1700000111));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(2, 777));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(2));

    // Force slot 2 out of the cache, then read it back from storage.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 1));

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(2, &out));
    TEST_ASSERT_EQUAL_UINT32(1, out.accepted_shares);
    TEST_ASSERT_EQUAL_DOUBLE(42.5, out.best_diff);
    TEST_ASSERT_EQUAL_INT64(1700000111, out.best_diff_ts);
    TEST_ASSERT_EQUAL_UINT64(777, out.hashes);
}

void test_tm_pool_stats_load_unset_slot_returns_zeroed_record(void)
{
    reset_all();
    tm_pool_lifetime_stat_t out;
    memset(&out, 0xAA, sizeof(out)); // poison, so BB_OK+zeroed is provable
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(1, &out));
    TEST_ASSERT_EQUAL_UINT32(0, out.accepted_shares);
    TEST_ASSERT_EQUAL_UINT64(0, out.hashes);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, out.best_diff);
    TEST_ASSERT_EQUAL_INT64(0, out.best_diff_ts);
    TEST_ASSERT_EQUAL_UINT32(0, out.blocks_found);
    TEST_ASSERT_EQUAL_INT64(0, out.last_seen_ts);
}

void test_tm_pool_stats_reset_zeroes_ram_and_nvs_for_active_slot(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(0, 9.0, 500));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_reset(0));

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(0, &out)); // reads RAM (still active)
    TEST_ASSERT_EQUAL_UINT32(0, out.accepted_shares);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, out.best_diff);
}

void test_tm_pool_stats_reset_zeroes_nvs_for_inactive_slot(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(1, 9.0, 500));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(1));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 1)); // deactivate slot 1
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_reset(1));

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(1, &out)); // reads storage
    TEST_ASSERT_EQUAL_UINT32(0, out.accepted_shares);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, out.best_diff);
}

/* ---------------------------------------------------------------------------
 * Sanitizer
 * ---------------------------------------------------------------------------*/

static double s_bits_to_double(uint64_t bits)
{
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

void test_tm_pool_stats_sanitize_rejects_nan_best_diff(void)
{
    tm_pool_lifetime_stat_t sl;
    memset(&sl, 0, sizeof(sl));
    sl.best_diff = s_bits_to_double(UINT64_C(0x7FF8000000000000)); // quiet NaN
    tm_pool_stats_sanitize_slot(&sl);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, sl.best_diff);
}

void test_tm_pool_stats_sanitize_rejects_inf_best_diff(void)
{
    tm_pool_lifetime_stat_t sl;
    memset(&sl, 0, sizeof(sl));
    sl.best_diff = s_bits_to_double(UINT64_C(0x7FF0000000000000)); // +inf
    tm_pool_stats_sanitize_slot(&sl);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, sl.best_diff);
}

void test_tm_pool_stats_sanitize_rejects_zero_hash_clamp_sentinel(void)
{
    tm_pool_lifetime_stat_t sl;
    memset(&sl, 0, sizeof(sl));
    sl.best_diff    = 2e15;
    sl.best_diff_ts = 1700000000;
    tm_pool_stats_sanitize_slot(&sl);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, sl.best_diff);
    TEST_ASSERT_EQUAL_INT64(0, sl.best_diff_ts);
}

void test_tm_pool_stats_sanitize_caps_blocks_found(void)
{
    tm_pool_lifetime_stat_t sl;
    memset(&sl, 0, sizeof(sl));
    sl.blocks_found = 5000;
    tm_pool_stats_sanitize_slot(&sl);
    TEST_ASSERT_EQUAL_UINT32(0, sl.blocks_found);
}

void test_tm_pool_stats_sanitize_out_of_window_timestamp_does_not_cascade(void)
{
    tm_pool_lifetime_stat_t sl;
    sl.accepted_shares = 10;
    sl.hashes          = UINT64_C(123456789012);
    sl.best_diff       = 5.5;
    sl.best_diff_ts    = 1700000000; // sane
    sl.blocks_found    = 3;
    sl.last_seen_ts    = 1; // out of window -- too small to be a real wall-clock ts

    tm_pool_stats_sanitize_slot(&sl);

    TEST_ASSERT_EQUAL_UINT32(10, sl.accepted_shares);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(123456789012), sl.hashes);
    TEST_ASSERT_EQUAL_DOUBLE(5.5, sl.best_diff);
    TEST_ASSERT_EQUAL_INT64(1700000000, sl.best_diff_ts);
    TEST_ASSERT_EQUAL_UINT32(3, sl.blocks_found);
    TEST_ASSERT_EQUAL_INT64(0, sl.last_seen_ts); // only this field reset
}

void test_tm_pool_stats_sanitize_out_of_window_best_diff_ts_resets_only_itself(void)
{
    tm_pool_lifetime_stat_t sl;
    memset(&sl, 0, sizeof(sl));
    sl.best_diff    = 5.5; // sane, not the clamp sentinel
    sl.best_diff_ts = 999999999999; // out of window
    tm_pool_stats_sanitize_slot(&sl);
    TEST_ASSERT_EQUAL_DOUBLE(5.5, sl.best_diff); // NOT wiped
    TEST_ASSERT_EQUAL_INT64(0, sl.best_diff_ts);
}

void test_tm_pool_stats_sanitize_accepts_zero_timestamps(void)
{
    tm_pool_lifetime_stat_t sl;
    memset(&sl, 0, sizeof(sl));
    tm_pool_stats_sanitize_slot(&sl);
    TEST_ASSERT_EQUAL_INT64(0, sl.best_diff_ts);
    TEST_ASSERT_EQUAL_INT64(0, sl.last_seen_ts);
}

void test_tm_pool_stats_sanitize_null_is_a_noop(void)
{
    tm_pool_stats_sanitize_slot(NULL); // must not crash
}

void test_tm_pool_stats_load_sanitizes_corrupt_stored_record(void)
{
    reset_all();
    tm_pool_lifetime_stat_t corrupt;
    memset(&corrupt, 0, sizeof(corrupt));
    corrupt.accepted_shares = 7;
    corrupt.best_diff       = s_bits_to_double(UINT64_C(0x7FF8000000000000)); // NaN
    corrupt.blocks_found    = 5000; // over cap

    TEST_ASSERT_EQUAL(BB_OK, bb_config_set_blob(&s_probe_pool0_stat, &corrupt, sizeof(corrupt)));

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(0, &out));
    TEST_ASSERT_EQUAL_UINT32(7, out.accepted_shares); // untouched
    TEST_ASSERT_EQUAL_DOUBLE(0.0, out.best_diff);
    TEST_ASSERT_EQUAL_UINT32(0, out.blocks_found);
}

/* ---------------------------------------------------------------------------
 * Fault injection
 * ---------------------------------------------------------------------------*/

void test_tm_pool_stats_load_propagates_genuine_backend_fault(void)
{
    reset_all();
    fake_nvs_backend_fail_key("pool0_stat");

    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_stats_load(0, &out));
}

void test_tm_pool_stats_flush_propagates_genuine_backend_fault(void)
{
    reset_all();
    fake_nvs_backend_fail_set_key("pool0_stat");

    // record_block flushes immediately -- the fault surfaces on this call.
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_stats_record_block(0, 1000));
}

void test_tm_pool_stats_record_share_best_diff_flush_propagates_genuine_backend_fault(void)
{
    reset_all();
    fake_nvs_backend_fail_set_key("pool0_stat");
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_stats_record_share(0, 10.0, 1000));
}

/* ---------------------------------------------------------------------------
 * Bounds / NULL rejection
 * ---------------------------------------------------------------------------*/

void test_tm_pool_stats_load_rejects_out_of_range_idx(void)
{
    reset_all();
    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_stats_load(TM_POOL_MAX, &out));
}

void test_tm_pool_stats_load_rejects_null_out(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_stats_load(0, NULL));
}

void test_tm_pool_stats_record_share_rejects_out_of_range_idx(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_stats_record_share(TM_POOL_MAX, 1.0, 1));
}

void test_tm_pool_stats_record_hashes_rejects_out_of_range_idx(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_stats_record_hashes(TM_POOL_MAX, 1));
}

void test_tm_pool_stats_record_block_rejects_out_of_range_idx(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_stats_record_block(TM_POOL_MAX, 1));
}

void test_tm_pool_stats_flush_rejects_out_of_range_idx(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_stats_flush(TM_POOL_MAX));
}

void test_tm_pool_stats_reset_rejects_out_of_range_idx(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_stats_reset(TM_POOL_MAX));
}
