// mining_pool_stats — in-RAM only in this PR (see mining_pool_stats.h for
// why NVS persistence / the sanitizer / the block.found bb_event topic are
// deferred). These tests cover the surviving surface: find_or_alloc (LRU),
// record_share/hashes/block, lifetime accessors, slot lookup, reset.

#include "unity.h"
#include "mining_pool_stats.h"
#include "mining.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

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

    mining_pool_stat_t *slots[MINING_POOL_STATS_MAX];
    char host[32];
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        snprintf(host, sizeof(host), "p%d.example.com", i);
        slots[i] = mining_pool_stats_find_or_alloc(host, 3333);
        TEST_ASSERT_NOT_NULL(slots[i]);
    }

    mining_pool_stat_t *lru = slots[0];

    mining_pool_stat_t *newcomer = mining_pool_stats_find_or_alloc("new.example.com", 3333);
    TEST_ASSERT_NOT_NULL(newcomer);
    TEST_ASSERT_EQUAL_PTR(lru, newcomer);
    TEST_ASSERT_EQUAL_STRING("new.example.com", newcomer->host);
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

    mining_pool_stats_find_or_alloc("p0.example.com", 3333);

    mining_pool_stat_t *newcomer = mining_pool_stats_find_or_alloc("new.example.com", 3333);
    TEST_ASSERT_NOT_NULL(newcomer);
    TEST_ASSERT_EQUAL_PTR(slots[1], newcomer);
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
 * record_block: increments only the matching slot + the lifetime counter
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

    char host[32];
    for (int i = 0; i < MINING_POOL_STATS_MAX - 1; i++) {
        snprintf(host, sizeof(host), "filler-%d.pool", i);
        mining_pool_stats_find_or_alloc(host, 2000 + i);
    }
    mining_pool_stats_find_or_alloc("evict-trigger.pool", 9999);

    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        const mining_pool_stat_t *sl = mining_pool_stats_slot(i);
        TEST_ASSERT_NOT_NULL(sl);
        TEST_ASSERT_NOT_EQUAL(0, strcmp(sl->host, "first.pool"));
    }
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
 * find_or_alloc: NULL host normalizes to empty string, doesn't crash.
 * ---------------------------------------------------------------------- */
void test_pool_stats_find_or_alloc_null_host(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc(NULL, 1234);
    TEST_ASSERT_NOT_NULL(sl);
    TEST_ASSERT_EQUAL_STRING("", sl->host);
}

/* -------------------------------------------------------------------------
 * init / reset: zero the table.
 * ---------------------------------------------------------------------- */
void test_pool_stats_init_runs_clean(void)
{
    ps_setUp();
    mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    mining_pool_stats_init();
    TEST_ASSERT_EQUAL_UINT32(0, mining_pool_stats_lifetime_blocks());
    TEST_ASSERT_EQUAL_INT64(0, mining_pool_stats_lifetime_last_block_ts());
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        const mining_pool_stat_t *sl = mining_pool_stats_slot(i);
        TEST_ASSERT_NOT_NULL(sl);
        TEST_ASSERT_EQUAL_STRING("", sl->host);
    }
}

void test_pool_stats_reset_zeroes_all_state(void)
{
    ps_setUp();
    mining_pool_stat_t *sl = mining_pool_stats_find_or_alloc("pool.example.com", 3333);
    mining_pool_stats_record_block(sl, 1750000000);
    TEST_ASSERT_EQUAL_UINT32(1, mining_pool_stats_lifetime_blocks());

    mining_pool_stats_reset();

    TEST_ASSERT_EQUAL_UINT32(0, mining_pool_stats_lifetime_blocks());
    TEST_ASSERT_EQUAL_INT64(0, mining_pool_stats_lifetime_last_block_ts());
    const mining_pool_stat_t *sl0 = mining_pool_stats_slot(0);
    TEST_ASSERT_NOT_NULL(sl0);
    TEST_ASSERT_EQUAL_STRING("", sl0->host);
}
