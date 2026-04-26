#include "bb_mdns.h"  // Include first so bb_mdns_txt_t is defined before knot.h
#include "knot.h"
#include "bb_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <string.h>

#define KNOT_PEER_COUNT 32

static const char *TAG = "knot";

static knot_peer_t g_peer_table[KNOT_PEER_COUNT];
static SemaphoreHandle_t g_mutex = NULL;
static bool g_initialized = false;

/* No local TTL prune. IDF's mdns_browse only delivers notifier callbacks on
 * change events (new / updated / removed), so a stable peer goes silent on
 * the wire toward us even though IDF still considers it alive. We trust
 * IDF's own TTL bookkeeping: when a peer's announced TTL expires without a
 * refresh, IDF fires the notifier with ttl=0 which routes through
 * on_peer_removed below. last_seen_us still tracks the most-recent
 * notification so the UI can show "seen N seconds ago" — that number will
 * grow for stable peers, which is the correct semantic. */

static void on_peer_discovered(const bb_mdns_peer_t *peer, void *ctx) {
    if (!peer || !peer->instance_name) {
        return;
    }

    knot_peer_t new_peer = {0};
    strncpy(new_peer.instance_name, peer->instance_name, sizeof(new_peer.instance_name) - 1);
    strncpy(new_peer.hostname, peer->hostname ? peer->hostname : "", sizeof(new_peer.hostname) - 1);
    strncpy(new_peer.ip4, peer->ip4 ? peer->ip4 : "", sizeof(new_peer.ip4) - 1);
    new_peer.port = peer->port;
    new_peer.last_seen_us = esp_timer_get_time();

    // Apply TXT records (worker, board, version, state) using helper
    knot_table_apply_txt(&new_peer, peer->txt, peer->txt_count);

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        int slot = knot_table_upsert(g_peer_table, KNOT_PEER_COUNT, &new_peer);
        xSemaphoreGive(g_mutex);
        if (slot >= 0) {
            bb_log_d(TAG, "peer upserted: %s (slot %d)", peer->instance_name, slot);
        } else {
            bb_log_w(TAG, "peer table full, failed to upsert %s", peer->instance_name);
        }
    }
}

static void on_peer_removed(const char *instance_name, void *ctx) {
    if (!instance_name) {
        return;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        int slot = knot_table_remove(g_peer_table, KNOT_PEER_COUNT, instance_name);
        xSemaphoreGive(g_mutex);
        if (slot >= 0) {
            bb_log_i(TAG, "peer removed: %s (slot %d)", instance_name, slot);
        }
    }
}

int knot_init(void) {
    if (g_initialized) {
        return 0;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        bb_log_e(TAG, "failed to create mutex");
        return -1;
    }

    bb_err_t err = bb_mdns_browse_start("_taipanminer", "_tcp",
                                        on_peer_discovered,
                                        on_peer_removed,
                                        NULL);
    if (err != BB_OK) {
        bb_log_e(TAG, "failed to start mdns browse: %d", err);
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
        return -1;
    }

    g_initialized = true;
    bb_log_i(TAG, "knot initialized");
    return 0;
}

size_t knot_snapshot(knot_peer_t *out, size_t cap) {
    if (!out || cap == 0) {
        return 0;
    }

    size_t copied = 0;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        copied = knot_table_snapshot(g_peer_table, KNOT_PEER_COUNT, out, cap);
        xSemaphoreGive(g_mutex);
    }

    return copied;
}
