/*
 * mining_pool_stats.c — per-pool lifetime stats (8 slots, LRU eviction).
 *
 * NVS storage: per-field keys ps<N>_<field> (e.g. ps0_host, ps1_port).
 * No blob API in bb_nv; per-field keys used throughout.
 *
 * Table access:
 *   ESP_PLATFORM — mining_stats.pool_stats (shared struct, guarded by mutex)
 *   Host/native  — file-static s_table (no FreeRTOS)
 *
 * Corruption recovery: NVS values are validated by the sanitizer (NaN/inf,
 * timestamp range, cross-field invariants) at load time. Lifetime counters
 * are preserved across firmware upgrades — no schema-version wipe path.
 */

#include "mining_pool_stats.h"
#include "mining.h"
#include "bb_nv.h"
#include "bb_log.h"
#include "bb_event.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include "freertos/semphr.h"
#endif
#include "bb_timer.h"

/* block.found event topic handle; set by mining_pool_stats_set_block_topic(). */
static bb_event_topic_t s_block_topic = NULL;

/* Upper bound on the device-lifetime block count and the per-slot blocks_found
 * counter. A SW/HW-SHA miner on these boards can't find real Bitcoin blocks
 * (difficulty is 15+ orders of magnitude above reach). Values above this are
 * persisted corruption from the classic-ESP32 DPORT zero-hash erratum where
 * all-zero hashes spuriously met every pool target and fired record_block(). */
#define LIFETIME_BLOCKS_SANE_MAX 1024u

static const char *TAG  = "pool_stats";
static const char *s_ns = "taipanminer";

/* TA-413: pool_stats lives in its own BSS global, not inside mining_stats_t.
 * Embedding 976 bytes in mining_stats_t shifted hot-loop BSS addresses and
 * caused ~1-3% hashrate loss on CPU boards.  The mutex from mining_stats
 * still guards all access (same contract as before — callers hold it). */
#ifndef ESP_PLATFORM
static mining_pool_stats_t s_table;
#define S_TABLE (&s_table)
#else
static mining_pool_stats_t s_pool_stats;
#define S_TABLE (&s_pool_stats)
#endif

/* -------------------------------------------------------------------------
 * Host-only test-injection state (compiled out on device)
 * ---------------------------------------------------------------------- */
#ifndef ESP_PLATFORM
/* Monotonically increasing timestamp counter. Reset by reset_for_test(). */
static int64_t s_host_clock = 1;

/* Pre-seeded NVS slot data: when valid[i], s_load_slot returns slots[i] directly
 * instead of calling bb_nv stubs. Covers the slot-populated init load path. */
static mining_pool_stat_t s_injected_slots[MINING_POOL_STATS_MAX];
static bool               s_injected_slot_valid[MINING_POOL_STATS_MAX];

/* Pre-seeded lifetime_blocks for init load path. UINT32_MAX = use stub (0). */
static uint32_t s_injected_lifetime_blocks = UINT32_MAX;
#endif

/* -------------------------------------------------------------------------
 * NVS corruption validators
 *
 * Validate only genuine corruption signatures: NaN/inf, out-of-range
 * timestamps, and cross-field invariants. Magnitude ceilings on monotonic
 * counters (shares, hashes, blocks) have been removed — legitimate values
 * grow without bound (a bitaxe at 1.4 TH/s accumulates ~4e19 hashes/year,
 * far above any finite ceiling).
 * ---------------------------------------------------------------------- */

/* best_diff (double): must be finite (not NaN, not inf) and non-negative.
 * Implemented via integer bit-test to avoid GCC FP instrumentation artifacts
 * in coverage builds (isfinite/isnan generate phantom exception-path branches
 * that are never reachable from C code). */
static bool s_value_is_sane_best_diff(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    /* NaN/inf: all exponent bits set (mask 0x7FF0000000000000). Sign: bit 63. */
    return (bits & UINT64_C(0x7FF0000000000000)) != UINT64_C(0x7FF0000000000000)
        && (bits >> 63) == 0;
}

/*
 * Timestamp (int64 wall-clock seconds): 0 (never set) is accepted.
 * Otherwise valid range: 2020-01-01 (1577836800) .. 2100-01-01 (4102444800).
 */
static bool s_value_is_sane_ts(int64_t ts)
{
    if (ts == 0) return true;
    return ts >= (int64_t)1577836800 && ts <= (int64_t)4102444800;
}

