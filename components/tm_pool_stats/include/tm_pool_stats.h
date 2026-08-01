#pragma once

// tm_pool_stats -- per-pool LIFETIME record + scoreboard + sanitizers (TM v2
// pool subsystem: PR2/TA-569 built the lifetime record; PR3/TA-570 adds the
// per-pool scoreboard below). Companion to tm_pool_config (PR1):
// tm_pool_config owns the SETTINGS a slot is configured with
// (host/port/wallet/...); tm_pool_stats owns the LIFETIME COUNTERS and the
// per-slot top-N best-share SCOREBOARD accumulated while mining against
// that slot.
//
// Storage: one fixed-size BLOB per slot per record kind, via
// bb_config_get_blob/set_blob (namespace "tm_pool", same namespace
// tm_pool_config uses -- distinct per-slot keys, "pool%d_stat" for the
// lifetime record and "pool%d_score" for the scoreboard). See
// src/tm_pool_stats.c.
//
// Active-only-in-RAM discipline: exactly ONE slot's records (lifetime AND
// scoreboard) live in RAM at a time (the "active" or "cached" slot) --
// there is no v1-style multi-slot LRU. A record_* call targeting a
// different idx than the currently cached one flushes the prior cached
// slot's dirty records (if any) then loads the new slot's lifetime record
// and scoreboard from storage together. A best_diff improvement and
// record_block() persist the lifetime record immediately (rare, high-value
// events); record_hashes() only dirties the lifetime cache -- it is
// persisted by the next explicit tm_pool_stats_flush() call, normally
// driven by the periodic flush timer armed in tm_pool_stats_init(). A
// scoreboard change (tm_pool_scoreboard_maybe_insert returning true) is
// ALSO persisted immediately -- same rarity/high-value rationale as
// best_diff: a share good enough to enter the top 10 is a rare event, and
// losing it to a reboot between "accepted" and the next periodic flush
// would be a visible regression for a feature whose whole point is
// remembering best-ever shares. The two blobs (lifetime, scoreboard) are
// flushed independently -- a share can change one without the other (e.g.
// it beats the #5 slot on the scoreboard without beating the all-time
// best_diff).
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

#include <stdbool.h>
#include <stddef.h>
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

// -----------------------------------------------------------------------
// Scoreboard (PR3/TA-570): per-pool top-N best-share record.
// -----------------------------------------------------------------------

#define TM_POOL_SCOREBOARD_N 10

// A single scoreboard entry -- the FULL share record (not just the diff),
// so a UI can render "best share ever" with its provenance (when, which
// job, which nonce/extranonce2/version bits produced it).
typedef struct {
    double   diff;         // sort key (descending)
    int64_t  submitted_ts; // wall-clock unix seconds (when accepted)
    uint32_t ntime;        // header ntime submitted with the share
    uint32_t nonce;
    uint32_t version_bits;   // BIP320 rolled bits; meaningful only if version_rolled
    bool     version_rolled;
    uint8_t  hash_prefix[8]; // MSB 8 bytes of the 32-byte SHA256d, INTERNAL
                              // (little-endian, byte[31]=MSB) byte order --
                              // verbatim, NOT reversed. Use
                              // tm_pool_scoreboard_hash_to_hex() to render
                              // the conventional big-endian hex form.
    bool     hash_prefix_valid;
    uint8_t  extranonce2[8]; // raw bytes
    uint8_t  extranonce2_len; // <= 8
    char     job_id[32];      // truncated
} tm_pool_scoreboard_entry_t;

typedef struct {
    tm_pool_scoreboard_entry_t entries[TM_POOL_SCOREBOARD_N]; // sorted diff descending, [0..count)
    uint8_t                    count;
} tm_pool_scoreboard_t;

// Full-share input a caller passes to tm_pool_stats_record_share() /
// tm_pool_scoreboard_maybe_insert(). Carries the full 32-byte hash (or
// NULL); tm_pool_scoreboard_maybe_insert() extracts the 8-byte MSB prefix
// itself -- callers never touch hash_prefix directly.
typedef struct {
    double   diff;
    int64_t  submitted_ts;
    uint32_t ntime;
    uint32_t nonce;
    uint32_t version_bits;
    bool     version_rolled;
    const uint8_t *hash;        // 32 bytes (internal byte order) or NULL
    const uint8_t *extranonce2; // raw bytes, or NULL if extranonce2_len == 0
    uint8_t        extranonce2_len;
    const char    *job_id;
} tm_pool_share_t;

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

