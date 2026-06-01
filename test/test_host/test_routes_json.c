/*
 * test_routes_json.c — golden tests for route JSON emitters (TA-291).
 *
 * All emitters use the bb_http capture harness: emit_* functions write into a
 * streaming JSON object, and the captured body is verified directly.
 * This validates the same code path that runs in the production handler.
 */
#include "unity.h"
#include "routes_json.h"
#include "bb_json.h"
#include "bb_http.h"
#include "bb_http_host.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ============================================================================
 * Helpers — streaming capture (used by emit_stats_json / emit_settings_json)
 * ========================================================================= */

/* Portable strdup — POSIX strdup is not declared under -std=c99 (glibc hides it
 * without _POSIX_C_SOURCE), and an implicit decl truncates the 64-bit pointer
 * return on LP64. */
static char *dupstr(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* Capture emit_pool_json output into a heap string.
 * Caller must free() the returned pointer. */
static char *capture_pool(const pool_snapshot_t *snap,
                           const pool_stat_snapshot_t *stats,
                           size_t stats_count)
{
    bb_http_request_t *req;
    bb_http_host_capture_begin(&req);
    bb_http_json_obj_stream_t obj;
    bb_http_resp_json_obj_begin(req, &obj);
    emit_pool_json(&obj, snap, stats, stats_count);
    bb_http_resp_json_obj_end(&obj);
    bb_http_host_capture_t cap;
    bb_http_host_capture_end(req, &cap);
    char *copy = dupstr(cap.body);
    bb_http_host_capture_free(&cap);
    return copy;
}

/* Capture emit_diag_asic_json output into a heap string.
 * Caller must free() the returned pointer. */
static char *capture_diag_asic(const diag_asic_snapshot_t *snap)
{
    bb_http_request_t *req;
    bb_http_host_capture_begin(&req);
    bb_http_json_obj_stream_t obj;
    bb_http_resp_json_obj_begin(req, &obj);
    emit_diag_asic_json(&obj, snap);
    bb_http_resp_json_obj_end(&obj);
    bb_http_host_capture_t cap;
    bb_http_host_capture_end(req, &cap);
    char *copy = dupstr(cap.body);
    bb_http_host_capture_free(&cap);
    return copy;
}

/* Capture emit_stats_json output into a heap string.
 * Caller must free() the returned pointer. */
static char *capture_stats(const stats_snapshot_t *snap)
{
    bb_http_request_t *req;
    bb_http_host_capture_begin(&req);
    bb_http_json_obj_stream_t obj;
    bb_http_resp_json_obj_begin(req, &obj);
    emit_stats_json(&obj, snap);
    bb_http_resp_json_obj_end(&obj);
    bb_http_host_capture_t cap;
    bb_http_host_capture_end(req, &cap);
    char *copy = dupstr(cap.body);
    bb_http_host_capture_free(&cap);
    return copy;
}

/* Capture emit_settings_json output into a heap string.
 * Caller must free() the returned pointer. */
static char *capture_settings(const settings_snapshot_t *snap)
{
    bb_http_request_t *req;
    bb_http_host_capture_begin(&req);
    bb_http_json_obj_stream_t obj;
    bb_http_resp_json_obj_begin(req, &obj);
    emit_settings_json(&obj, snap);
    bb_http_resp_json_obj_end(&obj);
    bb_http_host_capture_t cap;
    bb_http_host_capture_end(req, &cap);
    char *copy = dupstr(cap.body);
    bb_http_host_capture_free(&cap);
    return copy;
}

/* ============================================================================
 * /api/stats — both ASIC and non-ASIC paths tested via capture harness
 * ========================================================================= */

#ifdef ASIC_CHIP
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
    s.expected_ghs     = -1.0;         /* unavailable */
    s.now_us           = 15000000LL;   /* "now" */

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);

    /* uptime_s = (15000000 - 5000000) / 1000000 = 10
     * last_share_ago_s = (15000000 - 10000000) / 1000000 = 5
     * ASIC fields all zero: expected_ghs=null (freq_cfg=0, not >0),
     * freq_configured/effective=0 (>=0), total_valid=false → nulls, chips=[] */
    TEST_ASSERT_EQUAL_STRING(
        "{\"hashrate\":223000,\"hashrate_avg\":215000,\"temp_c\":42.5,"
        "\"shares\":7,\"session_shares\":5,\"session_rejected\":1,\"session_blocks_found\":0,\"session_best_diff_ts\":0,\"session_last_block_ts\":0,"
        "\"rejected\":{\"total\":1,\"job_not_found\":1,\"low_difficulty\":0,"
        "\"duplicate\":0,\"stale_prevhash\":0,\"other\":0,\"other_last_code\":-1},"
        "\"last_share_ago_s\":5,\"best_diff\":131072,"
        "\"uptime_s\":10,"
        "\"expected_ghs\":null,"
        "\"asic_hashrate\":0,\"asic_hashrate_avg\":0,\"asic_shares\":0,\"asic_temp_c\":0,"
        "\"asic_freq_configured_mhz\":0,\"asic_freq_effective_mhz\":0,"
        "\"asic_small_cores\":0,\"asic_count\":0,"
        "\"asic_total_ghs\":null,\"asic_hw_error_pct\":null,"
        "\"asic_total_ghs_1m\":null,\"asic_total_ghs_10m\":null,\"asic_total_ghs_1h\":null,"
        "\"asic_hw_error_pct_1m\":null,\"asic_hw_error_pct_10m\":null,\"asic_hw_error_pct_1h\":null,"
        "\"pool_effective_hashrate\":0,\"asic_chips\":[]}",
        json);
    free(json);
}

