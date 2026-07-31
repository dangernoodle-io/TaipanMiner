/*
 * mining_pool_stats.c — per-pool lifetime stats (8 slots, LRU eviction).
 *
 * In-RAM only — see mining_pool_stats.h for why NVS persistence is
 * deferred (the now-removed legacy NVS helper API, B1-708).
 *
 * Table access:
 *   ESP_PLATFORM — mining_stats.pool_stats-equivalent global, guarded by
 *                  the mining_stats mutex (callers hold it via the record_*
 *                  API; this file takes it internally for reset/find_or_alloc).
 *   Host/native  — file-static table (no FreeRTOS).
 */

#include "mining_pool_stats.h"
#include "mining.h"
#include "bb_log.h"
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
#include "freertos/semphr.h"
#include "bb_timer.h"
#endif

static const char *TAG = "pool_stats";

static mining_pool_stats_t s_table;

/* -------------------------------------------------------------------------
 * Host-only test-injection state (compiled out on device)
 * ---------------------------------------------------------------------- */
#ifndef ESP_PLATFORM
/* Monotonically increasing timestamp counter. Reset by reset_for_test(). */
static int64_t s_host_clock = 1;
#endif

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
    memset(&s_table, 0, sizeof(s_table));
}

void mining_pool_stats_reset(void)
{
#ifdef ESP_PLATFORM
    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&s_table, 0, sizeof(s_table));
        xSemaphoreGive(mining_stats.mutex);
    } else {
        bb_log_w(TAG, "reset: mutex timeout");
    }
#else
    memset(&s_table, 0, sizeof(s_table));
#endif
}

mining_pool_stat_t *mining_pool_stats_find_or_alloc(const char *host, uint16_t port)
{
    mining_pool_stats_t *ps = &s_table;

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
}

void mining_pool_stats_record_hashes(mining_pool_stat_t *slot, uint64_t n)
{
    if (!slot) return;
    slot->hashes += n;
}

void mining_pool_stats_record_block(mining_pool_stat_t *slot, int64_t now_ts)
{
    if (!slot) return;
    slot->blocks_found++;
    slot->last_block_ts = now_ts;
    /* Also bump the device-lifetime counter (never evicted). */
    s_table.lifetime_blocks_total++;
    s_table.lifetime_last_block_ts = now_ts;
}

uint32_t mining_pool_stats_lifetime_blocks(void)
{
    return s_table.lifetime_blocks_total;
}

int64_t mining_pool_stats_lifetime_last_block_ts(void)
{
    return s_table.lifetime_last_block_ts;
}

const mining_pool_stat_t *mining_pool_stats_slot(int idx)
{
    if (idx < 0 || idx >= MINING_POOL_STATS_MAX) return NULL;
    return &s_table.slots[idx];
}

#ifndef ESP_PLATFORM
void mining_pool_stats_reset_for_test(void)
{
    memset(&s_table, 0, sizeof(s_table));
    s_host_clock = 1;
}
#endif
