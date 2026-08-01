// tm_pool_stats -- see include/tm_pool_stats.h for the public contract.
//
// Storage: two bb_config BLOB fields per slot, backend="nvs", namespace
// "tm_pool" (same namespace tm_pool_config uses, distinct keys) -- key
// "pool%d_stat" for the lifetime record, "pool%d_score" for the scoreboard
// (PR3/TA-570) -- both well under the real NVS 15-char key-name limit
// (longest here is "pool0_score" at 11 chars). Field tables are
// macro-generated (TM_POOL_STATS_DEFINE_SLOT / TM_POOL_STATS_DEFINE_SCORE_SLOT)
// mirroring tm_pool_config's own TM_POOL_DEFINE_SLOT pattern, to avoid
// hand-duplicating one descriptor x TM_POOL_MAX slots.
//
// Sanitizer bounds (best_diff NaN/inf via raw-bits exponent check, the
// zero-hash-clamp 1e15 sentinel, LIFETIME_BLOCKS_SANE_MAX, and the
// 2020..2100 timestamp sanity window) are intentionally DUPLICATED from
// v1's components/mining/src/mining_pool_stats.c verbatim, not factored
// into a shared header: v1 is dying wholesale at the v2 cutover (TM is a
// ground-up rewrite retiring components/mining entirely), so coupling this
// component to it would tie a live PR to code slated for deletion. Same
// rationale for the raw-bits check (avoids isnan()/isinf() phantom-branch
// coverage artifacts) and the non-cascading-reset invariant (a bad
// timestamp must never wipe accepted_shares/hashes/best_diff). The
// scoreboard's own sanitizer (tm_pool_scoreboard_sanitize) lives in
// tm_pool_scoreboard.c alongside its other pure logic
// (tm_pool_scoreboard_maybe_insert, tm_pool_scoreboard_hash_to_hex) -- this
// file only wires it into the load/activate path below.
//
// Concurrency: internally synchronized via bb_lock (s_lock) -- this
// component is inherently multi-task regardless of any caller discipline
// (hash accumulation from the mining core, share/block recording from the
// stratum task, and the self-wired periodic flush timer's callback, which
// runs on breadboard's shared bb_timer_disp task -- a task no composer-owned
// mutex could ever wrap). Every access to s_cache/s_score_cache/s_cache_idx/
// s_dirty/s_score_dirty (all record_*/flush/reset/load-of-cached-slot/
// activate, and the flush-timer callback) holds s_lock for its duration.
// The internal s_*_locked() helpers assume the caller already holds s_lock
// -- they never take or release it themselves.

#include "tm_pool_stats.h"
#include "tm_pool_stats_internal.h"

#include "bb_config.h"
#include "bb_lock.h"
#include "bb_lock_once.h"
#include "bb_log.h"

#ifdef ESP_PLATFORM
#include "bb_timer.h"
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#define TM_POOL_STATS_NVS_NS "tm_pool"

// Upper bound on the per-slot blocks_found counter. A SW/HW-SHA or ASIC
// miner on these boards cannot find real Bitcoin blocks (difficulty is many
// orders of magnitude above reach) -- see v1's identical constant/rationale
// in mining_pool_stats.c.
#define TM_POOL_STATS_BLOCKS_SANE_MAX 1024u

// Timestamp (unix seconds) sanity window: 2020-01-01 .. 2100-01-01. 0 (never
// set) is always accepted.
#define TM_POOL_STATS_TS_MIN ((int64_t)1577836800)
#define TM_POOL_STATS_TS_MAX ((int64_t)4102444800)

static const char *TAG = "tm_pool_stats";

// -----------------------------------------------------------------------
// Per-slot BLOB field table
// -----------------------------------------------------------------------

#define TM_POOL_STATS_DEFINE_SLOT(n) \
    static const bb_config_field_t s_pool##n##_stat = { \
        .id      = "pool" #n ".stat", \
        .type    = BB_CONFIG_BLOB, \
        .addr    = { .backend = "nvs", .ns_or_dir = TM_POOL_STATS_NVS_NS, .key = "pool" #n "_stat" }, \
        .max_len = sizeof(tm_pool_lifetime_stat_t), \
    }

