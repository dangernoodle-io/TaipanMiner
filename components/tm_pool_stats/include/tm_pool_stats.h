#pragma once

// tm_pool_stats -- per-pool LIFETIME record + sanitizer (TM v2 pool
// subsystem PR2, TA-569). Companion to tm_pool_config (PR1): tm_pool_config
// owns the SETTINGS a slot is configured with (host/port/wallet/...);
// tm_pool_stats owns the LIFETIME COUNTERS accumulated while mining against
// that slot. The scoreboard (session-scoped, multi-slot at-a-glance view) is
// a separate, later PR (TA-570) -- not built here.
//
// Storage: one fixed-size BLOB per slot, via bb_config_get_blob/set_blob
// (namespace "tm_pool", same namespace tm_pool_config uses -- distinct
// per-slot keys, "pool%d_stat"). See src/tm_pool_stats.c.
//
// Active-only-in-RAM discipline: exactly ONE slot's record lives in RAM at a
// time (the "active" or "cached" slot) -- there is no v1-style multi-slot
// LRU. A record_* call targeting a different idx than the currently cached
// one flushes the prior cached slot (if it has unpersisted changes) then
// loads the new slot from storage. A best_diff improvement and
// record_block() persist immediately (rare, high-value events);
// record_hashes() only dirties the cache -- it is persisted by the next
// explicit tm_pool_stats_flush() call, normally driven by the periodic
// flush timer armed in tm_pool_stats_init().
//
// Concurrency: this component is internally synchronized (bb_core's
// bb_lock) -- callers need no external mutex. This is deliberate, not
// merely convenient: the self-wired periodic flush timer's callback runs on
// breadboard's shared bb_timer_disp task, a task no composer-owned mutex
// could ever wrap, so every code path that touches the active-slot cache
// (every function below, plus the timer callback) takes the same internal
// lock. Multiple callers may safely call into this component concurrently
// from different tasks (e.g. the mining core accumulating hashes, the
// stratum task recording shares/blocks, and the flush timer).

#include "bb_core.h"
#include "tm_pool_cfg.h" // TM_POOL_MAX -- see the header's own doc comment

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t accepted_shares;
    uint64_t hashes;
    double   best_diff;    // raw firmware best (not network-normalized)
    int64_t  best_diff_ts; // unix seconds, 0 = unset
    uint32_t blocks_found;
    int64_t  last_seen_ts; // unix seconds, 0 = unset
} tm_pool_lifetime_stat_t;

// Composition-tier init: resets the in-RAM active-slot cache and (on
// ESP-IDF only -- see src/tm_pool_stats.c) arms the ~10-minute periodic
// flush timer that persists the active slot's dirty record. Always returns
// BB_OK on host/native (no timer to fail); on-device, propagates a genuine
// bb_timer failure.
bb_err_t tm_pool_stats_init(void);

// Load slot `idx`'s lifetime record. If `idx` is the currently active
// (cached) slot, returns the in-RAM record (which may include changes not
// yet flushed to storage); otherwise reads storage directly. A slot with no
// stored record resolves to a zeroed *out. The record read from storage is
// always sanitized first (see tm_pool_stats_sanitize_slot).
// Returns:
//   BB_OK                 loaded (*out populated, see above)
//   BB_ERR_INVALID_ARG    out is NULL, or idx >= TM_POOL_MAX
//   (other)               a genuine backend I/O error
bb_err_t tm_pool_stats_load(uint8_t idx, tm_pool_lifetime_stat_t *out);

// Record an ACCEPTED share against slot `idx`: increments accepted_shares,
// updates best_diff/best_diff_ts if share_diff beats the current best, and
// sets last_seen_ts. A best_diff improvement is persisted immediately
// (before this call returns); otherwise the mutation only dirties the
// active-slot cache.
bb_err_t tm_pool_stats_record_share(uint8_t idx, double share_diff, int64_t now_ts);

// Accumulate `n` hashes onto slot `idx`. Dirties the active-slot cache only
// -- does NOT persist to storage on its own (see tm_pool_stats_flush).
bb_err_t tm_pool_stats_record_hashes(uint8_t idx, uint64_t n);

// Record a found block against slot `idx`: increments blocks_found and sets
// last_seen_ts. Always persisted immediately (rare, high-value event).
bb_err_t tm_pool_stats_record_block(uint8_t idx, int64_t now_ts);

// Persist the current in-RAM record for slot `idx` to storage. A no-op
// (returns BB_OK) if `idx` is not the currently active/cached slot -- per
// the active-only-in-RAM discipline, only the cached slot has anything to
// flush.
bb_err_t tm_pool_stats_flush(uint8_t idx);

// Zero slot `idx`'s lifetime record, in both RAM (if it is the currently
// active/cached slot) and storage.
bb_err_t tm_pool_stats_reset(uint8_t idx);

// Every entry point above rejects idx >= TM_POOL_MAX and any NULL required
// pointer argument with BB_ERR_INVALID_ARG.

// Pure sanitizer: resets individually-corrupt fields of *sl to safe
// defaults in place. A corrupt field is reset WITHOUT cascading to any
// other field, EXCEPT the documented best_diff/best_diff_ts pair (the
// zero-hash-clamp sentinel resets both together, since a clamped best_diff
// makes its own timestamp meaningless). In particular, a bad timestamp
// never wipes accepted_shares/hashes/best_diff. Struct-in/struct-out, no
// platform/storage access -- directly unit-testable. NULL is a no-op.
void tm_pool_stats_sanitize_slot(tm_pool_lifetime_stat_t *sl);

#ifdef __cplusplus
}
#endif