/* -------------------------------------------------------------------------
 * Per-slot and lifetime sanitization.
 * Resets corrupt fields to safe defaults and logs one line per corruption.
 * ---------------------------------------------------------------------- */

static void s_sanitize_slot(int idx, mining_pool_stat_t *sl)
{
    if (!s_value_is_sane_best_diff(sl->best_diff)) {
        bb_log_w(TAG, "pool_stats: slot %d best_diff corrupt (raw=%g); reset to 0", idx, sl->best_diff);
        sl->best_diff = 0.0;
    }
    if (sl->best_diff >= 1e15) {
        bb_log_w(TAG, "pool_stats: slot %d best_diff=%g is the zero-hash clamp; reset to 0", idx, sl->best_diff);
        sl->best_diff = 0.0;
        sl->best_diff_ts = 0;
    }
    if (sl->blocks_found > LIFETIME_BLOCKS_SANE_MAX) {
        bb_log_w(TAG, "pool_stats: slot %d blocks_found=%" PRIu32 " implausible; reset to 0", idx, sl->blocks_found);
        sl->blocks_found = 0;
        sl->last_block_ts = 0;
    }
    if (!s_value_is_sane_ts(sl->best_diff_ts)) {
        bb_log_w(TAG, "pool_stats: slot %d best_diff_ts corrupt (raw=%" PRId64 "); reset to 0",
                 idx, sl->best_diff_ts);
        sl->best_diff_ts = 0;
    }
    /* last_seen_us is esp_timer_get_time() microseconds-since-boot, NOT a
     * wall-clock timestamp. After a reboot it's always in the low-microsecond
     * range. Don't sanitize against the wall-clock range — that wrongly resets
     * it to 0, which makes find_or_alloc treat the slot as empty and memset
     * away the loaded shares/hashes/best_diff. Negative values would indicate
     * corruption but esp_timer is monotonic and non-negative. */
    if (sl->last_seen_us < 0) {
        bb_log_w(TAG, "pool_stats: slot %d last_seen_us negative (raw=%" PRId64 "); reset to 0",
                 idx, sl->last_seen_us);
        sl->last_seen_us = 0;
    }
    if (!s_value_is_sane_ts(sl->last_block_ts)) {
        bb_log_w(TAG, "pool_stats: slot %d last_block_ts corrupt (raw=%" PRId64 "); reset to 0",
                 idx, sl->last_block_ts);
        sl->last_block_ts = 0;
    }
}

static void s_sanitize_lifetime(mining_pool_stats_t *ps)
{
    if (ps->lifetime_blocks_total > LIFETIME_BLOCKS_SANE_MAX) {
        bb_log_w(TAG, "pool_stats: lifetime_blocks_total=%" PRIu32 " implausible; reset to 0",
                 ps->lifetime_blocks_total);
        ps->lifetime_blocks_total = 0;
        ps->lifetime_last_block_ts = 0;
    }
    if (!s_value_is_sane_ts(ps->lifetime_last_block_ts)) {
        bb_log_w(TAG, "pool_stats: lifetime_last_block_ts corrupt (raw=%" PRId64 "); reset to 0",
                 ps->lifetime_last_block_ts);
        ps->lifetime_last_block_ts = 0;
    }
    /* Cross-field: a "last block timestamp" is only meaningful if at least
     * one block has been found. If blocks==0 but ts is non-zero, the ts is
     * stale corruption. */
    if (ps->lifetime_blocks_total == 0 && ps->lifetime_last_block_ts != 0) {
        bb_log_w(TAG, "pool_stats: lifetime_last_block_ts=%" PRId64 " with 0 blocks; reset to 0",
                 ps->lifetime_last_block_ts);
        ps->lifetime_last_block_ts = 0;
    }
}

/* -------------------------------------------------------------------------
 * NVS key helpers  (key format: ps<N>_<field>, max 15 chars)
 * ---------------------------------------------------------------------- */

static void s_key(char buf[16], int slot, const char *field)
{
    snprintf(buf, 16, "ps%d_%s", slot, field);
}

/* -------------------------------------------------------------------------
 * Save / load one slot
 * ---------------------------------------------------------------------- */

