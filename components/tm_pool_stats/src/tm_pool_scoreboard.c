// tm_pool_scoreboard -- pure per-slot top-N best-share logic. See
// include/tm_pool_stats.h for the public contract. No I/O, no lock -- the
// active-slot cache / storage integration (record_share, load, flush) lives
// in tm_pool_stats.c alongside the lifetime record it shares a lock with.
//
// Sanitizer bounds (diff NaN/inf via raw-bits exponent check) share
// tm_pool_stats_diff_is_sane() (tm_pool_stats_internal.h) with the lifetime
// record's sanitizer (tm_pool_stats.c's tm_pool_stats_sanitize_slot) -- see
// that header for why the raw-bits check is used instead of
// isnan()/isinf().

#include "tm_pool_stats.h"
#include "tm_pool_stats_internal.h"

#include "bb_log.h"
#include "bb_str.h"

#include <string.h>

static const char *TAG = "tm_pool_stats";

// -----------------------------------------------------------------------
// tm_pool_scoreboard_maybe_insert
// -----------------------------------------------------------------------

static void s_fill_entry(tm_pool_scoreboard_entry_t *e, const tm_pool_share_t *share)
{
    memset(e, 0, sizeof(*e));

    e->diff           = share->diff;
    e->submitted_ts   = share->submitted_ts;
    e->ntime          = share->ntime;
    e->nonce          = share->nonce;
    e->version_bits   = share->version_bits;
    e->version_rolled = share->version_rolled;

    if (share->hash != NULL) {
        memcpy(e->hash_prefix, &share->hash[24], sizeof(e->hash_prefix)); // MSB 8 bytes
        e->hash_prefix_valid = true;
    }

    // A NULL extranonce2 pointer means "no bytes supplied", regardless of
    // what extranonce2_len the caller passed -- otherwise the entry would
    // claim N zero bytes that were never actually provided.
    uint8_t en2_len = (share->extranonce2 != NULL) ? share->extranonce2_len : 0;
    if (en2_len > sizeof(e->extranonce2)) {
        en2_len = sizeof(e->extranonce2);
    }
    if (en2_len > 0) {
        memcpy(e->extranonce2, share->extranonce2, en2_len);
    }
    e->extranonce2_len = en2_len;

    if (share->job_id != NULL) {
        bb_strlcpy(e->job_id, share->job_id, sizeof(e->job_id));
    }
}

bool tm_pool_scoreboard_maybe_insert(tm_pool_scoreboard_t *sb, const tm_pool_share_t *share)
{
    if (sb == NULL || share == NULL) {
        return false;
    }

    bool full = sb->count >= TM_POOL_SCOREBOARD_N;
    if (full && share->diff <= sb->entries[sb->count - 1].diff) {
        return false; // doesn't beat the current minimum -- rejected
    }

    // Find the insert position: first index whose diff is < share->diff
    // (board is sorted descending).
    uint8_t pos = sb->count;
    for (uint8_t i = 0; i < sb->count; i++) {
        if (share->diff > sb->entries[i].diff) {
            pos = i;
            break;
        }
    }

    uint8_t new_count = full ? TM_POOL_SCOREBOARD_N : (uint8_t)(sb->count + 1);

    // Shift entries at/after `pos` down by one, dropping the last entry if
    // the board is (or becomes) full.
    for (uint8_t i = new_count - 1; i > pos; i--) {
        sb->entries[i] = sb->entries[i - 1];
    }

    s_fill_entry(&sb->entries[pos], share);
    sb->count = new_count;
    return true;
}

// -----------------------------------------------------------------------
// tm_pool_scoreboard_sanitize
// -----------------------------------------------------------------------

// The scoreboard uses no 1e15-zero-hash-clamp sentinel (that clamp is
// specific to mining_pool_stats's best_diff accumulation path, not
// applicable to a genuine submitted share's diff) -- only the shared
// NaN/inf/sign check (tm_pool_stats_diff_is_sane, tm_pool_stats_internal.h).
void tm_pool_scoreboard_sanitize(tm_pool_scoreboard_t *sb)
{
    if (sb == NULL) {
        return;
    }

    uint8_t count = sb->count;
    if (count > TM_POOL_SCOREBOARD_N) {
        bb_log_w(TAG, "scoreboard count=%u exceeds max; clamped", (unsigned)count);
        count = TM_POOL_SCOREBOARD_N;
    }

    uint8_t write = 0;
    for (uint8_t read = 0; read < count; read++) {
        tm_pool_scoreboard_entry_t *e = &sb->entries[read];

        if (!tm_pool_stats_diff_is_sane(e->diff)) {
            bb_log_w(TAG, "scoreboard entry %u diff corrupt (raw=%g); dropped", (unsigned)read, e->diff);
            continue; // drop -- do not advance `write`
        }

        if (e->extranonce2_len > sizeof(e->extranonce2)) {
            bb_log_w(TAG, "scoreboard entry %u extranonce2_len=%u over cap; clamped",
                     (unsigned)read, (unsigned)e->extranonce2_len);
            e->extranonce2_len = (uint8_t)sizeof(e->extranonce2);
        }

        if (write != read) {
            sb->entries[write] = *e; // re-compact -- close the gap left by a drop
        }
        write++;
    }

    sb->count = write;
}

// -----------------------------------------------------------------------
// tm_pool_scoreboard_hash_to_hex
// -----------------------------------------------------------------------

int tm_pool_scoreboard_hash_to_hex(const tm_pool_scoreboard_entry_t *e, char *out, size_t out_len)
{
    if (e == NULL || out == NULL || !e->hash_prefix_valid) {
        return -1;
    }

    // 8 bytes -> 16 hex chars + NUL.
    if (out_len < (sizeof(e->hash_prefix) * 2) + 1) {
        return -1;
    }

    // Reverse the internal (little-endian, byte[7]=MSB-of-prefix) byte
    // order into the conventional big-endian display order before hex
    // encoding -- see the header's doc comment. This is a cold-path
    // display helper only; the record hot path never does this.
    uint8_t reversed[sizeof(e->hash_prefix)];
    for (size_t i = 0; i < sizeof(reversed); i++) {
        reversed[i] = e->hash_prefix[sizeof(reversed) - 1 - i];
    }

    size_t written = bb_str_bytes_to_hex(reversed, sizeof(reversed), out, out_len);
    return (int)(written * 2);
}