void test_stats_zeroed(void)
{
    /* All-zero snapshot: uptime=0, last_share_ago_s=-1, expected_ghs=null (unavailable) */
    stats_snapshot_t s = {0};
    s.session_rejected_other_last_code = -1;
    s.expected_ghs = -1.0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"hashrate\":0,\"hashrate_avg\":0,\"temp_c\":0,"
        "\"shares\":0,\"session_shares\":0,\"session_rejected\":0,\"session_blocks_found\":0,\"session_best_diff_ts\":0,\"session_last_block_ts\":0,"
        "\"rejected\":{\"total\":0,\"job_not_found\":0,\"low_difficulty\":0,"
        "\"duplicate\":0,\"stale_prevhash\":0,\"other\":0,\"other_last_code\":-1},"
        "\"last_share_ago_s\":-1,\"best_diff\":0,"
        "\"uptime_s\":0,"
        "\"expected_ghs\":null,"
        "\"asic_hashrate\":0,\"asic_hashrate_avg\":0,\"asic_shares\":0,\"asic_temp_c\":0,"
        "\"asic_freq_configured_mhz\":0,\"asic_freq_effective_mhz\":0,"
        "\"asic_small_cores\":0,\"asic_count\":0,"
        "\"asic_total_ghs\":null,\"asic_hw_error_pct\":null,"
        "\"asic_total_ghs_1m\":null,\"asic_total_ghs_10m\":null,\"asic_total_ghs_1h\":null,"
        "\"asic_hw_error_pct_1m\":null,\"asic_hw_error_pct_10m\":null,\"asic_hw_error_pct_1h\":null,"
        "\"pool_effective_hashrate\":0,\"asic_chips\":[]}",
        json);
    free(json);
}

void test_stats_no_share_yet(void)
{
    /* last_share_us=0 → last_share_ago_s should be -1 */
    stats_snapshot_t s = {0};
    s.session_rejected_other_last_code = -1;
    s.expected_ghs = -1.0;
    s.last_share_us    = 0;
    s.session_start_us = 1000000LL;
    s.now_us           = 61000000LL;  /* 60 s uptime */
    s.pool_effective_hashrate = -1.0; /* no shares yet → null */

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);

    /* uptime_s = 60, last_share_ago_s = -1
     * ASIC fields all zero (zero-init snapshot) */
    const char *expected =
        "{\"hashrate\":0,\"hashrate_avg\":0,\"temp_c\":0,"
        "\"shares\":0,\"session_shares\":0,\"session_rejected\":0,\"session_blocks_found\":0,\"session_best_diff_ts\":0,\"session_last_block_ts\":0,"
        "\"rejected\":{\"total\":0,\"job_not_found\":0,\"low_difficulty\":0,"
        "\"duplicate\":0,\"stale_prevhash\":0,\"other\":0,\"other_last_code\":-1},"
        "\"last_share_ago_s\":-1,\"best_diff\":0,"
        "\"uptime_s\":60,"
        "\"expected_ghs\":null,"
        "\"asic_hashrate\":0,\"asic_hashrate_avg\":0,\"asic_shares\":0,\"asic_temp_c\":0,"
        "\"asic_freq_configured_mhz\":0,\"asic_freq_effective_mhz\":0,"
        "\"asic_small_cores\":0,\"asic_count\":0,"
        "\"asic_total_ghs\":null,\"asic_hw_error_pct\":null,"
        "\"asic_total_ghs_1m\":null,\"asic_total_ghs_10m\":null,\"asic_total_ghs_1h\":null,"
        "\"asic_hw_error_pct_1m\":null,\"asic_hw_error_pct_10m\":null,\"asic_hw_error_pct_1h\":null,"
        "\"pool_effective_hashrate\":null,\"asic_chips\":[]}";
    TEST_ASSERT_EQUAL_STRING(expected, json);
    free(json);
}
#endif /* ASIC_CHIP */

