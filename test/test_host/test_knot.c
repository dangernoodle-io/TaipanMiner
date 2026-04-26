#include "unity.h"
#include "knot.h"
#include <string.h>

// Helper to create a test peer
static knot_peer_t make_peer(const char *instance, const char *hostname, const char *ip) {
    knot_peer_t p = {0};
    strncpy(p.instance_name, instance, sizeof(p.instance_name) - 1);
    strncpy(p.hostname, hostname, sizeof(p.hostname) - 1);
    strncpy(p.ip4, ip, sizeof(p.ip4) - 1);
    p.port = 80;
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
    TEST_ASSERT_EQUAL_STRING("miner1", table[0].instance_name);
}

void test_knot_table_upsert_update_existing(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t peer1 = make_peer("miner1", "miner1.local", "192.168.1.1");
    knot_peer_t peer2 = make_peer("miner1", "miner1-updated.local", "192.168.1.10");

    int slot1 = knot_table_upsert(table, 4, &peer1);
    TEST_ASSERT_EQUAL_INT(0, slot1);

    int slot2 = knot_table_upsert(table, 4, &peer2);
    TEST_ASSERT_EQUAL_INT(0, slot2); // same slot
    TEST_ASSERT_EQUAL_STRING("miner1-updated.local", table[0].hostname);
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

void test_knot_table_remove_existing(void) {
    knot_peer_t table[4] = {0};
    knot_peer_t peer = make_peer("miner1", "miner1.local", "192.168.1.1");

    knot_table_upsert(table, 4, &peer);
    int slot = knot_table_remove(table, 4, "miner1");
    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_INT(0, table[0].instance_name[0]); // empty
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
    TEST_ASSERT_EQUAL_INT(0, table[0].instance_name[0]); // p1 pruned
    TEST_ASSERT_EQUAL_STRING("miner2", table[1].instance_name); // p2 kept
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
    TEST_ASSERT_EQUAL_STRING("miner1", out[0].instance_name);
    TEST_ASSERT_EQUAL_STRING("miner2", out[1].instance_name);
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
    TEST_ASSERT_EQUAL_STRING("miner1", out[0].instance_name);
    TEST_ASSERT_EQUAL_STRING("miner2", out[1].instance_name);
}

void test_knot_table_apply_txt(void) {
    knot_peer_t peer = {0};
    strncpy(peer.instance_name, "miner1", sizeof(peer.instance_name) - 1);

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