TM_POOL_STATS_DEFINE_SLOT(0);
TM_POOL_STATS_DEFINE_SLOT(1);
TM_POOL_STATS_DEFINE_SLOT(2);

// TM_POOL_MAX-sized -- extending TM_POOL_MAX requires a new
// TM_POOL_STATS_DEFINE_SLOT(n) invocation above plus a new row here (a
// mismatch is a build error, not a silent runtime gap).
static const bb_config_field_t *s_pool_stat_fields[TM_POOL_MAX] = {
    &s_pool0_stat,
    &s_pool1_stat,
    &s_pool2_stat,
};

#define TM_POOL_STATS_DEFINE_SCORE_SLOT(n) \
    static const bb_config_field_t s_pool##n##_score = { \
        .id      = "pool" #n ".score", \
        .type    = BB_CONFIG_BLOB, \
        .addr    = { .backend = "nvs", .ns_or_dir = TM_POOL_STATS_NVS_NS, .key = "pool" #n "_score" }, \
        .max_len = sizeof(tm_pool_scoreboard_t), \
    }

TM_POOL_STATS_DEFINE_SCORE_SLOT(0);
TM_POOL_STATS_DEFINE_SCORE_SLOT(1);
TM_POOL_STATS_DEFINE_SCORE_SLOT(2);

// TM_POOL_MAX-sized, same discipline as s_pool_stat_fields above.
static const bb_config_field_t *s_pool_score_fields[TM_POOL_MAX] = {
    &s_pool0_score,
    &s_pool1_score,
    &s_pool2_score,
};

// -----------------------------------------------------------------------
// Active-only-in-RAM cache + internal lock. See the file-level doc comment
// above for the concurrency contract.
// -----------------------------------------------------------------------

static bb_once_t s_lock_once = BB_ONCE_INIT;
static bb_lock_t s_lock;

static tm_pool_lifetime_stat_t s_cache;
static tm_pool_scoreboard_t    s_score_cache;
static int                     s_cache_idx = -1; // -1 == no active slot -- shared by both caches
static bool                    s_dirty;
static bool                    s_score_dirty;

// Forward declarations -- defined below, alongside their sibling
// storage/cache helpers; needed here by the ESP-IDF-only flush-timer
// callback.
static bb_err_t s_flush_locked(void);
static bb_err_t s_score_flush_locked(void);

#ifdef ESP_PLATFORM
// ~10 minutes.
#define TM_POOL_STATS_FLUSH_PERIOD_US (600ULL * 1000000ULL)

static bb_periodic_timer_t s_flush_timer;

static void s_flush_timer_cb(void *arg)
{
    (void)arg;
    bb_lock_lock(&s_lock);
    if (s_cache_idx >= 0) {
        (void)s_flush_locked();
        (void)s_score_flush_locked();
    }
    bb_lock_unlock(&s_lock);
}
#endif

// -----------------------------------------------------------------------
// Sanitizer -- pure, no locking (no shared state touched).
// -----------------------------------------------------------------------

// NaN/inf check via raw-bits exponent test -- shared with the scoreboard's
// sanitizer (tm_pool_scoreboard.c) via tm_pool_stats_diff_is_sane()
// (tm_pool_stats_internal.h) rather than duplicated per-TU.
static bool s_ts_is_sane(int64_t ts)
{
    if (ts == 0) {
        return true;
    }
    return ts >= TM_POOL_STATS_TS_MIN && ts <= TM_POOL_STATS_TS_MAX;
}

