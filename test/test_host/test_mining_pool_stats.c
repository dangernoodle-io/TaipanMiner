#include "unity.h"
#include "mining_pool_stats.h"
#include "mining.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

/* setUp/tearDown reset module state before each test so cases are isolated. */
static void ps_setUp(void)
{
    mining_pool_stats_reset_for_test();
}

/* -------------------------------------------------------------------------
 * find_or_alloc: returns same slot for same host:port
 * ---------------------------------------------------------------------- */
void test_pool_stats_find_returns_existing_slot(void)
{
    ps_setUp();
    mining_pool_stat_t *a = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    mining_pool_stat_t *b = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_PTR(a, b);
}

/* -------------------------------------------------------------------------
 * find_or_alloc: case-insensitive host match
 * ---------------------------------------------------------------------- */
void test_pool_stats_case_insensitive_host_match(void)
{
    ps_setUp();
    mining_pool_stat_t *a = mining_pool_stats_find_or_alloc("POOL.example.com", 3333);
    mining_pool_stat_t *b = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_PTR(a, b);
}

/* -------------------------------------------------------------------------
 * find_or_alloc: different ports are different slots
 * ---------------------------------------------------------------------- */
void test_pool_stats_different_port_is_new_slot(void)
{
    ps_setUp();
    mining_pool_stat_t *a = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    mining_pool_stat_t *b = mining_pool_stats_find_or_alloc("pool.example.com", 3334);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_EQUAL(a, b);
}

/* -------------------------------------------------------------------------
 * find_or_alloc: alloc into empty slot
 * ---------------------------------------------------------------------- */
void test_pool_stats_alloc_into_empty_slot(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("acme-pool.example.com", 4444);
    TEST_ASSERT_NOT_NULL(sl);
    TEST_ASSERT_EQUAL_STRING("acme-pool.example.com", sl->host);
    TEST_ASSERT_EQUAL_UINT16(4444, sl->port);
    TEST_ASSERT_EQUAL_UINT32(0, sl->shares);
    TEST_ASSERT_EQUAL_UINT64(0, sl->hashes);
    TEST_ASSERT_EQUAL_UINT32(0, sl->blocks_found);
    TEST_ASSERT_NOT_EQUAL(0, sl->last_seen_us);
}

/* -------------------------------------------------------------------------
 * find_or_alloc: LRU eviction picks the slot with lowest last_seen_us
 * ---------------------------------------------------------------------- */
void test_pool_stats_lru_eviction_picks_lowest_last_seen(void)
{
    ps_setUp();

    /* Fill all 8 slots. Slot 0 will be allocated first → lowest last_seen_us. */
    mining_pool_stat_t *slots[MINING_POOL_STATS_MAX];
    char host[32];
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        snprintf(host, sizeof(host), "p%d.example.com", i);
        slots[i] = mining_pool_stats_find_or_alloc(host, 3333);
        TEST_ASSERT_NOT_NULL(slots[i]);
    }

    /* Record the pointer of slot 0 (earliest allocated = lowest last_seen_us). */
    mining_pool_stat_t *lru = slots[0];

    /* Allocate a 9th pool — must evict slot 0. */
    mining_pool_stat_t *newcomer = mining_pool_stats_find_or_alloc("new.example.com", 3333);
    TEST_ASSERT_NOT_NULL(newcomer);
    TEST_ASSERT_EQUAL_PTR(lru, newcomer);
    TEST_ASSERT_EQUAL_STRING("new.example.com", newcomer->host);
}

/* -------------------------------------------------------------------------
 * record_share: updates only the matching slot
 * ---------------------------------------------------------------------- */
void test_pool_stats_record_share_updates_matching_slot(void)
{
    ps_setUp();
    mining_pool_stat_t *a = mining_pool_stats_find_or_alloc("pool-a.example.com", 3333);
    mining_pool_stat_t *b = mining_pool_stats_find_or_alloc("pool-b.example.com", 3333);

    mining_pool_stats_record_share(a, 1024.0, 1750000000);
    mining_pool_stats_record_share(a, 512.0, 1750000001);   /* lower — should not update best_diff */

    TEST_ASSERT_EQUAL_UINT32(2, a->shares);
    TEST_ASSERT_EQUAL_DOUBLE(1024.0, a->best_diff);
    TEST_ASSERT_EQUAL_UINT32(0, b->shares);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, b->best_diff);
}