static void s_save_slot(int idx, const mining_pool_stat_t *sl)
{
    char key[16];
    uint32_t hi, lo;

    s_key(key, idx, "host");
    bb_nv_set_str(s_ns, key, sl->host);

    s_key(key, idx, "port");
    bb_nv_set_u32(s_ns, key, sl->port);

    s_key(key, idx, "shr");
    bb_nv_set_u32(s_ns, key, sl->shares);

    s_key(key, idx, "h_lo");
    bb_nv_set_u32(s_ns, key, (uint32_t)(sl->hashes & 0xFFFFFFFFu));
    s_key(key, idx, "h_hi");
    bb_nv_set_u32(s_ns, key, (uint32_t)(sl->hashes >> 32));

    /* pack best_diff (double) as two u32 */
    {
        uint64_t bits;
        memcpy(&bits, &sl->best_diff, sizeof(bits));
        hi = (uint32_t)(bits >> 32);
        lo = (uint32_t)(bits & 0xFFFFFFFFu);
    }
    s_key(key, idx, "bd_hi");
    bb_nv_set_u32(s_ns, key, hi);
    s_key(key, idx, "bd_lo");
    bb_nv_set_u32(s_ns, key, lo);

    s_key(key, idx, "blk");
    bb_nv_set_u32(s_ns, key, sl->blocks_found);

    /* pack last_seen_us (int64) as two u32 */
    hi = (uint32_t)((uint64_t)sl->last_seen_us >> 32);
    lo = (uint32_t)((uint64_t)sl->last_seen_us & 0xFFFFFFFFu);
    s_key(key, idx, "ls_hi");
    bb_nv_set_u32(s_ns, key, hi);
    s_key(key, idx, "ls_lo");
    bb_nv_set_u32(s_ns, key, lo);

    /* best_diff_ts (int64 wall-clock seconds) */
    hi = (uint32_t)((uint64_t)sl->best_diff_ts >> 32);
    lo = (uint32_t)((uint64_t)sl->best_diff_ts & 0xFFFFFFFFu);
    s_key(key, idx, "bdt_hi");
    bb_nv_set_u32(s_ns, key, hi);
    s_key(key, idx, "bdt_lo");
    bb_nv_set_u32(s_ns, key, lo);

    /* last_block_ts (int64 wall-clock seconds) */
    hi = (uint32_t)((uint64_t)sl->last_block_ts >> 32);
    lo = (uint32_t)((uint64_t)sl->last_block_ts & 0xFFFFFFFFu);
    s_key(key, idx, "lbt_hi");
    bb_nv_set_u32(s_ns, key, hi);
    s_key(key, idx, "lbt_lo");
    bb_nv_set_u32(s_ns, key, lo);
}

static void s_load_slot(int idx, mining_pool_stat_t *sl)
{
#ifndef ESP_PLATFORM
    /* idx is always in [0, MAX-1] from the for-loop in mining_pool_stats_init;
     * inject_slot_for_test already validates bounds before setting valid[]. */
    if (s_injected_slot_valid[idx]) {
        *sl = s_injected_slots[idx];
        return;
    }
#endif
    char key[16];
    uint32_t hi = 0, lo = 0;

    s_key(key, idx, "host");
    bb_nv_get_str(s_ns, key, sl->host, sizeof(sl->host), "");
    sl->host[sizeof(sl->host) - 1] = '\0';

    s_key(key, idx, "port");
    {
        uint32_t p = 0;
        bb_nv_get_u32(s_ns, key, &p, 0);
        sl->port = (uint16_t)p;
    }

    s_key(key, idx, "shr");
    bb_nv_get_u32(s_ns, key, &sl->shares, 0);

    hi = 0; lo = 0;
    s_key(key, idx, "h_lo");
    bb_nv_get_u32(s_ns, key, &lo, 0);
    s_key(key, idx, "h_hi");
    bb_nv_get_u32(s_ns, key, &hi, 0);
    sl->hashes = ((uint64_t)hi << 32) | lo;

    hi = 0; lo = 0;
    s_key(key, idx, "bd_hi");
    bb_nv_get_u32(s_ns, key, &hi, 0);
    s_key(key, idx, "bd_lo");
    bb_nv_get_u32(s_ns, key, &lo, 0);
    {
        uint64_t bits = ((uint64_t)hi << 32) | lo;
        memcpy(&sl->best_diff, &bits, sizeof(sl->best_diff));
    }

    s_key(key, idx, "blk");
    bb_nv_get_u32(s_ns, key, &sl->blocks_found, 0);

    hi = 0; lo = 0;
    s_key(key, idx, "ls_hi");
    bb_nv_get_u32(s_ns, key, &hi, 0);
    s_key(key, idx, "ls_lo");
    bb_nv_get_u32(s_ns, key, &lo, 0);
    sl->last_seen_us = (int64_t)(((uint64_t)hi << 32) | lo);

    hi = 0; lo = 0;
    s_key(key, idx, "bdt_hi");
    bb_nv_get_u32(s_ns, key, &hi, 0);
    s_key(key, idx, "bdt_lo");
    bb_nv_get_u32(s_ns, key, &lo, 0);
    sl->best_diff_ts = (int64_t)(((uint64_t)hi << 32) | lo);

    hi = 0; lo = 0;
    s_key(key, idx, "lbt_hi");
    bb_nv_get_u32(s_ns, key, &hi, 0);
    s_key(key, idx, "lbt_lo");
    bb_nv_get_u32(s_ns, key, &lo, 0);
    sl->last_block_ts = (int64_t)(((uint64_t)hi << 32) | lo);
}

