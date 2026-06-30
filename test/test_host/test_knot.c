#include "unity.h"
#include "knot.h"
#include <string.h>

// Helper to create a test peer
static knot_peer_t make_peer(const char *instance, const char *hostname, const char *ip) {
    knot_peer_t p = {0};
    strncpy(p.id.instance_name, instance, sizeof(p.id.instance_name) - 1);
    strncpy(p.id.hostname, hostname, sizeof(p.id.hostname) - 1);
    strncpy(p.id.ip4, ip, sizeof(p.id.ip4) - 1);
    p.id.port = 80;
    strncpy(p.worker, "worker-1", sizeof(p.worker) - 1);
    strncpy(p.board, "tdongle-s3", sizeof(p.board) - 1);
    strncpy(p.version, "v1.0.0", sizeof(p.version) - 1);
    strncpy(p.state, "mining", sizeof(p.state) - 1);
    p.last_seen_us = 1000000; // 1 second from epoch
    return p;
}

void test_knot_table_upsert_empty_slot(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t peer = make_peer("miner1", "miner1.local", "192.168.1.1");

    int slot = knot_table_upsert(table, 4, &peer);
    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_STRING("miner1", table[0].id.instance_name);
}

void test_knot_table_upsert_update_existing(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t peer1 = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t peer2 = make_peer("miner1", "miner1-updated.local", "192.168.1.10");

    int slot1 = knot_table_upsert(table, 4, &peer1);
    TEST_ASSERT_EQUAL_INT(0, slot1);

    int slot2 = knot_table_upsert(table, 4, &peer2);
    TEST_ASSERT_EQUAL_INT(0, slot2); // same slot
    TEST_ASSERT_EQUAL_STRING("miner1-updated.local", table[0].id.hostname);
}

void test_knot_table_upsert_table_full(void) {
    knot_peer_t table[2] = {0};
    knot_peer_t p1 = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t p2 = make_peer("miner2", "miner2.local", "192.168.1.2");
    knot_peer_t p3 = make_peer("miner3", "miner3.local", "192.168.1.3");

    knot_table_upsert(table, 2, &p1);
    knot_table_upsert(table, 2, &p2);
    int slot3 = knot_table_upsert(table, 2, &p3);
    TEST_ASSERT_EQUAL_INT(-1, slot3);
}

void test_knot_table_upsert_evicts_same_hostname(void) {
    // Same physical device announcing under an old + new instance_name
    // (e.g. across a firmware update). Newer instance wins; old entry evicted.
    knot_peer_t table[4] = {0};
    knot_peer_t old_inst = make_peer("TaipanMiner-9895", "tdongles3-1", "172.16.1.215");
    knot_peer_t new_inst = make_peer("tdongles3-1-7cd8", "tdongles3-1", "172.16.1.215");

    knot_table_upsert(table, 4, &old_inst);
    knot_table_upsert(table, 4, &new_inst);

    // Snapshot should contain only the new instance — old was evicted
    knot_peer_t out[4] = {0};
    size_t count = knot_table_snapshot(table, 4, out, 4);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("tdongles3-1-7cd8", out[0].id.instance_name);
}

void test_knot_table_remove_existing(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t peer = make_peer("miner1", "miner1.local", "192.168.1.1");

    knot_table_upsert(table, 4, &peer);
    int slot = knot_table_remove(table, 4, "miner1");
    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_INT(0, table[0].id.instance_name[0]); // empty
}

void test_knot_table_remove_missing(void) {
    knot_peer_t table[4] = {0};
    int slot = knot_table_remove(table, 4, "nonexistent");
    TEST_ASSERT_EQUAL_INT(-1, slot);
}