/* -------------------------------------------------------------------------
 * record_block: increments only the matching slot
 * ---------------------------------------------------------------------- */
void test_pool_stats_record_block_increments_matching_slot(void)
{
    ps_setUp();
    mining_pool_stat_t *a = mining_pool_stats_find_or_alloc("pool-a.example.com", 3333);
    mining_pool_stat_t *b = mining_pool_stats_find_or_alloc("pool-b.example.com", 3333);

    mining_pool_stats_record_block(a, 1750000010);
    mining_pool_stats_record_block(a, 1750000010);

    TEST_ASSERT_EQUAL_UINT32(2, a->blocks_found);
    TEST_ASSERT_EQUAL_UINT32(0, b->blocks_found);
    /* Device-lifetime counter also bumps on every block. */
    TEST_ASSERT_EQUAL_UINT32(2, mining_pool_stats_lifetime_blocks());
}

/* -------------------------------------------------------------------------
 * lifetime_blocks_total survives LRU eviction of a slot that held blocks.
 * ---------------------------------------------------------------------- */
void test_pool_stats_lifetime_blocks_survive_eviction(void)
{
    ps_setUp();
    mining_pool_stat_t *first = mining_pool_stats_find_or_alloc("first.pool", 1111);
    mining_pool_stats_record_block(first, 1750000020);
    TEST_ASSERT_EQUAL_UINT32(1, mining_pool_stats_lifetime_blocks());

    /* Fill the remaining 7 slots so the table is full. */
    char host[32];
    for (int i = 0; i < MINING_POOL_STATS_MAX - 1; i++) {
        snprintf(host, sizeof(host), "filler-%d.pool", i);
        mining_pool_stats_find_or_alloc(host, 2000 + i);
    }
    /* Allocating one more evicts the oldest slot — the original 'first.pool'
     * (lowest last_seen_us). Its blocks_found is lost, but the device-level
     * counter must remain. */
    mining_pool_stats_find_or_alloc("evict-trigger.pool", 9999);

    /* No 'first.pool' slot left. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        const mining_pool_stat_t *sl = mining_pool_stats_slot(i);
        TEST_ASSERT_NOT_NULL(sl);
        TEST_ASSERT_NOT_EQUAL(0, strcmp(sl->host, "first.pool"));
    }
    /* But the device-lifetime counter still reflects the block. */
    TEST_ASSERT_EQUAL_UINT32(1, mining_pool_stats_lifetime_blocks());
}

/* -------------------------------------------------------------------------
 * record_hashes: accumulates without clobbering other fields
 * ---------------------------------------------------------------------- */
void test_pool_stats_record_hashes_accumulates(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);

    mining_pool_stats_record_hashes(sl, 1000000ULL);
    mining_pool_stats_record_hashes(sl, 2000000ULL);

    TEST_ASSERT_EQUAL_UINT64(3000000ULL, sl->hashes);
    /* Other fields untouched. */
    TEST_ASSERT_EQUAL_UINT32(0, sl->shares);
    TEST_ASSERT_EQUAL_UINT32(0, sl->blocks_found);
}

/* -------------------------------------------------------------------------
 * NULL guards: record_share / record_hashes / record_block are no-ops.
 * ---------------------------------------------------------------------- */
void test_pool_stats_null_slot_is_safe(void)
{
    ps_setUp();
    mining_pool_stats_record_share(NULL, 1024.0, 1750000000);
    mining_pool_stats_record_hashes(NULL, 1000ULL);
    mining_pool_stats_record_block(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, mining_pool_stats_lifetime_blocks());
}

/* -------------------------------------------------------------------------
 * mining_pool_stats_slot bounds checking.
 * ---------------------------------------------------------------------- */
void test_pool_stats_slot_out_of_range_returns_null(void)
{
    ps_setUp();
    TEST_ASSERT_NULL(mining_pool_stats_slot(-1));
    TEST_ASSERT_NULL(mining_pool_stats_slot(MINING_POOL_STATS_MAX));
    TEST_ASSERT_NULL(mining_pool_stats_slot(MINING_POOL_STATS_MAX + 100));
    TEST_ASSERT_NOT_NULL(mining_pool_stats_slot(0));
}