/* -------------------------------------------------------------------------
 * Timestamp helper
 * ---------------------------------------------------------------------- */

static int64_t s_now_us(void)
{
#ifdef ESP_PLATFORM
    return (int64_t)bb_timer_now_us();
#else
    return s_host_clock++;
#endif
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void mining_pool_stats_init(void)
{
    /* Erase legacy lifetime keys unconditionally. */
    static const char *s_legacy[] = {
        "lt_shares", "lt_hashes_lo", "lt_hashes_hi", "lt_best_hi", "lt_best_lo"
    };
    for (int i = 0; i < (int)(sizeof(s_legacy) / sizeof(s_legacy[0])); i++) {
        bb_nv_erase(s_ns, s_legacy[i]);
    }

    /* Load device-lifetime block counter. */
    uint32_t lifetime_blocks = 0;
#ifndef ESP_PLATFORM
    if (s_injected_lifetime_blocks != UINT32_MAX) {
        lifetime_blocks = s_injected_lifetime_blocks;
    } else {
        bb_nv_get_u32(s_ns, "ps_life_blocks", &lifetime_blocks, 0);
    }
#else
    bb_nv_get_u32(s_ns, "ps_life_blocks", &lifetime_blocks, 0);
#endif
    S_TABLE->lifetime_blocks_total = lifetime_blocks;

    /* Load device-lifetime "last block found" wall-clock timestamp. */
    {
        uint32_t hi = 0, lo = 0;
        bb_nv_get_u32(s_ns, "ps_lblk_hi", &hi, 0);
        bb_nv_get_u32(s_ns, "ps_lblk_lo", &lo, 0);
        S_TABLE->lifetime_last_block_ts = (int64_t)(((uint64_t)hi << 32) | lo);
    }

    /* Sanitize lifetime fields before use. */
    s_sanitize_lifetime(S_TABLE);

    /* Load per-slot data from NVS. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        mining_pool_stat_t sl;
        memset(&sl, 0, sizeof(sl));
        s_load_slot(i, &sl);
        s_sanitize_slot(i, &sl);
        if (sl.host[0] != '\0' || sl.last_seen_us != 0) {
            S_TABLE->slots[i] = sl;
            bb_log_i(TAG, "slot %d: %s:%" PRIu16 " shr=%" PRIu32,
                     i, sl.host, sl.port, sl.shares);
        }
    }
    if (S_TABLE->lifetime_blocks_total > 0) {
        bb_log_i(TAG, "lifetime blocks: %" PRIu32, S_TABLE->lifetime_blocks_total);
    }
}

/* Persist a single slot + the device-lifetime counter. Used by hot-path
 * recorders (every share accept) so we don't write all 8 slots × 7 fields
 * each = ~56 NVS ops per share, which would starve IDLE0 on Core 0 and
 * trip the task watchdog on stratum-busy boards. */
static void s_save_one_slot_and_lifetime(int idx, const mining_pool_stat_t *slot,
                                         uint32_t lifetime_blocks,
                                         int64_t  lifetime_last_block_ts)
{
    s_save_slot(idx, slot);
    bb_nv_set_u32(s_ns, "ps_life_blocks", lifetime_blocks);
    uint32_t hi = (uint32_t)((uint64_t)lifetime_last_block_ts >> 32);
    uint32_t lo = (uint32_t)((uint64_t)lifetime_last_block_ts & 0xFFFFFFFFu);
    bb_nv_set_u32(s_ns, "ps_lblk_hi", hi);
    bb_nv_set_u32(s_ns, "ps_lblk_lo", lo);
}

/* Return the index of `slot` within the live table, or -1 if not found.
 * Integer arithmetic on uintptr_t avoids UB from NULL/out-of-range pointers
 * and collapses the NULL, slot<base, and slot>=base+MAX checks into one
 * unsigned comparison — a single branch for coverage. */
static int s_slot_index(const mining_pool_stat_t *slot)
{
    uintptr_t base = (uintptr_t)(void *)S_TABLE->slots;
    uintptr_t addr = (uintptr_t)(void *)slot;   /* 0 if slot==NULL */
    uintptr_t off  = addr - base;               /* wraps if addr < base */
    if (off >= (uintptr_t)MINING_POOL_STATS_MAX * sizeof(mining_pool_stat_t)) return -1;
    return (int)(off / sizeof(mining_pool_stat_t));
}

void mining_pool_stats_save(void)
{
#ifdef ESP_PLATFORM
    mining_pool_stats_t snap;
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        snap = s_pool_stats;
        xSemaphoreGive(mining_stats.mutex);
    } else {
        bb_log_w(TAG, "save: mutex timeout");
        return;
    }
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        s_save_slot(i, &snap.slots[i]);
    }
    bb_nv_set_u32(s_ns, "ps_life_blocks", snap.lifetime_blocks_total);
    {
        uint32_t hi = (uint32_t)((uint64_t)snap.lifetime_last_block_ts >> 32);
        uint32_t lo = (uint32_t)((uint64_t)snap.lifetime_last_block_ts & 0xFFFFFFFFu);
        bb_nv_set_u32(s_ns, "ps_lblk_hi", hi);
        bb_nv_set_u32(s_ns, "ps_lblk_lo", lo);
    }
#else
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        s_save_slot(i, &s_table.slots[i]);
    }
    bb_nv_set_u32(s_ns, "ps_life_blocks", s_table.lifetime_blocks_total);
    {
        uint32_t hi = (uint32_t)((uint64_t)s_table.lifetime_last_block_ts >> 32);
        uint32_t lo = (uint32_t)((uint64_t)s_table.lifetime_last_block_ts & 0xFFFFFFFFu);
        bb_nv_set_u32(s_ns, "ps_lblk_hi", hi);
        bb_nv_set_u32(s_ns, "ps_lblk_lo", lo);
    }
