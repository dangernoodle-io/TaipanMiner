/*
 * mining_pool_stats.c — per-pool lifetime stats (8 slots, LRU eviction).
 *
 * NVS storage: per-field keys ps<N>_<field> (e.g. ps0_host, ps1_port).
 * No blob API in bb_nv; per-field keys used throughout.
 *
 * Table access:
 *   ESP_PLATFORM — mining_stats.pool_stats (shared struct, guarded by mutex)
 *   Host/native  — file-static s_table (no FreeRTOS)
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

#ifdef ESP_PLATFORM
#include "freertos/semphr.h"
#include "esp_timer.h"
#endif

/* block.found event topic handle; set by mining_pool_stats_set_block_topic(). */
static bb_event_topic_t s_block_topic = NULL;

static const char *TAG  = "pool_stats";
static const char *s_ns = "taipanminer";

/* On host builds the shared mining_stats struct does not exist; use a local table. */
#ifndef ESP_PLATFORM
static mining_pool_stats_t s_table;
#define S_TABLE (&s_table)
#else
#define S_TABLE (&mining_stats.pool_stats)
#endif

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

#ifndef ESP_PLATFORM
/* Monotonically increasing timestamp counter for host builds.
 * Reset by mining_pool_stats_reset_for_test() so LRU tests are deterministic. */
static int64_t s_host_clock = 1;
#endif

static int64_t s_now_us(void)
{
#ifdef ESP_PLATFORM
    return (int64_t)esp_timer_get_time();
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

    /* Load device-lifetime block counter (survives LRU slot eviction). */
    uint32_t lifetime_blocks = 0;
    bb_nv_get_u32(s_ns, "ps_life_blocks", &lifetime_blocks, 0);
    S_TABLE->lifetime_blocks_total = lifetime_blocks;

    /* Load device-lifetime "last block found" wall-clock timestamp. */
    {
        uint32_t hi = 0, lo = 0;
        bb_nv_get_u32(s_ns, "ps_lblk_hi", &hi, 0);
        bb_nv_get_u32(s_ns, "ps_lblk_lo", &lo, 0);
        S_TABLE->lifetime_last_block_ts = (int64_t)(((uint64_t)hi << 32) | lo);
    }

    /* Load per-slot data from NVS. */
    for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
        mining_pool_stat_t sl;
        memset(&sl, 0, sizeof(sl));
        s_load_slot(i, &sl);
        if (sl.host[0] != '\0' || sl.last_seen_us != 0) {
            S_TABLE->slots[i] = sl;
            bb_log_i(TAG, "slot %d: %s:%" PRIu16 " shr=%" PRIu32,
                     i, sl.host, sl.port, sl.shares);
        }
    }
    if (lifetime_blocks > 0) {
        bb_log_i(TAG, "lifetime blocks: %" PRIu32, lifetime_blocks);
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

/* Return the index of `slot` within the live table, or -1 if not found. */
static int s_slot_index(const mining_pool_stat_t *slot)
{
    if (!slot) return -1;
    const mining_pool_stat_t *base = S_TABLE->slots;
    if (slot < base || slot >= base + MINING_POOL_STATS_MAX) return -1;
    return (int)(slot - base);
}

void mining_pool_stats_save(void)
{
#ifdef ESP_PLATFORM
    mining_pool_stats_t snap;
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        snap = mining_stats.pool_stats;
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
    s_host_clock = 1;
}
#endif