/* -------------------------------------------------------------------------
 * LRU eviction: when slot[0] is recently touched, eviction selects a
 * different slot. Exercises the inner branch of the LRU loop.
 * ---------------------------------------------------------------------- */
void test_pool_stats_lru_eviction_skips_recent_slot(void)
{
    ps_setUp();

    mining_pool_stat_t *slots[MINING_POOL_STATS_MAX];
    char host[32];
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        snprintf(host, sizeof(host), "p%d.example.com", i);
        slots[i] = mining_pool_stats_find_or_alloc(host, 3333);
    }

    /* Re-touch slot 0 so its last_seen_us is now the largest, then slot 1
     * becomes the LRU target. */
    mining_pool_stats_find_or_alloc("p0.example.com", 3333);

    mining_pool_stat_t *newcomer = mining_pool_stats_find_or_alloc("new.example.com", 3333);
    TEST_ASSERT_NOT_NULL(newcomer);
    TEST_ASSERT_EQUAL_PTR(slots[1], newcomer);
    TEST_ASSERT_EQUAL_STRING("new.example.com", newcomer->host);
}

/* -------------------------------------------------------------------------
 * init / save: cover the load + save paths against the bb_nv host stubs.
 * The stubs don't persist, but every NVS call still executes — enough to
 * exercise s_load_slot, the legacy-erase loop, and the host save branch.
 * ---------------------------------------------------------------------- */
void test_pool_stats_init_runs_clean(void)
{
    ps_setUp();
    mining_pool_stats_init();
    /* With stub NVS, schema==0 → wipe → all slots zero. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        const mining_pool_stat_t *sl = mining_pool_stats_slot(i);
        TEST_ASSERT_NOT_NULL(sl);
        TEST_ASSERT_EQUAL_INT64(0, sl->last_seen_us);
    }
    TEST_ASSERT_EQUAL_UINT32(0, mining_pool_stats_lifetime_blocks());
}

void test_pool_stats_save_runs_clean(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    mining_pool_stats_record_share(sl, 4096.0, 1750000000);
    mining_pool_stats_record_block(sl, 1750000030);
    mining_pool_stats_record_hashes(sl, 12345678ULL);
    /* Exercises the host branch of mining_pool_stats_save (all 8 slots
     * serialised to bb_nv stubs). */
    mining_pool_stats_save();
    TEST_ASSERT_EQUAL_INT64(1750000000, sl->best_diff_ts);
    TEST_ASSERT_EQUAL_INT64(1750000030, sl->last_block_ts);
    TEST_ASSERT_EQUAL_INT64(1750000030, mining_pool_stats_lifetime_last_block_ts());
    TEST_ASSERT_EQUAL_UINT32(1, sl->shares);
    TEST_ASSERT_EQUAL_UINT32(1, sl->blocks_found);
}

/* -------------------------------------------------------------------------
 * block topic setter/getter round-trip.
 * ---------------------------------------------------------------------- */
void test_pool_stats_block_topic_roundtrip(void)
{
    ps_setUp();
    /* Use an arbitrary non-NULL sentinel; the API is opaque-handle. */
    bb_event_topic_t fake = (bb_event_topic_t)(uintptr_t)0xdeadbeefu;
    mining_pool_stats_set_block_topic(fake);
    TEST_ASSERT_EQUAL_PTR(fake, mining_pool_stats_get_block_topic());
    mining_pool_stats_set_block_topic(NULL);
    TEST_ASSERT_NULL(mining_pool_stats_get_block_topic());
}

/* =========================================================================
 * Corruption recovery tests — exercise s_sanitize_slot / s_sanitize_lifetime
 * via the test hooks exposed for host builds.
 * ======================================================================= */