void tm_pool_stats_sanitize_slot(tm_pool_lifetime_stat_t *sl)
{
    if (sl == NULL) {
        return;
    }

    if (!tm_pool_stats_diff_is_sane(sl->best_diff)) {
        bb_log_w(TAG, "best_diff corrupt (raw=%g); reset to 0", sl->best_diff);
        sl->best_diff = 0.0;
    }
    if (sl->best_diff >= 1e15) {
        bb_log_w(TAG, "best_diff=%g is the zero-hash clamp; reset to 0", sl->best_diff);
        sl->best_diff    = 0.0;
        sl->best_diff_ts = 0;
    }
    if (sl->blocks_found > TM_POOL_STATS_BLOCKS_SANE_MAX) {
        bb_log_w(TAG, "blocks_found=%" PRIu32 " implausible; reset to 0", sl->blocks_found);
        sl->blocks_found = 0;
    }
    if (!s_ts_is_sane(sl->best_diff_ts)) {
        bb_log_w(TAG, "best_diff_ts corrupt (raw=%" PRId64 "); reset to 0", sl->best_diff_ts);
        sl->best_diff_ts = 0;
    }
    if (!s_ts_is_sane(sl->last_seen_ts)) {
        bb_log_w(TAG, "last_seen_ts corrupt (raw=%" PRId64 "); reset to 0", sl->last_seen_ts);
        sl->last_seen_ts = 0;
    }
}

// -----------------------------------------------------------------------
// Storage + cache helpers. Every function below (down to and including
// s_activate_locked) assumes the caller already holds s_lock.
// -----------------------------------------------------------------------

// No shared state touched -- safe to call without s_lock held (kept
// separate from the _locked helpers below for that reason).
static bb_err_t s_load_from_storage(uint8_t idx, tm_pool_lifetime_stat_t *out)
{
    memset(out, 0, sizeof(*out));

    size_t   len = 0;
    bb_err_t err = bb_config_get_blob(s_pool_stat_fields[idx], out, sizeof(*out), &len);
    if (err == BB_ERR_NOT_FOUND) {
        return BB_OK; // unset slot -> zeroed record
    }
    if (err != BB_OK) {
        return err;
    }
    if (len != sizeof(*out)) {
        // Stored value is the wrong size for this build (schema drift or a
        // foreign write under this key) -- not a value-level corruption the
        // sanitizer can reason about field-by-field. Treat as unset.
        bb_log_w(TAG, "slot %d stat blob size mismatch (got %zu, want %zu); treating as unset",
                 idx, len, sizeof(*out));
        memset(out, 0, sizeof(*out));
        return BB_OK;
    }

    tm_pool_stats_sanitize_slot(out);
    return BB_OK;
}

// Persists s_cache to storage for s_cache_idx. Caller must hold s_lock and
// ensure s_cache_idx >= 0.
static bb_err_t s_flush_locked(void)
{
    bb_err_t err = bb_config_set_blob(s_pool_stat_fields[s_cache_idx], &s_cache, sizeof(s_cache));
    if (err == BB_OK) {
        s_dirty = false;
    }
    return err;
}

// Scoreboard counterparts of s_load_from_storage / s_flush_locked above --
// same contracts, targeting the "pool%d_score" blob instead of
// "pool%d_stat".
static bb_err_t s_score_load_from_storage(uint8_t idx, tm_pool_scoreboard_t *out)
{
    memset(out, 0, sizeof(*out));

    size_t   len = 0;
    bb_err_t err = bb_config_get_blob(s_pool_score_fields[idx], out, sizeof(*out), &len);
    if (err == BB_ERR_NOT_FOUND) {
        return BB_OK; // unset slot -> empty board
    }
    if (err != BB_OK) {
        return err;
    }
    if (len != sizeof(*out)) {
        bb_log_w(TAG, "slot %d score blob size mismatch (got %zu, want %zu); treating as unset",
                 idx, len, sizeof(*out));
        memset(out, 0, sizeof(*out));
        return BB_OK;
    }

    tm_pool_scoreboard_sanitize(out);
    return BB_OK;
}

// Persists s_score_cache to storage for s_cache_idx. Caller must hold
// s_lock and ensure s_cache_idx >= 0.
static bb_err_t s_score_flush_locked(void)
{
    bb_err_t err = bb_config_set_blob(s_pool_score_fields[s_cache_idx], &s_score_cache, sizeof(s_score_cache));
    if (err == BB_OK) {
        s_score_dirty = false;
    }
    return err;
}

