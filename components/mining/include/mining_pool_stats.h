#pragma once

/*
 * mining_pool_stats.h — per-pool lifetime stats (8 slots, LRU eviction).
 *
 * Host-compilable: no ESP-IDF includes in this header.
 */

#include "mining.h"

/* bb_event_topic_t is used in the block-topic setter/getter API below. */
#include "bb_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load pool_stats_v1 from NVS on startup; erase legacy lt_* keys.
 * Must be called after mining_stats_init() (mutex must exist on ESP).
 * Safe to call on host (in-memory bb_nv stubs).
 */
void mining_pool_stats_init(void);

/*
 * Persist current pool_stats table to NVS.
 * Called by the 10-min stats_save_task; also called after record_share
 * and record_block to persist immediately for those events.
 * Takes the mining_stats mutex internally on ESP_PLATFORM.
 */
void mining_pool_stats_save(void);

/*
 * Look up the slot for host:port (case-insensitive host match).
 * If not found: allocate an empty slot, or evict the slot with the
 * smallest last_seen_us when all 8 are occupied. Updates last_seen_us.
 * Returns a pointer into mining_stats.pool_stats.slots[].
 * Never returns NULL.
 */
mining_pool_stat_t *mining_pool_stats_find_or_alloc(const char *host, uint16_t port);

/*
 * Increment shares and update best_diff for the given slot. If share_diff
 * sets a new best, best_diff_ts is updated to `now_ts` (wall-clock unix
 * seconds; pass 0 if SNTP not yet synced — caller's choice).
 * Persists immediately.
 */
void mining_pool_stats_record_share(mining_pool_stat_t *slot,
                                    double               share_diff,
                                    int64_t              now_ts);

/*
 * Add n to hashes for the given slot.
 * NOT persisted on every call — only on the periodic save.
 */
void mining_pool_stats_record_hashes(mining_pool_stat_t *slot, uint64_t n);

/*
 * Increment blocks_found for the given slot AND bump the device-lifetime
 * counter. Both per-slot last_block_ts and lifetime_last_block_ts are set
 * to `now_ts` (wall-clock unix seconds; 0 if SNTP not yet synced).
 * Persists immediately.
 */
void mining_pool_stats_record_block(mining_pool_stat_t *slot, int64_t now_ts);

/* Read the device-lifetime block counter (survives slot LRU eviction). */
uint32_t mining_pool_stats_lifetime_blocks(void);

/* Read the device-lifetime "last block found" wall-clock timestamp.
 * 0 = no block has been found while SNTP was synced. */
int64_t mining_pool_stats_lifetime_last_block_ts(void);

/* Read a slot by index (NULL if out of range). Tests + diagnostics. */
const mining_pool_stat_t *mining_pool_stats_slot(int idx);

/*
 * Set the bb_event topic handle for "block.found" events.
 * Called from main.c after bb_event_topic_register.
 * Safe to call with NULL (disables event posting).
 * On host builds this is a no-op (bb_event stubs are absent).
 */
void mining_pool_stats_set_block_topic(bb_event_topic_t topic);

/*
 * Get the registered block.found topic handle (may be NULL).
 */
bb_event_topic_t mining_pool_stats_get_block_topic(void);

#ifndef ESP_PLATFORM
/*
 * Test hook: zero-fill the in-memory pool stats table.
 * Call from setUp() to isolate tests from each other.
 */
void mining_pool_stats_reset_for_test(void);
#endif

#ifdef __cplusplus
}
#endif
