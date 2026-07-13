// mining_wire — the mining engine's ENTIRE delivery-facing surface (TA-561).
//
// Descriptor + gather live in their own TU so mining.c (the hot-loop file)
// never needs to know bb_serialize exists. Registration of mining_desc +
// mining_gather with a delivery path happens in the composition root
// (src/main.c), never here and never in mining.c.
//
// Compiles on both host and ESP-IDF; no platform-specific code except the
// ESP_PLATFORM live-state read, which is guarded.

#include "mining.h"

#include <stddef.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bb_timer.h"
#else
// Host-only mirror of the fields mining_gather() reads. There is no live
// mining_stats/FreeRTOS mutex on host (the mining task never runs there);
// tests seed this directly via mining_gather_set_snapshot_for_test().
static mining_snap_t s_host_snap;
static bool          s_host_snap_seeded = false;
#endif

// ---------------------------------------------------------------------------
// Descriptor (SSOT) -- declarative offsets + types, format-agnostic.
// ---------------------------------------------------------------------------

static const bb_serialize_field_t s_mining_fields[] = {
    { .key = "hashrate_hs",     .type = BB_TYPE_U64, .offset = offsetof(mining_snap_t, hashrate_hs) },
    { .key = "hashrate_1m_hs",  .type = BB_TYPE_F64, .offset = offsetof(mining_snap_t, hashrate_1m_hs) },
    { .key = "hashrate_10m_hs", .type = BB_TYPE_F64, .offset = offsetof(mining_snap_t, hashrate_10m_hs) },
    { .key = "hashrate_1h_hs",  .type = BB_TYPE_F64, .offset = offsetof(mining_snap_t, hashrate_1h_hs) },
    { .key = "accepted",        .type = BB_TYPE_U64, .offset = offsetof(mining_snap_t, accepted) },
    { .key = "rejected",        .type = BB_TYPE_U64, .offset = offsetof(mining_snap_t, rejected) },
    { .key = "best_diff",       .type = BB_TYPE_F64, .offset = offsetof(mining_snap_t, best_diff) },
    { .key = "hashes",          .type = BB_TYPE_U64, .offset = offsetof(mining_snap_t, hashes) },
    { .key = "blocks_found",    .type = BB_TYPE_U64, .offset = offsetof(mining_snap_t, blocks_found) },
    { .key = "temp_c",          .type = BB_TYPE_F64, .offset = offsetof(mining_snap_t, temp_c) },
    { .key = "uptime_s",        .type = BB_TYPE_I64, .offset = offsetof(mining_snap_t, uptime_s) },
    { .key = "sha_self_test_failed", .type = BB_TYPE_BOOL, .offset = offsetof(mining_snap_t, sha_self_test_failed) },
};

const bb_serialize_desc_t mining_desc = {
    .type_name = "mining",
    .fields    = s_mining_fields,
    .n_fields  = sizeof(s_mining_fields) / sizeof(s_mining_fields[0]),
    .snap_size = sizeof(mining_snap_t),
};

// ---------------------------------------------------------------------------
// Pure gather -- NO I/O, NO allocation, NO delivery knowledge.
// ---------------------------------------------------------------------------

void mining_gather(void *ctx, void *out)
{
    (void)ctx;
    mining_snap_t *snap = (mining_snap_t *)out;
    if (!snap) return;
    memset(snap, 0, sizeof(*snap));

#ifdef ESP_PLATFORM
    if (!mining_stats.mutex || xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        // Mutex not yet created (mining_stats_init() not called) or busy --
        // return a zeroed-but-valid snapshot rather than blocking.
        snap->sha_self_test_failed = mining_sha_self_test_failed();
        return;
    }
    snap->hashrate_hs     = (uint64_t)mining_stats.hw_hashrate;
    snap->hashrate_1m_hs  = mining_stats.hashrate_1m;
    snap->hashrate_10m_hs = mining_stats.hashrate_10m;
    snap->hashrate_1h_hs  = mining_stats.hashrate_1h;
    snap->accepted        = mining_stats.session.shares;
    snap->rejected         = mining_stats.session.rejected;
    snap->best_diff        = mining_stats.session.best_diff;
    snap->hashes            = mining_stats.session.hashes;
    snap->blocks_found      = mining_stats.session.blocks_found;
    snap->temp_c            = (double)mining_stats.temp_c;
    int64_t start_us = mining_stats.session.start_us;
    xSemaphoreGive(mining_stats.mutex);

    snap->uptime_s = (start_us > 0)
        ? (int64_t)(((int64_t)bb_timer_now_us() - start_us) / 1000000)
        : 0;
    snap->sha_self_test_failed = mining_sha_self_test_failed();
#else
    if (s_host_snap_seeded) {
        *snap = s_host_snap;
    }
#endif
}

#ifndef ESP_PLATFORM
void mining_gather_set_snapshot_for_test(const mining_snap_t *in)
{
    if (!in) {
        s_host_snap_seeded = false;
        memset(&s_host_snap, 0, sizeof(s_host_snap));
        return;
    }
    s_host_snap = *in;
    s_host_snap_seeded = true;
}
#endif