/* -------------------------------------------------------------------------
 * best_diff: NaN is rejected, reset to 0.0.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_nan(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff = (double)NAN;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, sl->best_diff);
}

/* -------------------------------------------------------------------------
 * best_diff: negative value is rejected, reset to 0.0.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_negative(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff = -1.0;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, sl->best_diff);
}

/* -------------------------------------------------------------------------
 * best_diff: infinity is rejected.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_inf(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff = (double)INFINITY;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, sl->best_diff);
}

/* -------------------------------------------------------------------------
 * best_diff: valid value passes through unchanged.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_valid_passes_through(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff = 1024.5;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_DOUBLE(1024.5, sl->best_diff);
}

/* -------------------------------------------------------------------------
 * best_diff: very large finite value is now accepted (no magnitude ceiling).
 * A bitaxe at 1.4 TH/s can accumulate difficulty well above the old 1e25
 * cap; the correct mechanism for layout corruption is the schema sentinel.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_large_finite_accepted(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff = 1e30;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_DOUBLE(1e30, sl->best_diff);
}

/* -------------------------------------------------------------------------
 * best_diff_ts: out-of-range timestamp is reset to 0.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_ts_out_of_range(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    /* Pre-2020 timestamp — corrupt. */
    sl->best_diff_ts = (int64_t)1000000000; /* 2001-09-09 */
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_INT64(0, sl->best_diff_ts);
}

/* -------------------------------------------------------------------------
 * best_diff_ts: 0 (never set) is accepted.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_ts_zero_accepted(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff_ts = 0;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_INT64(0, sl->best_diff_ts);
}

/* -------------------------------------------------------------------------
 * best_diff_ts: valid timestamp passes through.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_ts_valid(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff_ts = (int64_t)1750000000; /* 2025 */
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_INT64(1750000000, sl->best_diff_ts);
}

/* -------------------------------------------------------------------------
 * last_seen_us: out-of-range timestamp reset to 0.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_last_seen_us_out_of_range(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->last_seen_us = (int64_t)999999999; /* pre-2020 */
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_INT64(0, sl->last_seen_us);
}

/* -------------------------------------------------------------------------
 * last_block_ts: out-of-range timestamp reset to 0.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_last_block_ts_out_of_range(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->last_block_ts = (int64_t)(-1);
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_INT64(0, sl->last_block_ts);
}

/* -------------------------------------------------------------------------
 * shares: large value is accepted — monotonic counters grow without bound.
 * (The old >UINT32_MAX/2 ceiling has been removed.)
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_shares_large_accepted(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->shares = 0xFFFFFFFFu;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    /* No sanitization for shares — passes through unchanged. */
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, sl->shares);
}

/* -------------------------------------------------------------------------
 * hashes: large value is accepted — bitaxe accumulates >1e18/year legitimately.
 * (The old >1e18 ceiling has been removed.)
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_hashes_large_accepted(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->hashes = (uint64_t)9000000000000000000ull; /* 9e18 — was rejected before */
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)9000000000000000000ull, sl->hashes);
}

/* -------------------------------------------------------------------------
 * Happy path: all sane values pass through unchanged.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_all_sane_values_unchanged(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff    = 8192.0;
    sl->best_diff_ts = (int64_t)1750000000;
    sl->shares       = 1000;
    sl->blocks_found = 2;
    sl->hashes       = 1000000000ULL;
    sl->last_block_ts = (int64_t)1750000001;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_DOUBLE(8192.0, sl->best_diff);
    TEST_ASSERT_EQUAL_INT64(1750000000, sl->best_diff_ts);
    TEST_ASSERT_EQUAL_UINT32(1000, sl->shares);
    TEST_ASSERT_EQUAL_UINT32(2, sl->blocks_found);
    TEST_ASSERT_EQUAL_UINT64(1000000000ULL, sl->hashes);
    TEST_ASSERT_EQUAL_INT64(1750000001, sl->last_block_ts);
}

/* -------------------------------------------------------------------------
 * lifetime: lifetime_last_block_ts out of range is reset to 0.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_lifetime_last_block_ts_corrupt(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    /* Record a block with a bad timestamp — record_block stores it directly. */
    mining_pool_stats_record_block(sl, (int64_t)42); /* pre-2020 — out of range */
    TEST_ASSERT_EQUAL_INT64(42, mining_pool_stats_lifetime_last_block_ts());
    mining_pool_stats_sanitize_lifetime_for_test();
    TEST_ASSERT_EQUAL_INT64(0, mining_pool_stats_lifetime_last_block_ts());
}

