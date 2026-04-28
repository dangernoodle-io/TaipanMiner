/*
 * test_routes_json.c — golden tests for pure route JSON builders (TA-291).
 *
 * Each builder is tested with a realistic populated snapshot (happy path) and
 * at least one edge-case snapshot. JSON ordering is insertion-order (cJSON),
 * so literal string comparison is valid.
 */
#include "unity.h"
#include "routes_json.h"
#include "bb_json.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ============================================================================
 * Helpers
 * ========================================================================= */

static char *serialize_and_free(bb_json_t root)
{
    char *s = bb_json_serialize(root);
    bb_json_free(root);
    return s;
}

/* ============================================================================
 * /api/stats — no-ASIC path (host build never defines ASIC_CHIP)
 * ========================================================================= */

void test_stats_happy_path(void)
{
    stats_snapshot_t s = {0};
    s.hw_rate    = 223000.0;
    s.hw_ema     = 215000.0;
    s.temp_c     = 42.5f;
    s.hw_shares  = 7;
    s.session_shares  = 5;
    s.session_rejected = 1;
    s.session_rejected_job_not_found  = 1;
    s.session_rejected_low_difficulty = 0;
    s.session_rejected_duplicate      = 0;
    s.session_rejected_stale_prevhash = 0;
    s.session_rejected_other          = 0;
    s.session_rejected_other_last_code = -1;
    s.last_share_us    = 10000000LL;   /* 10 s ago */
    s.session_start_us = 5000000LL;    /* 5 s ago */
    s.best_diff        = 131072.0;
    s.lifetime_shares  = 42;
    s.now_us           = 15000000LL;   /* "now" */

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    /* uptime_s = (15000000 - 5000000) / 1000000 = 10
     * last_share_ago_s = (15000000 - 10000000) / 1000000 = 5 */
    TEST_ASSERT_EQUAL_STRING(
        "{\"hashrate\":223000,\"hashrate_avg\":215000,\"temp_c\":42.5,"
        "\"shares\":7,\"session_shares\":5,\"session_rejected\":1,"
        "\"rejected\":{\"total\":1,\"job_not_found\":1,\"low_difficulty\":0,"
        "\"duplicate\":0,\"stale_prevhash\":0,\"other\":0,\"other_last_code\":-1},"
        "\"last_share_ago_s\":5,\"lifetime_shares\":42,\"best_diff\":131072,"
        "\"uptime_s\":10,\"expected_ghs\":0.000223}",
        json);
    bb_json_free_str(json);
}

void test_stats_zeroed(void)
{
    /* All-zero snapshot: uptime=0, last_share_ago_s=-1, expected_ghs=0.000223 */
    stats_snapshot_t s = {0};
    s.session_rejected_other_last_code = -1;

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"hashrate\":0,\"hashrate_avg\":0,\"temp_c\":0,"
        "\"shares\":0,\"session_shares\":0,\"session_rejected\":0,"
        "\"rejected\":{\"total\":0,\"job_not_found\":0,\"low_difficulty\":0,"
        "\"duplicate\":0,\"stale_prevhash\":0,\"other\":0,\"other_last_code\":-1},"
        "\"last_share_ago_s\":-1,\"lifetime_shares\":0,\"best_diff\":0,"
        "\"uptime_s\":0,\"expected_ghs\":0.000223}",
        json);
    bb_json_free_str(json);
}

void test_stats_no_share_yet(void)
{
    /* last_share_us=0 → last_share_ago_s should be -1 */
    stats_snapshot_t s = {0};
    s.session_rejected_other_last_code = -1;
    s.last_share_us    = 0;
    s.session_start_us = 1000000LL;
    s.now_us           = 61000000LL;  /* 60 s uptime */

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = serialize_and_free(root);

    /* uptime_s = 60, last_share_ago_s = -1 */
    const char *expected =
        "{\"hashrate\":0,\"hashrate_avg\":0,\"temp_c\":0,"
        "\"shares\":0,\"session_shares\":0,\"session_rejected\":0,"
        "\"rejected\":{\"total\":0,\"job_not_found\":0,\"low_difficulty\":0,"
        "\"duplicate\":0,\"stale_prevhash\":0,\"other\":0,\"other_last_code\":-1},"
        "\"last_share_ago_s\":-1,\"lifetime_shares\":0,\"best_diff\":0,"
        "\"uptime_s\":60,\"expected_ghs\":0.000223}";
    TEST_ASSERT_EQUAL_STRING(expected, json);
    bb_json_free_str(json);
}