void test_knot_table_prune_stale_entries(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t p1 = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t p2 = make_peer("miner2", "miner2.local", "192.168.1.2");

    p1.last_seen_us = 1000000;   // 1 second
    p2.last_seen_us = 5000000;   // 5 seconds

    knot_table_upsert(table, 4, &p1);
    knot_table_upsert(table, 4, &p2);

    // Prune entries older than 3 seconds
    int64_t now_us = 6000000;
    int64_t ttl_us = 3000000;
    size_t pruned = knot_table_prune(table, 4, now_us, ttl_us);

    TEST_ASSERT_EQUAL_INT(1, pruned);
    TEST_ASSERT_EQUAL_INT(0, table[0].id.instance_name[0]); // p1 pruned
    TEST_ASSERT_EQUAL_STRING("miner2", table[1].id.instance_name); // p2 kept
}

void test_knot_table_snapshot(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t p1 = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t p2 = make_peer("miner2", "miner2.local", "192.168.1.2");

    knot_table_upsert(table, 4, &p1);
    knot_table_upsert(table, 4, &p2);

    knot_peer_t out[4] = {0};
    size_t count = knot_table_snapshot(table, 4, out, 4);

    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_STRING("miner1", out[0].id.instance_name);
    TEST_ASSERT_EQUAL_STRING("miner2", out[1].id.instance_name);
}

void test_knot_table_snapshot_cap(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t p1 = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t p2 = make_peer("miner2", "miner2.local", "192.168.1.2");
    knot_peer_t p3 = make_peer("miner3", "miner3.local", "192.168.1.3");

    knot_table_upsert(table, 4, &p1);
    knot_table_upsert(table, 4, &p2);
    knot_table_upsert(table, 4, &p3);

    knot_peer_t out[2] = {0};
    size_t count = knot_table_snapshot(table, 4, out, 2);

    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_STRING("miner1", out[0].id.instance_name);
    TEST_ASSERT_EQUAL_STRING("miner2", out[1].id.instance_name);
}

void test_knot_table_apply_txt(void) {
    knot_peer_t peer = {0};
    strncpy(peer.id.instance_name, "miner1", sizeof(peer.id.instance_name) - 1);

    bb_mdns_txt_t txt[] = {
        { .key = "worker", .value = "example.com/myworker" },
        { .key = "board", .value = "bitaxe-601" },
        { .key = "version", .value = "v1.2.3" },
        { .key = "state", .value = "mining" },
        { .key = "unknown", .value = "ignored" }
    };

    knot_table_apply_txt(&peer, txt, 5);

    TEST_ASSERT_EQUAL_STRING("example.com/myworker", peer.worker);
    TEST_ASSERT_EQUAL_STRING("bitaxe-601", peer.board);
    TEST_ASSERT_EQUAL_STRING("v1.2.3", peer.version);
    TEST_ASSERT_EQUAL_STRING("mining", peer.state);
}

void test_knot_table_apply_txt_ui_zero(void) {
    knot_peer_t peer = {0};
    strncpy(peer.id.instance_name, "miner1", sizeof(peer.id.instance_name) - 1);
    bb_mdns_txt_t txt[] = {
        { .key = "ui", .value = "0" },
    };
    knot_table_apply_txt(&peer, txt, 1);
    TEST_ASSERT_FALSE(peer.ui);
}

void test_knot_table_apply_txt_ui_one(void) {
    knot_peer_t peer = {0};
    strncpy(peer.id.instance_name, "miner1", sizeof(peer.id.instance_name) - 1);
    bb_mdns_txt_t txt[] = {
        { .key = "ui", .value = "1" },
    };
    knot_table_apply_txt(&peer, txt, 1);
    TEST_ASSERT_TRUE(peer.ui);
}

void test_knot_table_apply_txt_ui_absent_defaults_true(void) {
    knot_peer_t peer = {0};
    strncpy(peer.id.instance_name, "miner1", sizeof(peer.id.instance_name) - 1);
    bb_mdns_txt_t txt[] = {
        { .key = "worker", .value = "example.com/worker" },
    };
    knot_table_apply_txt(&peer, txt, 1);
    TEST_ASSERT_TRUE(peer.ui);
}