/* -------------------------------------------------------------------------
 * lifetime: valid last_block_ts passes through.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_lifetime_last_block_ts_valid(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    mining_pool_stats_record_block(sl, (int64_t)1750000000); /* 2025 */
    TEST_ASSERT_EQUAL_INT64(1750000000, mining_pool_stats_lifetime_last_block_ts());
    mining_pool_stats_sanitize_lifetime_for_test();
    TEST_ASSERT_EQUAL_INT64(1750000000, mining_pool_stats_lifetime_last_block_ts());
}

/* -------------------------------------------------------------------------
 * Cross-field: blocks==0 with non-zero lifetime_last_block_ts → ts reset.
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_lifetime_blocks_zero_ts_nonzero(void)
{
    ps_setUp();
    /* Directly set a non-zero ts with no blocks via the lifetime setter. */
    mining_pool_stats_set_lifetime_blocks_for_test(0u);
    /* Manually set the ts by going through record_block then zero-ing blocks. */
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    mining_pool_stats_record_block(sl, (int64_t)1750000000);
    /* Now force blocks back to zero while ts remains. */
    mining_pool_stats_set_lifetime_blocks_for_test(0u);
    TEST_ASSERT_EQUAL_INT64(1750000000, mining_pool_stats_lifetime_last_block_ts());
    mining_pool_stats_sanitize_lifetime_for_test();
    /* With blocks==0, the ts must be reset to 0. */
    TEST_ASSERT_EQUAL_INT64(0, mining_pool_stats_lifetime_last_block_ts());
}

/* -------------------------------------------------------------------------
 * lifetime: large lifetime_blocks_total is now accepted — no magnitude cap.
 * (The old >100 ceiling has been removed.)
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_lifetime_blocks_large_accepted(void)
{
    ps_setUp();
    mining_pool_stats_set_lifetime_blocks_for_test(25951u);
    TEST_ASSERT_EQUAL_UINT32(25951u, mining_pool_stats_lifetime_blocks());
    mining_pool_stats_sanitize_lifetime_for_test();
    /* No ceiling — large value passes through unchanged. */
    TEST_ASSERT_EQUAL_UINT32(25951u, mining_pool_stats_lifetime_blocks());
}

/* =========================================================================
 * Schema-sentinel tests
 * ======================================================================= */

/* -------------------------------------------------------------------------
 * Schema mismatch: init with stored schema != 2 wipes all data and resets.
 * ---------------------------------------------------------------------- */
void test_pool_stats_schema_mismatch_wipes_and_resets(void)
{
    ps_setUp();
    /* Simulate NVS containing schema=99 (wrong version) and some slot data
     * that would survive a normal load. We pre-populate in-memory state to
     * simulate what a stale-layout read would produce, then call init with
     * the mismatch injection so it exercises the wipe path. */
    mining_pool_stats_inject_schema_for_test(99u);
    /* Pre-load a slot so we can verify it is wiped. */
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    mining_pool_stats_record_share(sl, 512.0, 1750000000);
    TEST_ASSERT_EQUAL_UINT32(1, sl->shares);

    /* Call init — sees schema=99, wipes, resets to zeros. */
    mining_pool_stats_init();

    /* All slots must be zeroed after the wipe. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        const mining_pool_stat_t *s = mining_pool_stats_slot(i);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_EQUAL_INT64(0, s->last_seen_us);
        TEST_ASSERT_EQUAL_UINT32(0, s->shares);
    }
    TEST_ASSERT_EQUAL_UINT32(0, mining_pool_stats_lifetime_blocks());
}

/* -------------------------------------------------------------------------
 * Schema match: init with stored schema==3 loads data normally.
 * On host the bb_nv stubs are no-ops, so the "loaded" values come from the
 * stubs (all zeros). The test verifies that the wipe path is NOT taken —
 * i.e. the code falls through to the load path without error.
 * ---------------------------------------------------------------------- */
void test_pool_stats_schema_match_preserves_load_path(void)
{
    ps_setUp();
    /* Inject schema==3 — matches BB_POOL_STATS_SCHEMA_VERSION. */
    mining_pool_stats_inject_schema_for_test(3u);

    mining_pool_stats_init();

    /* Host stubs return 0 for all gets, so slots are zero — but the load
     * path ran (no wipe). We verify no assertion fired and init returned. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        const mining_pool_stat_t *sl = mining_pool_stats_slot(i);
        TEST_ASSERT_NOT_NULL(sl);
    }
    /* lifetime_blocks: stub returned 0. */
    TEST_ASSERT_EQUAL_UINT32(0, mining_pool_stats_lifetime_blocks());
}

/* -------------------------------------------------------------------------
 * Save writes schema: after save, get_saved_schema returns 3.
 * ---------------------------------------------------------------------- */
void test_pool_stats_save_writes_schema(void)
{
    ps_setUp();
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, mining_pool_stats_get_saved_schema_for_test());
    mining_pool_stats_save();
    TEST_ASSERT_EQUAL_UINT32(3u, mining_pool_stats_get_saved_schema_for_test());
}