#ifndef ASIC_CHIP
void test_stats_non_asic_happy_path(void)
{
    /* Non-ASIC path: hashrate_1m/10m/1h + pool_effective_hashrate + hw_error_pct fields */
    stats_snapshot_t s = {0};
    s.hw_rate    = 223000.0;
    s.hw_ema     = 215000.0;
    s.temp_c     = 42.5f;
    s.hw_shares  = 7;
    s.session_shares  = 5;
    s.session_rejected = 0;
    s.session_rejected_other_last_code = -1;
    s.last_share_us    = 10000000LL;
    s.session_start_us = 5000000LL;
    s.best_diff        = 131072.0;
    s.expected_ghs     = 500.0;
    s.now_us           = 15000000LL;
    s.hashrate_1m             = 220000.0;
    s.hashrate_10m            = 218000.0;
    s.hashrate_1h             = 215000.0;
    s.pool_effective_hashrate = 210000.0;
    s.hw_error_pct_1m         = 0.1;
    s.hw_error_pct_10m        = 0.2;
    s.hw_error_pct_1h         = 0.15;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_1m\":220000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_10m\":218000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_1h\":215000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hashrate\":210000"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hw_error_pct_1m\":0.1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hw_error_pct_10m\":0.2"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hw_error_pct_1h\":0.15"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"expected_ghs\":500"));
    free(json);
}

void test_stats_non_asic_all_windows_null(void)
{
    /* Non-ASIC: all rolling windows set to sentinel (-1) → emit null */
    stats_snapshot_t s = {0};
    s.session_rejected_other_last_code = -1;
    s.expected_ghs            = -1.0;
    s.hashrate_1m             = -1.0;
    s.hashrate_10m            = -1.0;
    s.hashrate_1h             = -1.0;
    s.pool_effective_hashrate = -1.0;
    s.hw_error_pct_1m         = -1.0;
    s.hw_error_pct_10m        = -1.0;
    s.hw_error_pct_1h         = -1.0;

    char *json = capture_stats(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_1m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_10m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hashrate_1h\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"pool_effective_hashrate\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hw_error_pct_1m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hw_error_pct_10m\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"hw_error_pct_1h\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"expected_ghs\":null"));
    free(json);
}
#endif /* !ASIC_CHIP */

