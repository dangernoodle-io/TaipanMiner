#include "bb_mdns.h"  // Include first so bb_mdns_txt_t is defined before knot.h
#include "knot.h"
#include "bb_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <mdns.h>
#include <string.h>

#define KNOT_PEER_COUNT 32
#define KNOT_REQUERY_INTERVAL_US (45ULL * 1000 * 1000)

static const char *TAG = "knot";

static knot_peer_t g_peer_table[KNOT_PEER_COUNT];
static SemaphoreHandle_t g_mutex = NULL;
static bool g_initialized = false;
static esp_timer_handle_t g_requery_timer = NULL;

// Forward declarations
static void on_peer_discovered(const bb_mdns_peer_t *peer, void *ctx);

/* No local TTL prune. IDF's mdns_browse only delivers notifier callbacks on
 * change events (new / updated / removed), so a stable peer goes silent on
 * the wire toward us even though IDF still considers it alive. We trust
 * IDF's own TTL bookkeeping: when a peer's announced TTL expires without a
 * refresh, IDF fires the notifier with ttl=0 which routes through
 * on_peer_removed below. last_seen_us still tracks the most-recent
 * notification so the UI can show "seen N seconds ago" — that number will
 * grow for stable peers, which is the correct semantic. */

static void on_requery_tick(void *ctx) {
    // Snapshot current peer table under mutex
    knot_peer_t snapshot[KNOT_PEER_COUNT];
    size_t snapshot_count = 0;

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        snapshot_count = knot_table_snapshot(g_peer_table, KNOT_PEER_COUNT, snapshot, KNOT_PEER_COUNT);
        xSemaphoreGive(g_mutex);
    }

    // For each peer, query TXT records
    for (size_t i = 0; i < snapshot_count; i++) {
        const knot_peer_t *peer = &snapshot[i];

        mdns_result_t *results = NULL;
        esp_err_t err = mdns_query_txt(peer->instance_name, "_taipanminer", "_tcp", 1500, &results);

        if (err == ESP_OK && results) {
            // Build bb_mdns_peer_t from result, preserving hostname/ip4 from snapshot
            bb_mdns_peer_t new_peer = {
                .instance_name = results->instance_name,
                .hostname = results->hostname ? results->hostname : peer->hostname,
                .ip4 = peer->ip4,  // Preserve from snapshot; results.addr is mdns_ip_addr_t linked list
                .port = results->port,
                .txt = (bb_mdns_txt_t *)results->txt,
                .txt_count = results->txt_count,
            };

            // Call on_peer_discovered to flow through existing upsert + apply_txt path
            on_peer_discovered(&new_peer, NULL);

            mdns_query_results_free(results);
        } else {
            // Log misses at debug level only
            bb_log_d(TAG, "mdns_query_txt %s failed: %s", peer->instance_name, esp_err_to_name(err));
        }
    }
}

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

    // Create and start periodic mDNS re-query timer
    const esp_timer_create_args_t requery_args = {
        .callback = on_requery_tick,
        .arg = NULL,
        .name = "knot_requery",
    };
    esp_err_t timer_err = esp_timer_create(&requery_args, &g_requery_timer);
    if (timer_err != ESP_OK) {
        bb_log_e(TAG, "failed to create requery timer: %s", esp_err_to_name(timer_err));
        bb_mdns_browse_stop("_taipanminer", "_tcp");
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
        return -1;
    }

    timer_err = esp_timer_start_periodic(g_requery_timer, KNOT_REQUERY_INTERVAL_US);
    if (timer_err != ESP_OK) {
        bb_log_e(TAG, "failed to start requery timer: %s", esp_err_to_name(timer_err));
        esp_timer_delete(g_requery_timer);
        g_requery_timer = NULL;
        bb_mdns_browse_stop("_taipanminer", "_tcp");
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
        return -1;
    }

    g_initialized = true;
    bb_log_i(TAG, "knot initialized");
    return 0;
}

void knot_set_self(const char *instance_name,
                   const char *hostname,
                   const char *ip4,
                   const char *worker,
                   const char *board,
                   const char *version,
                   const char *state) {
    if (!instance_name || !instance_name[0]) {
        return;
    }

    knot_peer_t self = {0};
    strncpy(self.instance_name, instance_name, sizeof(self.instance_name) - 1);
    if (hostname) strncpy(self.hostname, hostname, sizeof(self.hostname) - 1);
    if (ip4)      strncpy(self.ip4,      ip4,      sizeof(self.ip4) - 1);
    self.port = 80;
    if (worker)  strncpy(self.worker,  worker,  sizeof(self.worker) - 1);
    if (board)   strncpy(self.board,   board,   sizeof(self.board) - 1);
    if (version) strncpy(self.version, version, sizeof(self.version) - 1);
    if (state)   strncpy(self.state,   state,   sizeof(self.state) - 1);
    self.last_seen_us = esp_timer_get_time();

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        knot_table_upsert(g_peer_table, KNOT_PEER_COUNT, &self);
        xSemaphoreGive(g_mutex);
    }
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