/* ============================================================================
 * /api/pool
 * ========================================================================= */

void test_pool_disconnected(void)
{
    pool_snapshot_t s = {0};
    strncpy(s.host, "pool.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    strncpy(s.worker, "test-worker", sizeof(s.worker) - 1);
    strncpy(s.wallet, "tb1qtest000", sizeof(s.wallet) - 1);
    s.connected         = false;
    s.has_session_start = false;
    s.current_difficulty = 512.0;
    /* extranonce1_len=0, has_notify=false */

    bb_json_t root = bb_json_obj_new();
    build_pool_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"host\":\"pool.example.com\",\"port\":3333,"
        "\"worker\":\"test-worker\",\"wallet\":\"tb1qtest000\","
        "\"connected\":false,\"session_start_ago_s\":null,"
        "\"current_difficulty\":512,"
        "\"extranonce1\":null,\"extranonce2_size\":null,\"version_mask\":null,"
        "\"notify\":null}",
        json);
    bb_json_free_str(json);
}

void test_pool_connected_with_notify(void)
{
    pool_snapshot_t s = {0};
    strncpy(s.host, "pool.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    strncpy(s.worker, "test-worker", sizeof(s.worker) - 1);
    strncpy(s.wallet, "tb1qtest000", sizeof(s.wallet) - 1);
    s.connected          = true;
    s.has_session_start  = true;
    s.session_start_ago_s = 120;
    s.current_difficulty = 8192.0;

    /* extranonce: 2 bytes = "aabb" */
    s.extranonce1[0] = 0xaa;
    s.extranonce1[1] = 0xbb;
    s.extranonce1_len  = 2;
    s.extranonce2_size = 4;
    s.version_mask     = 0x1fffe000;

    /* notify fields */
    s.has_notify = true;
    strncpy(s.job_id, "abc123", sizeof(s.job_id) - 1);
    memset(s.prevhash, 0x00, 32);
    s.coinb1[0] = 0x01; s.coinb1_len = 1;
    s.coinb2[0] = 0x02; s.coinb2_len = 1;
    s.merkle_count = 0;
    s.version  = 0x20000000;
    s.nbits    = 0x1703a30c;
    s.ntime    = 0x65a1b2c3;
    s.clean_jobs = true;

    bb_json_t root = bb_json_obj_new();
    build_pool_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"host\":\"pool.example.com\",\"port\":3333,"
        "\"worker\":\"test-worker\",\"wallet\":\"tb1qtest000\","
        "\"connected\":true,\"session_start_ago_s\":120,"
        "\"current_difficulty\":8192,"
        "\"extranonce1\":\"aabb\",\"extranonce2_size\":4,\"version_mask\":\"1fffe000\","
        "\"notify\":{"
        "\"job_id\":\"abc123\","
        "\"prev_hash\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"coinb1\":\"01\","
        "\"coinb2\":\"02\","
        "\"merkle_branches\":[],"
        "\"version\":\"20000000\","
        "\"nbits\":\"1703a30c\","
        "\"ntime\":\"65a1b2c3\","
        "\"clean_jobs\":true}}",
        json);
    bb_json_free_str(json);
}

void test_pool_version_mask_zero(void)
{
    /* version_mask=0 → "version_mask":null */
    pool_snapshot_t s = {0};
    strncpy(s.host, "pool.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    s.connected         = true;
    s.has_session_start = true;
    s.session_start_ago_s = 5;
    s.current_difficulty = 512.0;
    s.extranonce1[0] = 0xde; s.extranonce1[1] = 0xad;
    s.extranonce1_len  = 2;
    s.extranonce2_size = 4;
    s.version_mask     = 0;   /* no version rolling */
    s.has_notify       = false;

    bb_json_t root = bb_json_obj_new();
    build_pool_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"host\":\"pool.example.com\",\"port\":3333,"
        "\"worker\":\"\",\"wallet\":\"\","
        "\"connected\":true,\"session_start_ago_s\":5,"
        "\"current_difficulty\":512,"
        "\"extranonce1\":\"dead\",\"extranonce2_size\":4,\"version_mask\":null,"
        "\"notify\":null}",
        json);
    bb_json_free_str(json);
}

/* ============================================================================
 * /api/diag/asic
 * ========================================================================= */

void test_diag_asic_empty(void)
{
    diag_asic_snapshot_t s = {0};

    bb_json_t root = bb_json_obj_new();
    build_diag_asic_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING("{\"recent_drops\":[]}", json);
    bb_json_free_str(json);
}