#endif
}

void mining_pool_stats_reset(void)
{
#ifdef ESP_PLATFORM
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&s_pool_stats, 0, sizeof(s_pool_stats));
        xSemaphoreGive(mining_stats.mutex);
    } else {
        bb_log_w(TAG, "reset: mutex timeout");
        return;
    }
#else
    memset(&s_table, 0, sizeof(s_table));
#endif
    mining_pool_stats_save();
}

mining_pool_stat_t *mining_pool_stats_find_or_alloc(const char *host, uint16_t port)
{
    mining_pool_stats_t *ps = S_TABLE;

    /* Normalise host to lowercase for comparison. */
    char norm[64];
    strncpy(norm, host ? host : "", sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    for (int c = 0; norm[c]; c++) {
        norm[c] = (char)tolower((unsigned char)norm[c]);
    }

    /* Case-insensitive host + exact port lookup. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        mining_pool_stat_t *sl = &ps->slots[i];
        if (sl->last_seen_us == 0) continue;
        char sl_norm[64];
        strncpy(sl_norm, sl->host, sizeof(sl_norm) - 1);
        sl_norm[sizeof(sl_norm) - 1] = '\0';
        for (int c = 0; sl_norm[c]; c++) {
            sl_norm[c] = (char)tolower((unsigned char)sl_norm[c]);
        }
        if (sl->port == port && strcmp(sl_norm, norm) == 0) {
            sl->last_seen_us = s_now_us();
            return sl;
        }
    }

    /* Find an empty slot first. */
    mining_pool_stat_t *target = NULL;
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        if (ps->slots[i].last_seen_us == 0) {
            target = &ps->slots[i];
            break;
        }
    }

    /* No empty slot — evict LRU (smallest last_seen_us). */
    if (!target) {
        target = &ps->slots[0];
        for (int i = 1; i < MINING_POOL_STATS_MAX; i++) {
            if (ps->slots[i].last_seen_us < target->last_seen_us) {
                target = &ps->slots[i];
            }
        }
        bb_log_i(TAG, "evicting slot %s:%" PRIu16, target->host, target->port);
    }

    /* Zero-fill and populate. */
    memset(target, 0, sizeof(*target));
    strncpy(target->host, norm, sizeof(target->host) - 1);
    target->host[sizeof(target->host) - 1] = '\0';
    target->port = port;
    target->last_seen_us = s_now_us();
    return target;
}