/* -------------------------------------------------------------------------
 * Fresh install (schema key absent): init wipes cleanly, subsequent save
 * writes schema. On host, bb_nv stubs always return 0, simulating the
 * "key not found → fallback=0" path on a real partition.
 * ---------------------------------------------------------------------- */
void test_pool_stats_fresh_install_wipes_and_save_writes_schema(void)
{
    ps_setUp();
    /* No schema injection → bb_nv stub returns 0 → treated as fresh install. */
    mining_pool_stats_init();

    /* Slots are zero. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        const mining_pool_stat_t *sl = mining_pool_stats_slot(i);
        TEST_ASSERT_EQUAL_INT64(0, sl->last_seen_us);
    }

    /* Subsequent save writes the schema sentinel. */
    mining_pool_stats_save();
    TEST_ASSERT_EQUAL_UINT32(3u, mining_pool_stats_get_saved_schema_for_test());
}

/* -------------------------------------------------------------------------
 * Schema match with pre-seeded slot data: exercises the "slot populated"
 * branch of the init load path (lines guarded by host[0]!='\0' check).
 * ---------------------------------------------------------------------- */
void test_pool_stats_schema_match_loads_injected_slot(void)
{
    ps_setUp();
    mining_pool_stats_inject_schema_for_test(3u);

    /* Pre-seed slot 0 with a known share count and host. */
    mining_pool_stat_t pre = {0};
    strncpy(pre.host, "pool.example.com", sizeof(pre.host) - 1);
    pre.port   = 3333;
    pre.shares = 42;
    /* last_seen_us must be in valid timestamp range for the load path to
     * store the slot (the check is host[0]!='\0' || last_seen_us!=0). */
    pre.last_seen_us = 0; /* host[0]!='\0' is sufficient */
    mining_pool_stats_inject_slot_for_test(0, &pre);

    mining_pool_stats_init();

    /* Slot 0 should have been loaded with the injected data. */
    const mining_pool_stat_t *sl = mining_pool_stats_slot(0);
    TEST_ASSERT_NOT_NULL(sl);
    TEST_ASSERT_EQUAL_STRING("pool.example.com", sl->host);
    TEST_ASSERT_EQUAL_UINT32(42, sl->shares);
}

/* -------------------------------------------------------------------------
 * Schema match with pre-seeded lifetime blocks > 0: exercises the
 * "lifetime blocks" log branch in the init load path.
 * ---------------------------------------------------------------------- */
void test_pool_stats_schema_match_loads_injected_lifetime_blocks(void)
{
    ps_setUp();
    mining_pool_stats_inject_schema_for_test(3u);
    mining_pool_stats_inject_lifetime_blocks_for_test(5u);

    mining_pool_stats_init();

    TEST_ASSERT_EQUAL_UINT32(5u, mining_pool_stats_lifetime_blocks());
}

/* =========================================================================
 * Branch coverage gap-fill tests
 * ======================================================================= */

/* -------------------------------------------------------------------------
 * best_diff: negative infinity is rejected (covers !isinf after !isnan passes).
 * ---------------------------------------------------------------------- */