void test_diag_asic_three_events(void)
{
    diag_asic_snapshot_t s = {0};
    s.now_us  = 10000000ULL;  /* 10 s */
    s.n_drops = 3;

    /* event 0: total kind, 5 s ago */
    s.drops[0].ts_us      = 5000000ULL;
    s.drops[0].chip_idx   = 0;
    s.drops[0].kind       = ROUTES_JSON_DROP_KIND_TOTAL;
    s.drops[0].domain_idx = 0;
    s.drops[0].asic_addr  = 0;
    s.drops[0].ghs        = 480.5f;
    s.drops[0].delta      = 10;
    s.drops[0].elapsed_s  = 60.0f;

    /* event 1: error kind, 2 s ago */
    s.drops[1].ts_us      = 8000000ULL;
    s.drops[1].chip_idx   = 1;
    s.drops[1].kind       = ROUTES_JSON_DROP_KIND_ERROR;
    s.drops[1].domain_idx = 2;
    s.drops[1].asic_addr  = 3;
    s.drops[1].ghs        = 12.0f;
    s.drops[1].delta      = 5;
    s.drops[1].elapsed_s  = 30.0f;

    /* event 2: domain kind, 1 s ago */
    s.drops[2].ts_us      = 9000000ULL;
    s.drops[2].chip_idx   = 0;
    s.drops[2].kind       = ROUTES_JSON_DROP_KIND_DOMAIN;
    s.drops[2].domain_idx = 1;
    s.drops[2].asic_addr  = 7;
    s.drops[2].ghs        = 120.0f;
    s.drops[2].delta      = 3;
    s.drops[2].elapsed_s  = 10.0f;

    bb_json_t root = bb_json_obj_new();
    build_diag_asic_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"recent_drops\":["
        "{\"ts_ago_s\":5,\"chip\":0,\"kind\":\"total\",\"domain\":0,"
        "\"addr\":0,\"ghs\":480.5,\"delta\":10,\"elapsed_s\":60},"
        "{\"ts_ago_s\":2,\"chip\":1,\"kind\":\"error\",\"domain\":2,"
        "\"addr\":3,\"ghs\":12,\"delta\":5,\"elapsed_s\":30},"
        "{\"ts_ago_s\":1,\"chip\":0,\"kind\":\"domain\",\"domain\":1,"
        "\"addr\":7,\"ghs\":120,\"delta\":3,\"elapsed_s\":10}"
        "]}",
        json);
    bb_json_free_str(json);
}

void test_diag_asic_future_ts_clamps_to_zero(void)
{
    /* ts_us > now_us: age_us should clamp to 0 → ts_ago_s = 0 */
    diag_asic_snapshot_t s = {0};
    s.now_us  = 1000000ULL;
    s.n_drops = 1;
    s.drops[0].ts_us      = 9999999ULL;  /* future */
    s.drops[0].chip_idx   = 0;
    s.drops[0].kind       = ROUTES_JSON_DROP_KIND_TOTAL;
    s.drops[0].ghs        = 1.0f;
    s.drops[0].elapsed_s  = 0.0f;

    bb_json_t root = bb_json_obj_new();
    build_diag_asic_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"recent_drops\":["
        "{\"ts_ago_s\":0,\"chip\":0,\"kind\":\"total\",\"domain\":0,"
        "\"addr\":0,\"ghs\":1,\"delta\":0,\"elapsed_s\":0}"
        "]}",
        json);
    bb_json_free_str(json);
}

/* ============================================================================
 * /api/knot
 * ========================================================================= */

void test_knot_empty(void)
{
    knot_snapshot_t s = {0};
    s.now_us  = 0;
    s.n_peers = 0;

    bb_json_t root = bb_json_arr_new();
    build_knot_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING("[]", json);
    bb_json_free_str(json);
}