/* ============================================================================
 * /api/pool — capture-based tests (emit_pool_json)
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
    s.pool_effective_hashrate = -1.0;
    s.pool_effective_hashrate_1m = -1.0;
    s.pool_effective_hashrate_10m = -1.0;
    s.pool_effective_hashrate_1h = -1.0;
    s.latency_ms        = -1;
    s.active_pool_idx   = -1;

    char *json = capture_pool(&s, NULL, 0);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"host\":\"pool.example.com\",\"port\":3333,"
        "\"worker\":\"test-worker\",\"wallet\":\"tb1qtest000\","
        "\"connected\":false,\"session_start_ago_s\":null,"
        "\"current_difficulty\":512,\"pool_effective_hashrate\":null,\"pool_effective_hashrate_1m\":null,\"pool_effective_hashrate_10m\":null,\"pool_effective_hashrate_1h\":null,\"latency_ms\":null,"
        "\"extranonce1\":null,\"extranonce2_size\":null,\"version_mask\":null,"
        "\"notify\":null,\"active_pool_idx\":null,"
        "\"extranonce_subscribe_status\":\"off\",\"lifetime_blocks_total\":0,\"lifetime_last_block_ts\":0,"
        "\"configured\":{\"primary\":null,\"fallback\":null},"
        "\"stats\":[]}",
        json);
    free(json);
}

void test_pool_with_active_idx_and_configured_slots(void)
{
    /* Exercises: active_pool_idx >= 0, both configured[] slots, merkle branch */
    pool_snapshot_t s = {0};
    strncpy(s.host, "primary.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    strncpy(s.worker, "worker-a", sizeof(s.worker) - 1);
    strncpy(s.wallet, "tb1qa", sizeof(s.wallet) - 1);
    s.connected           = true;
    s.has_session_start   = true;
    s.session_start_ago_s = 5;
    s.current_difficulty  = 1024.0;
    s.latency_ms          = 10;
    s.active_pool_idx     = 1;
    s.extranonce1[0] = 0xab; s.extranonce1_len = 1;
    s.extranonce2_size = 4;
    s.version_mask     = 0;

    s.has_notify = true;
    strncpy(s.job_id, "j1", sizeof(s.job_id) - 1);
    memset(s.prevhash, 0xff, 32);
    s.coinb1[0] = 0xaa; s.coinb1_len = 1;
    s.coinb2[0] = 0xbb; s.coinb2_len = 1;
    memset(s.merkle_branches[0], 0x11, 32);
    s.merkle_count = 1;
    s.version  = 0x20000000;
    s.nbits    = 0x1703a30c;
    s.ntime    = 0x65a1b2c3;
    s.clean_jobs = false;

    s.configured[0].configured = true;
    strncpy(s.configured[0].host,   "primary.example.com",  sizeof(s.configured[0].host)   - 1);
    s.configured[0].port = 3333;
    strncpy(s.configured[0].worker, "worker-a",             sizeof(s.configured[0].worker) - 1);
    strncpy(s.configured[0].wallet, "tb1qa",                sizeof(s.configured[0].wallet) - 1);
    s.configured[0].extranonce_subscribe = true;
    s.configured[0].decode_coinbase      = false;

    s.configured[1].configured = true;
    strncpy(s.configured[1].host,   "fallback.example.com", sizeof(s.configured[1].host)   - 1);
    s.configured[1].port = 3334;
    strncpy(s.configured[1].worker, "worker-b",             sizeof(s.configured[1].worker) - 1);
    strncpy(s.configured[1].wallet, "tb1qb",                sizeof(s.configured[1].wallet) - 1);
    s.configured[1].extranonce_subscribe = false;
    s.configured[1].decode_coinbase      = true;

    char *json = capture_pool(&s, NULL, 0);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"active_pool_idx\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"primary\":{\"host\":\"primary.example.com\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"fallback\":{\"host\":\"fallback.example.com\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"merkle_branches\":[\"1111111111111111111111111111111111111111111111111111111111111111\"]"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"extranonce_subscribe\":true,\"decode_coinbase\":false"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"extranonce_subscribe\":false,\"decode_coinbase\":true"));
    free(json);
}

/* TA-306: cover extranonce_subscribe_status non-default arms */
static void exercise_subscribe_status(int status, const char *expected)
{
    pool_snapshot_t s = {0};
    strncpy(s.host, "pool.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    s.active_pool_idx = -1;
    s.extranonce_subscribe_status = status;

    char *json = capture_pool(&s, NULL, 0);
    TEST_ASSERT_NOT_NULL(json);

    char needle[64];
    snprintf(needle, sizeof(needle), "\"extranonce_subscribe_status\":\"%s\"", expected);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, needle), needle);
    free(json);
}

void test_pool_subscribe_status_pending(void)  { exercise_subscribe_status(1, "pending"); }
void test_pool_subscribe_status_active(void)   { exercise_subscribe_status(2, "active"); }
void test_pool_subscribe_status_rejected(void) { exercise_subscribe_status(3, "rejected"); }

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
    s.latency_ms         = 42;
    s.active_pool_idx    = -1;

    s.extranonce1[0] = 0xaa;
    s.extranonce1[1] = 0xbb;
    s.extranonce1_len  = 2;
    s.extranonce2_size = 4;
    s.version_mask     = 0x1fffe000;

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

    char *json = capture_pool(&s, NULL, 0);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"host\":\"pool.example.com\",\"port\":3333,"
        "\"worker\":\"test-worker\",\"wallet\":\"tb1qtest000\","
        "\"connected\":true,\"session_start_ago_s\":120,"
        "\"current_difficulty\":8192,\"pool_effective_hashrate\":0,\"pool_effective_hashrate_1m\":0,\"pool_effective_hashrate_10m\":0,\"pool_effective_hashrate_1h\":0,\"latency_ms\":42,"
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
        "\"clean_jobs\":true},"
        "\"active_pool_idx\":null,"
        "\"extranonce_subscribe_status\":\"off\",\"lifetime_blocks_total\":0,\"lifetime_last_block_ts\":0,"
        "\"configured\":{\"primary\":null,\"fallback\":null},"
        "\"stats\":[]}",
        json);
    free(json);
}

