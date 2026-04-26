#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     instance_name[64];
    char     hostname[64];
    char     ip4[16];
    uint16_t port;
    char     worker[33];
    char     board[24];
    char     version[24];
    char     state[16];
    int64_t  last_seen_us;
} knot_peer_t;

/// Pure table operations (host-testable, no ESP wiring)

// bb_mdns_txt_t: forward declare for function signature
// On ESP: included from bb_mdns.h (before this header in include order)
// On host tests: must be defined locally by the caller
#ifndef ESP_PLATFORM
// Host test version
typedef struct {
    char *key;
    char *value;
} bb_mdns_txt_t;
#endif

int knot_table_upsert(knot_peer_t *table, size_t cap, const knot_peer_t *peer);
int knot_table_remove(knot_peer_t *table, size_t cap, const char *instance_name);
size_t knot_table_prune(knot_peer_t *table, size_t cap, int64_t now_us, int64_t ttl_us);
size_t knot_table_snapshot(const knot_peer_t *table, size_t cap, knot_peer_t *out, size_t out_cap);
void knot_table_apply_txt(knot_peer_t *peer, const bb_mdns_txt_t *txt, size_t txt_count);

/// Initialize knot peer table, mDNS browse, and TTL pruning timer.
/// Must be called after bb_mdns_init(). Safe to call multiple times (idempotent).
int knot_init(void);

/// Snapshot current peer table.
/// Copies non-empty entries to out buffer, returns count copied.
/// Safe to call concurrently from any task.
size_t knot_snapshot(knot_peer_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
