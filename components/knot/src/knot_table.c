#include <string.h>

// On ESP, include bb_mdns.h to get bb_mdns_txt_t definition before knot.h
#ifdef ESP_PLATFORM
#include "bb_mdns.h"
#endif

#include "knot.h"

// Pure host-testable functions for peer table management.

/// Insert or update a peer by instance_name.
/// Returns slot index on success, or -1 if table full and no existing slot.
int knot_table_upsert(knot_peer_t *table, size_t cap, const knot_peer_t *peer) {
    if (!table || !peer || !peer->instance_name[0]) {
        return -1;
    }

    // Search for existing entry by instance_name
    int empty_slot = -1;
    for (size_t i = 0; i < cap; i++) {
        if (table[i].instance_name[0] == '\0') {
            if (empty_slot == -1) {
                empty_slot = i;
            }
        } else if (strcmp(table[i].instance_name, peer->instance_name) == 0) {
            // Found existing entry, update it
            table[i] = *peer;
            return i;
        }
    }

    // No existing entry found; use empty slot if available
    if (empty_slot != -1) {
        table[empty_slot] = *peer;
        return empty_slot;
    }

    // Table full, no empty slot
    return -1;
}

/// Remove a peer by instance_name.
/// Marks the slot empty (instance_name[0] = '\0').
/// Returns slot index removed, or -1 if not found.
int knot_table_remove(knot_peer_t *table, size_t cap, const char *instance_name) {
    if (!table || !instance_name || !instance_name[0]) {
        return -1;
    }

    for (size_t i = 0; i < cap; i++) {
        if (table[i].instance_name[0] != '\0' &&
            strcmp(table[i].instance_name, instance_name) == 0) {
            table[i].instance_name[0] = '\0';
            return i;
        }
    }

    return -1;
}

/// Prune entries older than now_us - ttl_us.
/// Marks matching slots empty.
/// Returns count of entries pruned.
size_t knot_table_prune(knot_peer_t *table, size_t cap, int64_t now_us, int64_t ttl_us) {
    if (!table || ttl_us < 0) {
        return 0;
    }

    size_t pruned = 0;
    for (size_t i = 0; i < cap; i++) {
        if (table[i].instance_name[0] != '\0' &&
            table[i].last_seen_us < (now_us - ttl_us)) {
            table[i].instance_name[0] = '\0';
            pruned++;
        }
    }

    return pruned;
}

/// Copy non-empty entries from table into out buffer.
/// Returns count copied.
size_t knot_table_snapshot(const knot_peer_t *table, size_t cap, knot_peer_t *out, size_t out_cap) {
    if (!table || !out) {
        return 0;
    }

    size_t copied = 0;
    for (size_t i = 0; i < cap && copied < out_cap; i++) {
        if (table[i].instance_name[0] != '\0') {
            out[copied] = table[i];
            copied++;
        }
    }

    return copied;
}

/// Populate peer fields from TXT records.
/// Walks txt array, populates worker/board/version/state from matching keys.
/// Unknown keys are ignored.
void knot_table_apply_txt(knot_peer_t *peer, const bb_mdns_txt_t *txt, size_t txt_count) {
    if (!peer || !txt) {
        return;
    }

    for (size_t i = 0; i < txt_count; i++) {
        if (!txt[i].key || !txt[i].value) {
            continue;
        }

        if (strcmp(txt[i].key, "worker") == 0) {
            strncpy(peer->worker, txt[i].value, sizeof(peer->worker) - 1);
            peer->worker[sizeof(peer->worker) - 1] = '\0';
        } else if (strcmp(txt[i].key, "board") == 0) {
            strncpy(peer->board, txt[i].value, sizeof(peer->board) - 1);
            peer->board[sizeof(peer->board) - 1] = '\0';
        } else if (strcmp(txt[i].key, "version") == 0) {
            strncpy(peer->version, txt[i].value, sizeof(peer->version) - 1);
            peer->version[sizeof(peer->version) - 1] = '\0';
        } else if (strcmp(txt[i].key, "state") == 0) {
            strncpy(peer->state, txt[i].value, sizeof(peer->state) - 1);
            peer->state[sizeof(peer->state) - 1] = '\0';
        }
    }
}