void test_pool_version_mask_zero(void)
{
    pool_snapshot_t s = {0};
    strncpy(s.host, "pool.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    s.connected         = true;
    s.has_session_start = true;
    s.session_start_ago_s = 5;
    s.current_difficulty = 512.0;
    s.latency_ms        = -1;
    s.active_pool_idx   = -1;
    s.extranonce1[0] = 0xde; s.extranonce1[1] = 0xad;
    s.extranonce1_len  = 2;
    s.extranonce2_size = 4;
    s.version_mask     = 0;
    s.has_notify       = false;

    char *json = capture_pool(&s, NULL, 0);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"host\":\"pool.example.com\",\"port\":3333,"
        "\"worker\":\"\",\"wallet\":\"\","
        "\"connected\":true,\"session_start_ago_s\":5,"
        "\"current_difficulty\":512,\"pool_effective_hashrate\":0,\"pool_effective_hashrate_1m\":0,\"pool_effective_hashrate_10m\":0,\"pool_effective_hashrate_1h\":0,\"latency_ms\":null,"
        "\"extranonce1\":\"dead\",\"extranonce2_size\":4,\"version_mask\":null,"
        "\"notify\":null,\"active_pool_idx\":null,"
        "\"extranonce_subscribe_status\":\"off\",\"lifetime_blocks_total\":0,\"lifetime_last_block_ts\":0,"
        "\"configured\":{\"primary\":null,\"fallback\":null},"
        "\"stats\":[]}",
        json);
    free(json);
}

void test_pool_latency_positive(void)
{
    pool_snapshot_t s = {0};
    strncpy(s.host, "pool.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    s.connected         = true;
    s.has_session_start = true;
    s.session_start_ago_s = 10;
    s.current_difficulty = 512.0;
    s.latency_ms        = 42;
    s.active_pool_idx   = -1;
    s.extranonce1_len   = 0;
    s.has_notify        = false;

    char *json = capture_pool(&s, NULL, 0);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"host\":\"pool.example.com\",\"port\":3333,"
        "\"worker\":\"\",\"wallet\":\"\","
        "\"connected\":true,\"session_start_ago_s\":10,"
        "\"current_difficulty\":512,\"pool_effective_hashrate\":0,\"pool_effective_hashrate_1m\":0,\"pool_effective_hashrate_10m\":0,\"pool_effective_hashrate_1h\":0,\"latency_ms\":42,"
        "\"extranonce1\":null,\"extranonce2_size\":null,\"version_mask\":null,"
        "\"notify\":null,\"active_pool_idx\":null,"
        "\"extranonce_subscribe_status\":\"off\",\"lifetime_blocks_total\":0,\"lifetime_last_block_ts\":0,"
        "\"configured\":{\"primary\":null,\"fallback\":null},"
        "\"stats\":[]}",
        json);
    free(json);
}

void test_pool_latency_negative(void)
{
    pool_snapshot_t s = {0};
    strncpy(s.host, "pool.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    s.connected         = true;
    s.has_session_start = true;
    s.session_start_ago_s = 5;
    s.current_difficulty = 512.0;
    s.latency_ms        = -1;
    s.active_pool_idx   = -1;
    s.extranonce1_len   = 0;
    s.has_notify        = false;

    char *json = capture_pool(&s, NULL, 0);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"host\":\"pool.example.com\",\"port\":3333,"
        "\"worker\":\"\",\"wallet\":\"\","
        "\"connected\":true,\"session_start_ago_s\":5,"
        "\"current_difficulty\":512,\"pool_effective_hashrate\":0,\"pool_effective_hashrate_1m\":0,\"pool_effective_hashrate_10m\":0,\"pool_effective_hashrate_1h\":0,\"latency_ms\":null,"
        "\"extranonce1\":null,\"extranonce2_size\":null,\"version_mask\":null,"
        "\"notify\":null,\"active_pool_idx\":null,"
        "\"extranonce_subscribe_status\":\"off\",\"lifetime_blocks_total\":0,\"lifetime_last_block_ts\":0,"
        "\"configured\":{\"primary\":null,\"fallback\":null},"
        "\"stats\":[]}",
        json);
    free(json);
}

