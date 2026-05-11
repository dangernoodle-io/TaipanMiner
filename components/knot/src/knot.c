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
/* Tracked separately so on_peer_removed never evicts the self entry — when
 * mdns hostname-conflict resolution withdraws the local record, the browse
 * fires a "removed" callback for our own instance and we'd otherwise lose
 * the self row from /api/knot. We also stash the full self peer so we can
 * re-insert it after knot_table_upsert's dedupe-by-hostname pass evicts it
 * when another device advertises under the same hostname. */
static char g_self_instance[64] = {0};
static knot_peer_t g_self_peer = {0};
static bool g_self_set = false;

static void on_peer_discovered(const bb_mdns_peer_t *peer, void *ctx) {
    if (!peer || peer->instance_name[0] == '\0') {
        return;
    }

    knot_peer_t new_peer = {0};
    strncpy(new_peer.instance_name, peer->instance_name, sizeof(new_peer.instance_name) - 1);
    strncpy(new_peer.hostname,      peer->hostname,      sizeof(new_peer.hostname) - 1);
    strncpy(new_peer.ip4,           peer->ip4,           sizeof(new_peer.ip4) - 1);
    new_peer.port = peer->port;
    new_peer.last_seen_us = esp_timer_get_time();

    // Apply TXT records (worker, board, version, state) using helper
    knot_table_apply_txt(&new_peer, peer->txt, peer->txt_count);

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        int slot = knot_table_upsert(g_peer_table, KNOT_PEER_COUNT, &new_peer);
        /* upsert's dedupe-by-hostname pass evicts entries that share a
         * hostname but differ in instance — that's correct for stale OTA
         * advertisements but it also strips our self entry whenever
         * another device announces under the same configured hostname.
         * Re-insert the cached self peer so /api/knot keeps showing it. */
        if (g_self_set &&
            strcmp(new_peer.instance_name, g_self_instance) != 0 &&
            strcmp(new_peer.hostname, g_self_peer.hostname) == 0) {
            knot_table_upsert(g_peer_table, KNOT_PEER_COUNT, &g_self_peer);
        }
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

    /* Never evict the self entry on hostname conflict-resolution churn. */
    if (g_self_instance[0] != '\0' && strcmp(instance_name, g_self_instance) == 0) {
        bb_log_d(TAG, "ignoring remove for self: %s", instance_name);
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

void knot_deinit(void) {
    if (!g_initialized) return;
    bb_mdns_browse_stop("_taipanminer", "_tcp");
    vTaskDelay(pdMS_TO_TICKS(50));  // let any inflight callback complete
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memset(g_peer_table, 0, sizeof(g_peer_table));
        g_self_instance[0] = '\0';
        memset(&g_self_peer, 0, sizeof(g_self_peer));
        g_self_set = false;
        xSemaphoreGive(g_mutex);
        vSemaphoreDelete(g_mutex);
        g_mutex = NULL;
    }
    g_initialized = false;
    bb_log_i(TAG, "knot deinitialized");
}

bool knot_is_running(void) {
    return g_initialized;
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
        int slot = knot_table_upsert(g_peer_table, KNOT_PEER_COUNT, &self);
        strncpy(g_self_instance, instance_name, sizeof(g_self_instance) - 1);
        g_self_instance[sizeof(g_self_instance) - 1] = '\0';
        g_self_peer = self;
        g_self_set = true;
        xSemaphoreGive(g_mutex);
        bb_log_d(TAG, "set_self instance=%s slot=%d", instance_name, slot);
    } else {
        bb_log_e(TAG, "set_self mutex timeout (g_mutex=%p)", g_mutex);
    }
}

size_t knot_snapshot(knot_peer_t *out, size_t cap) {
    if (!out || cap == 0 || !g_mutex) {
        return 0;
    }

    size_t copied = 0;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        copied = knot_table_snapshot(g_peer_table, KNOT_PEER_COUNT, out, cap);
        xSemaphoreGive(g_mutex);
    }

    return copied;
}

void knot_walk(bool (*cb)(const knot_peer_t *peer, void *ctx), void *ctx) {
    if (!cb || !g_mutex) {
        return;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (size_t i = 0; i < KNOT_PEER_COUNT; i++) {
            if (g_peer_table[i].instance_name[0] != '\0') {
                if (!cb(&g_peer_table[i], ctx)) {
                    break;
                }
            }
        }
        xSemaphoreGive(g_mutex);
    }
}