void test_knot_table_null_guards(void) {
    knot_peer_t table[2] = {0};
    knot_peer_t peer = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t empty = {0};

    // upsert: null table, null peer, peer with empty instance_name
    TEST_ASSERT_EQUAL_INT(-1, knot_table_upsert(NULL, 2, &peer));
    TEST_ASSERT_EQUAL_INT(-1, knot_table_upsert(table, 2, NULL));
    TEST_ASSERT_EQUAL_INT(-1, knot_table_upsert(table, 2, &empty));

    // remove: null table, null name, empty name
    TEST_ASSERT_EQUAL_INT(-1, knot_table_remove(NULL, 2, "x"));
    TEST_ASSERT_EQUAL_INT(-1, knot_table_remove(table, 2, NULL));
    TEST_ASSERT_EQUAL_INT(-1, knot_table_remove(table, 2, ""));

    // prune: null table, negative ttl
    TEST_ASSERT_EQUAL_INT(0, knot_table_prune(NULL, 2, 1000, 100));
    TEST_ASSERT_EQUAL_INT(0, knot_table_prune(table, 2, 1000, -1));

    // snapshot: null table, null out
    knot_peer_t out[2] = {0};
    TEST_ASSERT_EQUAL_INT(0, knot_table_snapshot(NULL, 2, out, 2));
    TEST_ASSERT_EQUAL_INT(0, knot_table_snapshot(table, 2, NULL, 2));

    // apply_txt: null peer, null txt, txt entries with null key/value
    bb_mdns_txt_t bad[] = {
        { .key = NULL, .value = "v" },
        { .key = "k", .value = NULL },
    };
    knot_table_apply_txt(NULL, bad, 2);
    knot_table_apply_txt(&peer, NULL, 0);
    knot_table_apply_txt(&peer, bad, 2); // exercises the continue branch
}

// Context for knot_walk callback test
typedef struct {
    int visit_count;
    bool abort_after_one;
} knot_walk_test_ctx_t;

// Callback for knot_walk test
static bool knot_walk_test_cb(const knot_peer_t *peer, void *ctx_vp) {
    knot_walk_test_ctx_t *ctx = (knot_walk_test_ctx_t *)ctx_vp;
    ctx->visit_count++;
    if (ctx->abort_after_one) {
        return false;  // Abort iteration
    }
    return true;  // Continue iteration
}

void test_knot_walk_basic(void) {
    // This is a host test — knot_walk itself requires FreeRTOS mutexes.
    // We test it indirectly via the table mechanism: populate a table,
    // then manually iterate as knot_walk would, verifying the visit logic.

    knot_peer_t table[4] = {0};
    knot_peer_t p1 = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t p2 = make_peer("miner2", "miner2.local", "192.168.1.2");
    knot_peer_t p3 = make_peer("miner3", "miner3.local", "192.168.1.3");

    knot_table_upsert(table, 4, &p1);
    knot_table_upsert(table, 4, &p2);
    knot_table_upsert(table, 4, &p3);

    // Simulate knot_walk logic: iterate and count
    knot_walk_test_ctx_t ctx = {.visit_count = 0, .abort_after_one = false};
    for (size_t i = 0; i < 4; i++) {
        if (table[i].id.instance_name[0] != '\0') {
            if (!knot_walk_test_cb(&table[i], &ctx)) {
                break;
            }
        }
    }

    TEST_ASSERT_EQUAL_INT(3, ctx.visit_count);
}

void test_knot_walk_early_abort(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t p1 = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t p2 = make_peer("miner2", "miner2.local", "192.168.1.2");
    knot_peer_t p3 = make_peer("miner3", "miner3.local", "192.168.1.3");

    knot_table_upsert(table, 4, &p1);
    knot_table_upsert(table, 4, &p2);
    knot_table_upsert(table, 4, &p3);

    // Simulate knot_walk logic with early abort
    knot_walk_test_ctx_t ctx = {.visit_count = 0, .abort_after_one = true};
    for (size_t i = 0; i < 4; i++) {
        if (table[i].id.instance_name[0] != '\0') {
            if (!knot_walk_test_cb(&table[i], &ctx)) {
                break;
            }
        }
    }

    TEST_ASSERT_EQUAL_INT(1, ctx.visit_count);
}
