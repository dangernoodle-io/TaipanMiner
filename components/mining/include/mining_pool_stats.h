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
 * Load pool stats from NVS on startup; erase legacy lt_* keys.
 * Loaded values pass through the sanitizer (NaN/inf, timestamp range,
 * cross-field invariants) so corrupt fields self-heal without wiping
 * legitimate data.
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
 * Returns a pointer into the internal pool stats table (s_pool_stats.slots[]).
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
 * Zero all in-RAM pool stats (all 8 slots + lifetime_blocks_total +
 * lifetime_last_block_ts) and persist the zeroed state to NVS.
 * Takes the mining_stats mutex internally on ESP_PLATFORM.
 * Intended for the POST /api/stats/reset route (corrupt best_diff / phantom
 * block recovery without requiring a full factory reset).
 */
void mining_pool_stats_reset(void);

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
 * Test hook: zero-fill the in-memory pool stats table and reset injection
 * state. Call from setUp() to isolate tests from each other.
 */
void mining_pool_stats_reset_for_test(void);

/*
 * Test hook: run the NVS-corruption sanitizer on a single slot in-place.
 * Allows tests to pre-populate a slot with arbitrary (corrupt) values and
 * then assert that the sanitizer resets them to safe defaults.
 */
void mining_pool_stats_sanitize_slot_for_test(mining_pool_stat_t *sl, int idx);

/*
 * Test hook: run the NVS-corruption sanitizer on the lifetime counters.
 */
void mining_pool_stats_sanitize_lifetime_for_test(void);

/*
 * Test hook: directly set lifetime_blocks_total (bypasses record_block),
 * enabling tests to inject arbitrary values for sanitizer tests.
 */
void mining_pool_stats_set_lifetime_blocks_for_test(uint32_t v);

/*
 * Test hook: pre-seed slot `idx` with `*sl` so that the next
 * mining_pool_stats_init() call "loads" that data instead of calling the
 * bb_nv stubs (which always return zeros on host).
 * Allows coverage of the slot-populated load path.
 */
void mining_pool_stats_inject_slot_for_test(int idx, const mining_pool_stat_t *sl);

/*
 * Test hook: pre-seed the lifetime_blocks value that the next
 * mining_pool_stats_init() call will load.
 * UINT32_MAX = not injected (use bb_nv stub fallback = 0).
 */
void mining_pool_stats_inject_lifetime_blocks_for_test(uint32_t v);
#endif

#ifdef __cplusplus
}
#endif