void test_knot_two_peers(void)
{
    knot_snapshot_t s = {0};
    s.now_us  = 30000000LL;  /* 30 s */
    s.n_peers = 2;

    strncpy(s.peers[0].instance, "taipan-alpha._taipan._tcp.local", sizeof(s.peers[0].instance) - 1);
    strncpy(s.peers[0].hostname, "taipan-alpha", sizeof(s.peers[0].hostname) - 1);
    strncpy(s.peers[0].ip,       "192.168.1.10", sizeof(s.peers[0].ip) - 1);
    strncpy(s.peers[0].worker,   "alpha-worker", sizeof(s.peers[0].worker) - 1);
    strncpy(s.peers[0].board,    "bitaxe-601",   sizeof(s.peers[0].board)   - 1);
    strncpy(s.peers[0].version,  "1.2.3",        sizeof(s.peers[0].version) - 1);
    strncpy(s.peers[0].state,    "mining",       sizeof(s.peers[0].state)   - 1);
    s.peers[0].last_seen_us = 25000000LL;  /* 5 s ago */

    strncpy(s.peers[1].instance, "taipan-beta._taipan._tcp.local", sizeof(s.peers[1].instance) - 1);
    strncpy(s.peers[1].hostname, "taipan-beta", sizeof(s.peers[1].hostname) - 1);
    strncpy(s.peers[1].ip,       "192.168.1.11", sizeof(s.peers[1].ip) - 1);
    strncpy(s.peers[1].worker,   "beta-worker",  sizeof(s.peers[1].worker)  - 1);
    strncpy(s.peers[1].board,    "bitaxe-403",   sizeof(s.peers[1].board)   - 1);
    strncpy(s.peers[1].version,  "1.2.0",        sizeof(s.peers[1].version) - 1);
    strncpy(s.peers[1].state,    "ota",          sizeof(s.peers[1].state)   - 1);
    s.peers[1].last_seen_us = 20000000LL;  /* 10 s ago */

    bb_json_t root = bb_json_arr_new();
    build_knot_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "[{\"instance\":\"taipan-alpha._taipan._tcp.local\","
        "\"hostname\":\"taipan-alpha\",\"ip\":\"192.168.1.10\","
        "\"worker\":\"alpha-worker\",\"board\":\"bitaxe-601\","
        "\"version\":\"1.2.3\",\"state\":\"mining\",\"seen_ago_s\":5},"
        "{\"instance\":\"taipan-beta._taipan._tcp.local\","
        "\"hostname\":\"taipan-beta\",\"ip\":\"192.168.1.11\","
        "\"worker\":\"beta-worker\",\"board\":\"bitaxe-403\","
        "\"version\":\"1.2.0\",\"state\":\"ota\",\"seen_ago_s\":10}]",
        json);
    bb_json_free_str(json);
}

/* ============================================================================
 * /api/settings GET
 * ========================================================================= */

void test_settings_happy_path(void)
{
    settings_snapshot_t s = {0};
    strncpy(s.pool_host, "pool.example.com", sizeof(s.pool_host) - 1);
    s.pool_port = 3333;
    strncpy(s.wallet,    "tb1qtest000",     sizeof(s.wallet)    - 1);
    strncpy(s.worker,    "acme-worker",     sizeof(s.worker)    - 1);
    strncpy(s.pool_pass, "x",              sizeof(s.pool_pass) - 1);
    strncpy(s.hostname,  "acme-miner",     sizeof(s.hostname)  - 1);
    s.display_en     = true;
    s.ota_skip_check = false;

    bb_json_t root = bb_json_obj_new();
    build_settings_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"pool_host\":\"pool.example.com\",\"pool_port\":3333,"
        "\"wallet\":\"tb1qtest000\",\"worker\":\"acme-worker\","
        "\"pool_pass\":\"x\",\"hostname\":\"acme-miner\","
        "\"display_en\":true,\"ota_skip_check\":false}",
        json);
    bb_json_free_str(json);
}

void test_settings_empty_optional_fields(void)
{
    /* pool_pass and hostname may be empty strings */
    settings_snapshot_t s = {0};
    strncpy(s.pool_host, "stratum.example.com", sizeof(s.pool_host) - 1);
    s.pool_port = 4444;
    strncpy(s.wallet, "tb1qzero000", sizeof(s.wallet) - 1);
    strncpy(s.worker, "test-user",   sizeof(s.worker) - 1);
    /* pool_pass and hostname left as empty strings */
    s.display_en     = false;
    s.ota_skip_check = true;

    bb_json_t root = bb_json_obj_new();
    build_settings_json(&s, root);
    char *json = serialize_and_free(root);

    TEST_ASSERT_EQUAL_STRING(
        "{\"pool_host\":\"stratum.example.com\",\"pool_port\":4444,"
        "\"wallet\":\"tb1qzero000\",\"worker\":\"test-user\","
        "\"pool_pass\":\"\",\"hostname\":\"\","
        "\"display_en\":false,\"ota_skip_check\":true}",
        json);
    bb_json_free_str(json);
}

/* ============================================================================
 * /api/power and /api/fan are ASIC_CHIP only — not included in host build.
 * The test file compiles without ASIC_CHIP, so these builders are absent.
 * Their logic is covered by build_settings_json/build_diag_asic_json patterns.
 * ========================================================================= */
