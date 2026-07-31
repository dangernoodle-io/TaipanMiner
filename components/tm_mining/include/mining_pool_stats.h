#pragma once

/*
 * mining_pool_stats.h — per-pool lifetime stats (8 slots, LRU eviction).
 *
 * In-RAM only in this PR. NVS persistence (the old ps<N>_<field> key
 * layout) is deferred: it lived entirely behind the now-removed legacy NVS
 * helper API (B1-708, replaced by bb_storage + bb_storage_nvs + bb_config).
 * Re-adding persistence here mirrors src/main.c's NVS/WiFi deferral.
 * Persistence rejoins once bb_storage_nvs lands.
 *
 * Host-compilable: no ESP-IDF includes in this header.
 */

#include "mining.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Zero-initialize the in-RAM pool stats table. Call once at boot (no NVS
 * load in this PR — see header comment).
 */
void mining_pool_stats_init(void);

/*
 * Look up the slot for host:port (case-insensitive host match).
 * If not found: allocate an empty slot, or evict the slot with the
 * smallest last_seen_us when all 8 are occupied. Updates last_seen_us.
 * Returns a pointer into the internal pool stats table.
 * Never returns NULL.
 */
mining_pool_stat_t *mining_pool_stats_find_or_alloc(const char *host, uint16_t port);

/*
 * Increment shares and update best_diff for the given slot. If share_diff
 * sets a new best, best_diff_ts is updated to `now_ts` (wall-clock unix
 * seconds; pass 0 if SNTP not yet synced — caller's choice).
 */
void mining_pool_stats_record_share(mining_pool_stat_t *slot,
                                    double               share_diff,
                                    int64_t              now_ts);

/*
 * Add n to hashes for the given slot.
 */
void mining_pool_stats_record_hashes(mining_pool_stat_t *slot, uint64_t n);

/*
 * Increment blocks_found for the given slot AND bump the device-lifetime
 * counter. Both per-slot last_block_ts and lifetime_last_block_ts are set
 * to `now_ts` (wall-clock unix seconds; 0 if SNTP not yet synced).
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
 * lifetime_last_block_ts). Intended for a future POST /api/stats/reset
 * route (corrupt best_diff / phantom block recovery).
 */
void mining_pool_stats_reset(void);

#ifndef ESP_PLATFORM
/*
 * Test hook: zero-fill the in-memory pool stats table. Call from setUp()
 * to isolate tests from each other.
 */
void mining_pool_stats_reset_for_test(void);
#endif

#ifdef __cplusplus
}
#endif