// Record an ACCEPTED share against slot `idx`: the ONE logical entry point
// that updates BOTH the lifetime record and the scoreboard together, under
// the same lock + active-slot cache. Increments accepted_shares, updates
// best_diff/best_diff_ts if share->diff beats the current best, sets
// last_seen_ts from share->submitted_ts, and runs
// tm_pool_scoreboard_maybe_insert() against the active scoreboard. A
// best_diff improvement persists the lifetime record immediately; a
// scoreboard change (maybe_insert returning true) persists the scoreboard
// record immediately -- independently of each other (see the file-level
// doc comment). A share that improves neither only dirties the lifetime
// cache (same as before).
bb_err_t tm_pool_stats_record_share(uint8_t idx, const tm_pool_share_t *share);

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

// Zero slot `idx`'s lifetime record AND scoreboard, in both RAM (if it is
// the currently active/cached slot) and storage.
bb_err_t tm_pool_stats_reset(uint8_t idx);

// Load slot `idx`'s scoreboard. Same active-slot-cache/sanitize contract as
// tm_pool_stats_load(): returns the in-RAM board if `idx` is the active
// slot (including inserts not yet flushed), otherwise reads storage
// directly. An unset slot resolves to an empty board (count == 0). The
// board read from storage is always sanitized first (see
// tm_pool_scoreboard_sanitize).
// Returns:
//   BB_OK                 loaded (*out populated, see above)
//   BB_ERR_INVALID_ARG    out is NULL, or idx >= TM_POOL_MAX
//   (other)               a genuine backend I/O error
bb_err_t tm_pool_scoreboard_load(uint8_t idx, tm_pool_scoreboard_t *out);

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

// -----------------------------------------------------------------------
// Scoreboard pure functions -- no I/O, no lock. Directly host-testable.
// -----------------------------------------------------------------------

// Insert `share` into `sb` if it qualifies for the top TM_POOL_SCOREBOARD_N
// (board not yet full, or share->diff beats the current minimum entry
// (entries[count-1], since the board is kept sorted descending)). On
// insert: the entry is placed in sorted position (shift-down for lower
// entries), the board is capped at TM_POOL_SCOREBOARD_N (a full board's
// lowest entry is evicted), hash_prefix[8] is extracted as the MSB 8 bytes
// of share->hash (hash[24..31], verbatim internal byte order) when
// share->hash != NULL (hash_prefix_valid=false, hash_prefix zeroed
// otherwise), job_id is truncated to entry->job_id's capacity (31 chars +
// NUL), and extranonce2_len is clamped to 8 (bytes beyond 8 are dropped).
//
// Returns true iff an insert happened (the board changed) -- the signal a
// caller uses to decide whether the scoreboard needs persisting. sb == NULL
// or share == NULL returns false without touching anything.
bool tm_pool_scoreboard_maybe_insert(tm_pool_scoreboard_t *sb, const tm_pool_share_t *share);

// Pure sanitizer: validates each entry's diff (same NaN/inf/1e15-sentinel
// raw-bits check as tm_pool_stats_sanitize_slot) and clamps
// extranonce2_len <= 8. A corrupt entry (bad diff) is DROPPED and the
// array is re-compacted (surviving entries shifted down to close the gap)
// rather than zeroed in place or the whole table reset -- one bad entry
// never costs the other (up to) 9. `count` is clamped to
// TM_POOL_SCOREBOARD_N and to the number of entries that survive
// validation. Struct-in/struct-out, no platform/storage access -- directly
// unit-testable. NULL is a no-op.
void tm_pool_scoreboard_sanitize(tm_pool_scoreboard_t *sb);

// Render entry `e`'s hash_prefix as the conventional human-readable
// big-endian hex string (leading zeros meaningful) into out[out_len].
// hash_prefix is stored in INTERNAL byte order (little-endian, matching
// the rest of this codebase's hash convention -- see CLAUDE.md); this
// function REVERSES the 8 bytes before hex-encoding (via bb_str) to
// produce the big-endian form humans expect from a block-explorer-style
// display. This is a cold-path DISPLAY helper only -- never call it from
// the record hot path (tm_pool_stats_record_share et al. keep hash_prefix
// in internal order specifically to avoid this reversal on every share).
//
// Returns the number of hex characters written (excluding the NUL
// terminator) on success. Returns -1 if e is NULL, e->hash_prefix_valid is
// false, out is NULL, or out_len is too small to hold the 16 hex chars + a
// NUL terminator (17 bytes).
int tm_pool_scoreboard_hash_to_hex(const tm_pool_scoreboard_entry_t *e, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