void test_pool_stats_recovery_best_diff_negative_inf(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    sl->best_diff = -INFINITY;
    mining_pool_stats_sanitize_slot_for_test(sl, 0);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, sl->best_diff);
}

/* -------------------------------------------------------------------------
 * init load path: slot has empty host but non-zero last_seen_us — covers the
 * "|| last_seen_us != 0" branch of the slot-store condition.
 * ---------------------------------------------------------------------- */
void test_pool_stats_schema_match_loads_slot_via_last_seen_us(void)
{
    ps_setUp();
    mining_pool_stats_inject_schema_for_test(3u);

    /* slot with empty host but a valid last_seen_us. */
    mining_pool_stat_t pre = {0};
    pre.last_seen_us = 1750000000; /* non-zero: 2025 Unix timestamp */
    pre.shares = 7;
    mining_pool_stats_inject_slot_for_test(0, &pre);

    mining_pool_stats_init();

    const mining_pool_stat_t *sl = mining_pool_stats_slot(0);
    TEST_ASSERT_NOT_NULL(sl);
    TEST_ASSERT_EQUAL_UINT32(7, sl->shares);
}

/* -------------------------------------------------------------------------
 * find_or_alloc: NULL host is handled safely (covers the "host ? host : """
 * ternary branch with host == NULL).
 * ---------------------------------------------------------------------- */
void test_pool_stats_find_or_alloc_null_host(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc(NULL, 3333);
    TEST_ASSERT_NOT_NULL(sl);
    /* Normalised empty string for NULL host. */
    TEST_ASSERT_EQUAL_STRING("", sl->host);
}

/* -------------------------------------------------------------------------
 * inject_slot_for_test: NULL sl and out-of-range idx are silently ignored
 * (covers the guard branches in mining_pool_stats_inject_slot_for_test).
 * ---------------------------------------------------------------------- */
void test_pool_stats_inject_slot_null_and_oob(void)
{
    ps_setUp();
    /* These should not crash or corrupt state. */
    mining_pool_stats_inject_slot_for_test(-1, NULL);
    mining_pool_stats_inject_slot_for_test(MINING_POOL_STATS_MAX, NULL);
    mining_pool_stat_t dummy = {0};
    mining_pool_stats_inject_slot_for_test(-1, &dummy);
    mining_pool_stats_inject_slot_for_test(MINING_POOL_STATS_MAX, &dummy);
    mining_pool_stats_inject_slot_for_test(0, NULL);
    /* State is unchanged: all slots empty. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        TEST_ASSERT_EQUAL_INT64(0, mining_pool_stats_slot(i)->last_seen_us);
    }
}

/* -------------------------------------------------------------------------
 * record_share / record_block: slot pointer outside the table → s_slot_index
 * returns -1 → the persist path is skipped (covers if (idx >= 0) false branch
 * and the out-of-range branch inside s_slot_index).
 * ---------------------------------------------------------------------- */
void test_pool_stats_record_share_out_of_table_slot_skips_persist(void)
{
    ps_setUp();
    /* Stack-allocated slot is outside the module's table array. */
    mining_pool_stat_t fake = {0};
    uint32_t before = mining_pool_stats_lifetime_blocks();
    mining_pool_stats_record_share(&fake, 1024.0, 1750000000);
    /* shares incremented on the fake slot, but nothing persisted. */
    TEST_ASSERT_EQUAL_UINT32(1, fake.shares);
    /* Lifetime counter unchanged — persist was skipped. */
    TEST_ASSERT_EQUAL_UINT32(before, mining_pool_stats_lifetime_blocks());
}

void test_pool_stats_record_block_out_of_table_slot_skips_persist(void)
{
    ps_setUp();
    mining_pool_stat_t fake = {0};
    uint32_t before = mining_pool_stats_lifetime_blocks();
    mining_pool_stats_record_block(&fake, 1750000000);
    TEST_ASSERT_EQUAL_UINT32(1, fake.blocks_found);
    /* Lifetime counter IS incremented (that happens before s_slot_index). */
    TEST_ASSERT_EQUAL_UINT32(before + 1, mining_pool_stats_lifetime_blocks());
}