/* ============================================================================
 * emit_pool_json stats[] array — folded into pool emit
 * ========================================================================= */

void test_emit_pool_stats_empty(void)
{
    /* empty stats array (NULL, 0) → "stats":[] at end of pool object */
    pool_snapshot_t s = {0};
    strncpy(s.host, "pool.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    s.active_pool_idx = -1;
    s.latency_ms = -1;

    char *json = capture_pool(&s, NULL, 0);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"stats\":[]"));
    free(json);
}

void test_emit_pool_stats_two_entries(void)
{
    pool_stat_snapshot_t arr[2] = {0};
    strncpy(arr[0].host, "pool-a.example.com", sizeof(arr[0].host) - 1);
    arr[0].port          = 3333;
    arr[0].shares        = 42;
    arr[0].hashes        = 1500000000000ULL;
    arr[0].best_diff     = 1234.5;
    arr[0].blocks_found  = 1;
    arr[0].last_seen_us  = 10000000LL;
    arr[0].best_diff_ts  = 1750000000LL;
    arr[0].last_block_ts = 1750000030LL;

    strncpy(arr[1].host, "pool-b.example.com", sizeof(arr[1].host) - 1);
    arr[1].port          = 3334;
    arr[1].shares        = 7;
    arr[1].hashes        = 500000000ULL;
    arr[1].best_diff     = 99.0;
    arr[1].blocks_found  = 0;
    arr[1].last_seen_us  = 5000000LL;
    arr[1].best_diff_ts  = 0;
    arr[1].last_block_ts = 0;

    pool_snapshot_t s = {0};
    strncpy(s.host, "pool-a.example.com", sizeof(s.host) - 1);
    s.port = 3333;
    s.active_pool_idx = -1;
    s.latency_ms = -1;

    char *json = capture_pool(&s, arr, 2);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json,
        "\"stats\":["
        "{\"host\":\"pool-a.example.com\",\"port\":3333,\"shares\":42,"
        "\"hashes\":1500000000000,\"best_diff\":1234.5,\"blocks_found\":1,"
        "\"last_seen_s\":10,\"best_diff_ts\":1750000000,\"last_block_ts\":1750000030},"
        "{\"host\":\"pool-b.example.com\",\"port\":3334,\"shares\":7,"
        "\"hashes\":500000000,\"best_diff\":99,\"blocks_found\":0,"
        "\"last_seen_s\":5,\"best_diff_ts\":0,\"last_block_ts\":0}"
        "]"));
    free(json);
}

/* ============================================================================
 * /api/diag/asic — capture-based tests (emit_diag_asic_json)
 * ========================================================================= */