// Ensures the active-slot cache holds `idx` (both the lifetime record AND
// the scoreboard -- they share one active-slot index): flushes the prior
// cached slot's dirty records (if any), then loads `idx`'s lifetime record
// and scoreboard in together. No-op if `idx` is already active. Caller
// must hold s_lock. On a flush failure, the prior cached slot is left
// untouched (still cached, still dirty) so a retry (e.g. a later
// tm_pool_stats_flush() call) can recover it -- the switch to `idx` never
// happens. Lifetime is flushed before scoreboard (arbitrary but fixed
// order); either failing aborts the switch before any load is attempted.
static bb_err_t s_activate_locked(uint8_t idx)
{
    if (s_cache_idx == (int)idx) {
        return BB_OK;
    }
    if (s_cache_idx >= 0) {
        if (s_dirty) {
            bb_err_t err = s_flush_locked();
            if (err != BB_OK) {
                return err;
            }
        }
        if (s_score_dirty) {
            bb_err_t err = s_score_flush_locked();
            if (err != BB_OK) {
                return err;
            }
        }
    }

    tm_pool_lifetime_stat_t loaded;
    bb_err_t                err = s_load_from_storage(idx, &loaded);
    if (err != BB_OK) {
        return err;
    }

    tm_pool_scoreboard_t loaded_score;
    err = s_score_load_from_storage(idx, &loaded_score);
    if (err != BB_OK) {
        return err;
    }

    s_cache       = loaded;
    s_score_cache = loaded_score;
    s_cache_idx   = (int)idx;
    s_dirty       = false;
    s_score_dirty = false;
    return BB_OK;
}

// -----------------------------------------------------------------------
// Public API -- each entry point (and the timer callback above) acquires
// s_lock for its full duration.
// -----------------------------------------------------------------------

bb_err_t tm_pool_stats_init(void)
{
    bb_lock_config_t cfg = { .name = "tm_pool_stats", .category = "pool" };
    bb_err_t         err = bb_lock_once_ensure(&s_lock_once, &cfg, &s_lock);
    if (err != BB_OK) {
        return err;
    }

    bb_lock_lock(&s_lock);
    s_cache_idx   = -1;
    s_dirty       = false;
    s_score_dirty = false;
    memset(&s_cache, 0, sizeof(s_cache));
    memset(&s_score_cache, 0, sizeof(s_score_cache));
    bb_lock_unlock(&s_lock);

#ifdef ESP_PLATFORM
    err = bb_timer_deferred_periodic_create(s_flush_timer_cb, NULL, "tm_pool_stats", &s_flush_timer);
    if (err != BB_OK) {
        return err;
    }
    err = bb_timer_periodic_start(s_flush_timer, TM_POOL_STATS_FLUSH_PERIOD_US);
    if (err != BB_OK) {
        return err;
    }
#endif
    return BB_OK;
}

bb_err_t tm_pool_stats_load(uint8_t idx, tm_pool_lifetime_stat_t *out)
{
    if (out == NULL || idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }

    bb_lock_lock(&s_lock);
    bb_err_t err;
    if (s_cache_idx == (int)idx) {
        *out = s_cache;
        err  = BB_OK;
    } else {
        err = s_load_from_storage(idx, out);
    }
    bb_lock_unlock(&s_lock);
    return err;
}

bb_err_t tm_pool_scoreboard_load(uint8_t idx, tm_pool_scoreboard_t *out)
{
    if (out == NULL || idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }

    bb_lock_lock(&s_lock);
    bb_err_t err;
    if (s_cache_idx == (int)idx) {
        *out = s_score_cache;
        err  = BB_OK;
    } else {
        err = s_score_load_from_storage(idx, out);
    }
    bb_lock_unlock(&s_lock);
    return err;
}