void mining_pool_stats_record_share(mining_pool_stat_t *slot,
                                    double               share_diff,
                                    int64_t              now_ts)
{
    if (!slot) return;
    slot->shares++;
    if (share_diff > slot->best_diff) {
        slot->best_diff = share_diff;
        slot->best_diff_ts = now_ts;
    }
    /* Hot path — persist ONLY this slot, not all 8. Saving the full table
     * here is ~56 NVS ops (~500 ms) and starved IDLE0 on stratum-busy boards
     * (Core 0 task watchdog tripped on tdongle). The 10-min periodic save
     * still flushes everything for hashes accumulation. */
    int idx = s_slot_index(slot);
    if (idx >= 0) {
        s_save_one_slot_and_lifetime(idx, slot,
                                     S_TABLE->lifetime_blocks_total,
                                     S_TABLE->lifetime_last_block_ts);
    }
}

void mining_pool_stats_record_hashes(mining_pool_stat_t *slot, uint64_t n)
{
    if (!slot) return;
    slot->hashes += n;
    /* Not persisted here — only on periodic save. */
}

void mining_pool_stats_record_block(mining_pool_stat_t *slot, int64_t now_ts)
{
    if (!slot) return;
    slot->blocks_found++;
    slot->last_block_ts = now_ts;
    /* Also bump the device-lifetime counter (never evicted). */
    S_TABLE->lifetime_blocks_total++;
    S_TABLE->lifetime_last_block_ts = now_ts;
    /* Persist only this slot + the lifetime counter (see record_share). */
    int idx = s_slot_index(slot);
    if (idx >= 0) {
        s_save_one_slot_and_lifetime(idx, slot,
                                     S_TABLE->lifetime_blocks_total,
                                     S_TABLE->lifetime_last_block_ts);
    }
}

uint32_t mining_pool_stats_lifetime_blocks(void)
{
    return S_TABLE->lifetime_blocks_total;
}

int64_t mining_pool_stats_lifetime_last_block_ts(void)
{
    return S_TABLE->lifetime_last_block_ts;
}

const mining_pool_stat_t *mining_pool_stats_slot(int idx)
{
    if (idx < 0 || idx >= MINING_POOL_STATS_MAX) return NULL;
    return &S_TABLE->slots[idx];
}

void mining_pool_stats_set_block_topic(bb_event_topic_t topic)
{
    s_block_topic = topic;
}

bb_event_topic_t mining_pool_stats_get_block_topic(void)
{
    return s_block_topic;
}

#ifndef ESP_PLATFORM
void mining_pool_stats_reset_for_test(void)
{
    memset(&s_table, 0, sizeof(s_table));
    s_host_clock          = 1;
    s_injected_lifetime_blocks = UINT32_MAX;
    memset(s_injected_slots, 0, sizeof(s_injected_slots));
    memset(s_injected_slot_valid, 0, sizeof(s_injected_slot_valid));
}

void mining_pool_stats_sanitize_slot_for_test(mining_pool_stat_t *sl, int idx)
{
    s_sanitize_slot(idx, sl);
}

void mining_pool_stats_sanitize_lifetime_for_test(void)
{
    s_sanitize_lifetime(S_TABLE);
}

void mining_pool_stats_set_lifetime_blocks_for_test(uint32_t v)
{
    S_TABLE->lifetime_blocks_total = v;
}

void mining_pool_stats_inject_slot_for_test(int idx, const mining_pool_stat_t *sl)
{
    if (idx < 0 || idx >= MINING_POOL_STATS_MAX || !sl) return;
    s_injected_slots[idx] = *sl;
    s_injected_slot_valid[idx] = true;
}

void mining_pool_stats_inject_lifetime_blocks_for_test(uint32_t v)
{
    s_injected_lifetime_blocks = v;
}
#endif