void test_diag_asic_empty(void)
{
    diag_asic_snapshot_t s = {0};

    char *json = capture_diag_asic(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING("{\"recent_drops\":[]}", json);
    free(json);
}

void test_diag_asic_three_events(void)
{
    diag_asic_snapshot_t s = {0};
    s.now_us  = 10000000ULL;
    s.n_drops = 3;

    s.drops[0].ts_us      = 5000000ULL;
    s.drops[0].chip_idx   = 0;
    s.drops[0].kind       = ROUTES_JSON_DROP_KIND_TOTAL;
    s.drops[0].domain_idx = 0;
    s.drops[0].asic_addr  = 0;
    s.drops[0].ghs        = 480.5f;
    s.drops[0].delta      = 10;
    s.drops[0].elapsed_s  = 60.0f;

    s.drops[1].ts_us      = 8000000ULL;
    s.drops[1].chip_idx   = 1;
    s.drops[1].kind       = ROUTES_JSON_DROP_KIND_ERROR;
    s.drops[1].domain_idx = 2;
    s.drops[1].asic_addr  = 3;
    s.drops[1].ghs        = 12.0f;
    s.drops[1].delta      = 5;
    s.drops[1].elapsed_s  = 30.0f;

    s.drops[2].ts_us      = 9000000ULL;
    s.drops[2].chip_idx   = 0;
    s.drops[2].kind       = ROUTES_JSON_DROP_KIND_DOMAIN;
    s.drops[2].domain_idx = 1;
    s.drops[2].asic_addr  = 7;
    s.drops[2].ghs        = 120.0f;
    s.drops[2].delta      = 3;
    s.drops[2].elapsed_s  = 10.0f;

    char *json = capture_diag_asic(&s);
    TEST_ASSERT_NOT_NULL(json);

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
    free(json);
}

void test_diag_asic_future_ts_clamps_to_zero(void)
{
    diag_asic_snapshot_t s = {0};
    s.now_us  = 1000000ULL;
    s.n_drops = 1;
    s.drops[0].ts_us      = 9999999ULL;
    s.drops[0].chip_idx   = 0;
    s.drops[0].kind       = ROUTES_JSON_DROP_KIND_TOTAL;
    s.drops[0].ghs        = 1.0f;
    s.drops[0].elapsed_s  = 0.0f;

    char *json = capture_diag_asic(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"recent_drops\":["
        "{\"ts_ago_s\":0,\"chip\":0,\"kind\":\"total\",\"domain\":0,"
        "\"addr\":0,\"ghs\":1,\"delta\":0,\"elapsed_s\":0}"
        "]}",
        json);
    free(json);
}

/* ============================================================================
 * /api/knot — capture-based tests using RUNTIME array path
 * (build_knot_peer_json + bb_http_resp_json_arr_begin/emit/end)
 * build_knot_peer_json's own shape tests are kept below.
 * ========================================================================= */

/* Helper: capture the knot array output for a fixed peer array. */
static char *capture_knot(const knot_peer_t *peers, size_t n_peers, int64_t now_us)
{
    bb_http_request_t *req;
    bb_http_host_capture_begin(&req);
    bb_http_json_stream_t st;
    bb_http_resp_json_arr_begin(req, &st);
    for (size_t i = 0; i < n_peers; i++) {
        bb_json_t o = build_knot_peer_json(&peers[i], now_us);
        bb_http_resp_json_arr_emit(&st, o);
        bb_json_free(o);
    }
    bb_http_resp_json_arr_end(&st);
    bb_http_host_capture_t cap;
    bb_http_host_capture_end(req, &cap);
    char *copy = dupstr(cap.body);
    bb_http_host_capture_free(&cap);
    return copy;
}

void test_knot_empty(void)
{
    char *json = capture_knot(NULL, 0, 0);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL_STRING("[]", json);
    free(json);
}

void test_knot_two_peers(void)
{
    knot_peer_t peers[2] = {0};
    int64_t now_us = 30000000LL;

    strncpy(peers[0].instance_name, "taipan-alpha._taipan._tcp.local", sizeof(peers[0].instance_name) - 1);
    strncpy(peers[0].hostname, "taipan-alpha", sizeof(peers[0].hostname) - 1);
    strncpy(peers[0].ip4,      "192.168.1.10", sizeof(peers[0].ip4) - 1);
    strncpy(peers[0].worker,   "alpha-worker", sizeof(peers[0].worker) - 1);
    strncpy(peers[0].board,    "bitaxe-601",   sizeof(peers[0].board)   - 1);
    strncpy(peers[0].version,  "1.2.3",        sizeof(peers[0].version) - 1);
    strncpy(peers[0].state,    "mining",       sizeof(peers[0].state)   - 1);
    peers[0].last_seen_us = 25000000LL;

    strncpy(peers[1].instance_name, "taipan-beta._taipan._tcp.local", sizeof(peers[1].instance_name) - 1);
    strncpy(peers[1].hostname, "taipan-beta", sizeof(peers[1].hostname) - 1);
    strncpy(peers[1].ip4,      "192.168.1.11", sizeof(peers[1].ip4) - 1);
    strncpy(peers[1].worker,   "beta-worker",  sizeof(peers[1].worker)  - 1);
    strncpy(peers[1].board,    "bitaxe-403",   sizeof(peers[1].board)   - 1);
    strncpy(peers[1].version,  "1.2.0",        sizeof(peers[1].version) - 1);
    strncpy(peers[1].state,    "ota",          sizeof(peers[1].state)   - 1);
    peers[1].last_seen_us = 20000000LL;

    char *json = capture_knot(peers, 2, now_us);
    TEST_ASSERT_NOT_NULL(json);

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
    free(json);
}

void test_knot_peer_single_peer(void)
{
    /* build_knot_peer_json shape test — single peer object */
    knot_peer_t peer = {0};
    strncpy(peer.instance_name, "test-miner._taipan._tcp.local", sizeof(peer.instance_name) - 1);
    strncpy(peer.hostname, "test-miner", sizeof(peer.hostname) - 1);
    strncpy(peer.ip4,      "10.0.0.1",  sizeof(peer.ip4) - 1);
    strncpy(peer.worker,   "test-worker", sizeof(peer.worker) - 1);
    strncpy(peer.board,    "tdongle-s3",  sizeof(peer.board) - 1);
    strncpy(peer.version,  "0.9.5",       sizeof(peer.version) - 1);
    strncpy(peer.state,    "idle",        sizeof(peer.state) - 1);
    peer.last_seen_us = 60000000LL;

    int64_t now_us = 75000000LL;  /* 15 s ago */

    bb_json_t peer_obj = build_knot_peer_json(&peer, now_us);
    char *json = bb_json_serialize(peer_obj);
    bb_json_free(peer_obj);

    TEST_ASSERT_EQUAL_STRING(
        "{\"instance\":\"test-miner._taipan._tcp.local\","
        "\"hostname\":\"test-miner\",\"ip\":\"10.0.0.1\","
        "\"worker\":\"test-worker\",\"board\":\"tdongle-s3\","
        "\"version\":\"0.9.5\",\"state\":\"idle\",\"seen_ago_s\":15}",
        json);
    bb_json_free_str(json);
}

void test_knot_peer_matches_array_builder(void)
{
    /* Verify that capture_knot([peer]) produces the same object as
     * build_knot_peer_json serialized inside [...] */
    knot_peer_t peer = {0};
    strncpy(peer.instance_name, "gamma._taipan._tcp.local", sizeof(peer.instance_name) - 1);
    strncpy(peer.hostname, "gamma", sizeof(peer.hostname) - 1);
    strncpy(peer.ip4,      "172.16.1.50", sizeof(peer.ip4) - 1);
    strncpy(peer.worker,   "gamma-w", sizeof(peer.worker) - 1);
    strncpy(peer.board,    "bitaxe-650", sizeof(peer.board) - 1);
    strncpy(peer.version,  "2.1.0", sizeof(peer.version) - 1);
    strncpy(peer.state,    "mining", sizeof(peer.state) - 1);
    peer.last_seen_us = 1000000000LL;

    int64_t now_us = 1000003000LL;  /* 3000 us = 0 s */

    /* via per-peer function */
    bb_json_t peer_obj = build_knot_peer_json(&peer, now_us);
    char *peer_json = bb_json_serialize(peer_obj);
    bb_json_free(peer_obj);

    /* via capture array path */
    char *arr_json = capture_knot(&peer, 1, now_us);

    char expected[512];
    snprintf(expected, sizeof(expected), "[%s]", peer_json);
    TEST_ASSERT_EQUAL_STRING(expected, arr_json);

    bb_json_free_str(peer_json);
    free(arr_json);
}

/* ============================================================================
 * /api/settings GET
 * ========================================================================= */

void test_settings_happy_path(void)
{
    settings_snapshot_t s = {0};
    strncpy(s.hostname,  "acme-miner",     sizeof(s.hostname)  - 1);
    s.display_en     = true;
    s.ota_skip_check = false;
    s.mdns_en        = false;
    s.knot_en        = false;
    s.provisioned    = true;

    char *json = capture_settings(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"hostname\":\"acme-miner\","
        "\"display_en\":true,\"ota_skip_check\":false,"
        "\"mdns_en\":false,\"knot_en\":false,\"provisioned\":true}",
        json);
    free(json);
}

void test_settings_empty_optional_fields(void)
{
    /* hostname may be empty string; provisioned false on factory reset */
    settings_snapshot_t s = {0};
    /* hostname left as empty string */
    s.display_en     = false;
    s.ota_skip_check = true;
    s.mdns_en        = false;
    s.knot_en        = false;
    s.provisioned    = false;

    char *json = capture_settings(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_EQUAL_STRING(
        "{\"hostname\":\"\","
        "\"display_en\":false,\"ota_skip_check\":true,"
        "\"mdns_en\":false,\"knot_en\":false,\"provisioned\":false}",
        json);
    free(json);
}

void test_settings_all_bools_true(void)
{
    /* All boolean flags set to true — covers all bool branches */
    settings_snapshot_t s = {0};
    strncpy(s.hostname, "taipan-test", sizeof(s.hostname) - 1);
    s.display_en     = true;
    s.ota_skip_check = true;
    s.mdns_en        = true;
    s.knot_en        = true;
    s.provisioned    = true;

    char *json = capture_settings(&s);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"display_en\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"ota_skip_check\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"mdns_en\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"knot_en\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"provisioned\":true"));
    free(json);
}

/* /api/power and /api/fan ASIC tests are in test_routes_json_asic.c */