bb_err_t tm_pool_stats_record_share(uint8_t idx, const tm_pool_share_t *share)
{
    if (idx >= TM_POOL_MAX || share == NULL) {
        return BB_ERR_INVALID_ARG;
    }

    bb_lock_lock(&s_lock);
    bb_err_t err = s_activate_locked(idx);
    if (err == BB_OK) {
        s_cache.accepted_shares++;
        s_cache.last_seen_ts = share->submitted_ts;
        s_dirty              = true;

        bool best_improved = false;
        if (share->diff > s_cache.best_diff) {
            s_cache.best_diff    = share->diff;
            s_cache.best_diff_ts = share->submitted_ts;
            best_improved        = true;
        }

        bool sb_changed = tm_pool_scoreboard_maybe_insert(&s_score_cache, share);
        if (sb_changed) {
            s_score_dirty = true;
        }

        // Two independently rare, high-value events -- flush whichever
        // blob actually changed, immediately, and TRULY independently: a
        // lifetime-flush fault must not skip attempting the scoreboard
        // flush (or vice versa) -- each blob gets its own attempt
        // regardless of the other's outcome, and each stays dirty on its
        // own failure for a later retry (tm_pool_stats_flush). We surface
        // the FIRST error to the caller (arbitrary but fixed precedence:
        // lifetime before scoreboard) -- but both attempts always run. See
        // the header's file-level doc comment.
        bb_err_t lifetime_err = BB_OK;
        if (best_improved) {
            lifetime_err = s_flush_locked();
        }
        bb_err_t score_err = BB_OK;
        if (sb_changed) {
            score_err = s_score_flush_locked();
        }
        err = (lifetime_err != BB_OK) ? lifetime_err : score_err;
    }
    bb_lock_unlock(&s_lock);
    return err;
}

bb_err_t tm_pool_stats_record_hashes(uint8_t idx, uint64_t n)
{
    if (idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }

    bb_lock_lock(&s_lock);
    bb_err_t err = s_activate_locked(idx);
    if (err == BB_OK) {
        s_cache.hashes += n;
        s_dirty = true;
    }
    bb_lock_unlock(&s_lock);
    return err;
}

bb_err_t tm_pool_stats_record_block(uint8_t idx, int64_t now_ts)
{
    if (idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }

    bb_lock_lock(&s_lock);
    bb_err_t err = s_activate_locked(idx);
    if (err == BB_OK) {
        s_cache.blocks_found++;
        s_cache.last_seen_ts = now_ts;
        s_dirty              = true;
        err                  = s_flush_locked();
    }
    bb_lock_unlock(&s_lock);
    return err;
}

bb_err_t tm_pool_stats_flush(uint8_t idx)
{
    if (idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }

    bb_lock_lock(&s_lock);
    bb_err_t err = BB_OK;
    if (s_cache_idx == (int)idx) {
        err = s_flush_locked();
        if (err == BB_OK) {
            err = s_score_flush_locked();
        }
    } // else: nothing in RAM for this slot -- nothing to flush
    bb_lock_unlock(&s_lock);
    return err;
}

// Erases the stat blob then the score blob, in that fixed order. If the
// stat erase succeeds but the score erase then fails, storage is left
// split (stat erased, score not) and this function returns the score
// erase's error -- the caller can retry tm_pool_stats_reset(idx), which
// re-erases stat (already gone -- bb_config_erase is idempotent on an
// absent value) and retries score. RAM state (s_cache/s_score_cache) is
// left UNTOUCHED on any erase failure -- it is only zeroed once BOTH
// erases have succeeded, so a partial-erase failure never desyncs the
// active-slot cache from what's actually readable back out of it (the
// cache still reflects pre-reset values, which tm_pool_stats_load()/
// tm_pool_scoreboard_load() will keep returning until a successful reset
// or a slot switch reloads from storage).
bb_err_t tm_pool_stats_reset(uint8_t idx)
{
    if (idx >= TM_POOL_MAX) {
        return BB_ERR_INVALID_ARG;
    }

    bb_lock_lock(&s_lock);
    bb_err_t err = bb_config_erase(s_pool_stat_fields[idx]);
    if (err == BB_OK) {
        err = bb_config_erase(s_pool_score_fields[idx]);
    }
    if (err == BB_OK && s_cache_idx == (int)idx) {
        memset(&s_cache, 0, sizeof(s_cache));
        memset(&s_score_cache, 0, sizeof(s_score_cache));
        s_dirty       = false;
        s_score_dirty = false;
    }
    bb_lock_unlock(&s_lock);
    return err;
}
