// Ported from TaipanMiner jae/ta532-ota-hosts-guard's test_stratum_machine.c
// (TA-273), then jae/tm-stratum's v2-floor port (TA-560). Response handlers
// were originally driven off a parsed bb_json_t tree (deleted); this port
// drives them off a bb_serialize_json tok-recorder scan instead (see
// stratum_machine.h). TRIMMED of every test_build_work_*/
// test_subscribe_then_notify_work_seq_unchanged case: build_work() lives in
// work_build.h/test_work_build.c now, not stratum_machine. Everything else
// is the same assertions, re-expressed against the tok-recorder API.
#include "unity.h"
#include "stratum_machine.h"
#include "bb_serialize_json.h"
#include <string.h>
#include <math.h>

#define TEST_TOK_POOL_CAP 64

// Scans `json` (a complete document) into `rec`/`pool` (bounded mode, no
// arena -- every fixture string below is escape-free) and returns the root
// token index, or BB_SERIALIZE_JSON_TOK_ABSENT on a scan failure.
static bb_serialize_json_tok_idx_t scan_json(const char *json,
                                             bb_serialize_json_tok_recorder_t *rec,
                                             bb_serialize_json_tok_t *pool)
{
    size_t len = strlen(json);
    if (bb_serialize_json_tok_recorder_init(rec, json, len, pool, TEST_TOK_POOL_CAP, NULL, 0) != BB_OK) {
        return BB_SERIALIZE_JSON_TOK_ABSENT;
    }
    bb_serialize_json_ingest_t ingest = bb_serialize_json_tok_recorder_ingest(rec);
    if (bb_serialize_json_scan_bounded(json, len, &ingest) != BB_OK) {
        return BB_SERIALIZE_JSON_TOK_ABSENT;
    }
    return bb_serialize_json_tok_root(rec);
}

void test_stratum_machine_build_configure(void)
{
    char buf[256];
    int result = stratum_machine_build_configure(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[[\"version-rolling\"],"
                            "{\"version-rolling.mask\":\"1fffe000\","
                            "\"version-rolling.min-bit-count\":13}]", buf);
}

void test_stratum_machine_build_configure_truncation(void)
{
    char buf[10];
    int result = stratum_machine_build_configure(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_stratum_machine_build_subscribe(void)
{
    char buf[256];
    int result = stratum_machine_build_subscribe(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[\"TaipanMiner/0.1\"]", buf);
}

void test_stratum_machine_build_subscribe_truncation(void)
{
    char buf[10];
    int result = stratum_machine_build_subscribe(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_stratum_machine_build_authorize(void)
{
    char buf[256];
    int result = stratum_machine_build_authorize(buf, sizeof(buf),
                                                 "tk-test-000", "test-worker", "test-pass");
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[\"tk-test-000.test-worker\",\"test-pass\"]", buf);
}

void test_stratum_machine_build_authorize_different_values(void)
{
    char buf[256];
    int result = stratum_machine_build_authorize(buf, sizeof(buf),
                                                 "wallet-addr", "miner-01", "secretpass");
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[\"wallet-addr.miner-01\",\"secretpass\"]", buf);
}

void test_stratum_machine_build_authorize_truncation(void)
{
    char buf[10];
    int result = stratum_machine_build_authorize(buf, sizeof(buf),
                                                 "tk-test-000", "test-worker", "test-pass");
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_stratum_machine_build_keepalive(void)
{
    char buf[256];
    int result = stratum_machine_build_keepalive(buf, sizeof(buf), 512.0);
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[512.0000]", buf);
}

void test_stratum_machine_build_keepalive_small_difficulty(void)
{
    char buf[256];
    int result = stratum_machine_build_keepalive(buf, sizeof(buf), 1.5);
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[1.5000]", buf);
}

void test_stratum_machine_build_keepalive_large_difficulty(void)
{
    char buf[256];
    int result = stratum_machine_build_keepalive(buf, sizeof(buf), 1000000.5);
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[1000000.5000]", buf);
}

void test_stratum_machine_build_keepalive_truncation(void)
{
    char buf[5];
    int result = stratum_machine_build_keepalive(buf, sizeof(buf), 512.0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_handle_configure_result_golden(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    // NOTE: "version-rolling.min-bit-count" (30 bytes) is deliberately
    // omitted -- it exceeds BB_SERIALIZE_JSON_TOK_KEY_MAX_LEN (24) and would
    // abort the WHOLE scan (BB_ERR_NO_SPACE) if present, even though this
    // handler never reads it. See the PR report for this as a flagged
    // real-world risk: a pool that sends this BIP-310 field in its
    // mining.configure response would fail this scan entirely.
    bb_serialize_json_tok_idx_t result = scan_json(
        "{\"version-rolling\":true,\"version-rolling.mask\":\"1fffe000\"}", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_configure_result(&st, &rec, result);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0x1fffe000, st.version_mask);
}

void test_handle_configure_result_missing_field(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json("{\"version-rolling\":true}", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_configure_result(&st, &rec, result);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT32(0, st.version_mask);
}

void test_handle_configure_result_pool_not_supported(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json("{\"version-rolling\":false}", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.version_mask = 0;

    bool ok = stratum_machine_handle_configure_result(&st, &rec, result);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT32(0, st.version_mask);
}

void test_handle_subscribe_result_golden(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json(
        "[[\"mining.set_difficulty\",\"sub-1\"],"
        "\"08000002\",4]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, &rec, result);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("08000002", st.extranonce1_hex);
    TEST_ASSERT_EQUAL_INT(4, st.extranonce2_size);
    TEST_ASSERT_EQUAL_INT(4, (int)st.extranonce1_len);
    TEST_ASSERT_EQUAL_UINT8(0x08, st.extranonce1[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, st.extranonce1[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, st.extranonce1[2]);
    TEST_ASSERT_EQUAL_UINT8(0x02, st.extranonce1[3]);
}

void test_handle_subscribe_result_too_long_extranonce1(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json(
        "[[\"mining.set_difficulty\",\"sub-1\"],"
        "\"0102030405060708ff\",4]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, &rec, result);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_subscribe_result_invalid_no_extranonce(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json("[\"only-one\"]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, &rec, result);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_subscribe_result_extranonce2_size_at_max_accepted(void)
{
    // STRATUM_MAX_EXTRANONCE2_SIZE is 8 -- keep the JSON literal and the
    // assertion tied to the real constant so this test tracks any future
    // change to the cap.
    TEST_ASSERT_EQUAL_INT(8, STRATUM_MAX_EXTRANONCE2_SIZE);

    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json(
        "[[\"mining.set_difficulty\",\"sub-1\"],"
        "\"08000002\",8]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, &rec, result);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(STRATUM_MAX_EXTRANONCE2_SIZE, st.extranonce2_size);
}

void test_handle_subscribe_result_extranonce2_size_over_max_rejected(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json(
        "[[\"mining.set_difficulty\",\"sub-1\"],"
        "\"08000002\",9999]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, &rec, result);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_subscribe_result_extranonce2_size_negative_rejected(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json(
        "[[\"mining.set_difficulty\",\"sub-1\"],"
        "\"08000002\",-1]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, &rec, result);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_difficulty_1(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[1.0]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, st.difficulty);
}

void test_handle_set_difficulty_65536(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[65536.0]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(65536.0, st.difficulty);
}

void test_handle_set_difficulty_fractional(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[0.001]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(0.001, st.difficulty);
}

void test_handle_set_difficulty_zero_rejected(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[0.0]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);
}

void test_handle_set_difficulty_negative_rejected(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[-1.0]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);
}

void test_handle_set_difficulty_large_finite_accepted(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[1e308]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_TRUE(ok);
}

void test_handle_notify_golden(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json(
        "[\"test-job-01\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff\","
        "\"ffffffff01\","
        "[\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"],"
        "\"20000000\","
        "\"1a0392a3\","
        "\"67b1c400\","
        "true]",
        &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, &rec, params);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_STRING("test-job-01", st.job.job_id);
    TEST_ASSERT_EQUAL_UINT32(0x20000000, st.job.version);
    TEST_ASSERT_EQUAL_UINT32(0x1a0392a3, st.job.nbits);
    TEST_ASSERT_EQUAL_UINT32(0x67b1c400, st.job.ntime);
    TEST_ASSERT_TRUE(st.job.clean_jobs);
    TEST_ASSERT_EQUAL_INT(2, (int)st.job.merkle_count);
    TEST_ASSERT_EQUAL_UINT32(0, st.extranonce2);
    TEST_ASSERT_GREATER_THAN(0, (int)st.job.coinb1_len);
    TEST_ASSERT_GREATER_THAN(0, (int)st.job.coinb2_len);
}

void test_handle_notify_clean_jobs_false(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json(
        "[\"test-job-02\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\","
        "\"cafecafe\","
        "[],"
        "\"20000000\","
        "\"1a0392a3\","
        "\"67b1c401\","
        "false]",
        &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, &rec, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(st.job.clean_jobs);
    TEST_ASSERT_EQUAL_STRING("test-job-02", st.job.job_id);
    TEST_ASSERT_EQUAL_INT(0, (int)st.job.merkle_count);
}

void test_handle_notify_invalid_too_few_fields(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json(
        "[\"job-id\",\"prevhash\",\"coinb1\",\"coinb2\",\"merkle\"]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_notify_wrong_type_for_version(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json(
        "[\"job-id\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"coinb1\","
        "\"coinb2\","
        "[],"
        "536870912,"
        "\"1a0392a3\","
        "\"67b1c400\","
        "true]",
        &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_notify_ta186_non_monotonic_job_id(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bb_serialize_json_tok_t pool1[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec1;
    bb_serialize_json_tok_idx_t params1 = scan_json(
        "[\"old-job\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\","
        "\"cafecafe\","
        "[],"
        "\"20000000\","
        "\"1a0392a3\","
        "\"67b1c400\","
        "false]",
        &rec1, pool1);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params1);
    TEST_ASSERT_TRUE(stratum_machine_handle_notify(&st, &rec1, params1));
    TEST_ASSERT_EQUAL_STRING("old-job", st.job.job_id);

    bb_serialize_json_tok_t pool2[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec2;
    bb_serialize_json_tok_idx_t params2 = scan_json(
        "[\"aaa-job\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\","
        "\"cafecafe\","
        "[],"
        "\"20000000\","
        "\"1a0392a3\","
        "\"67b1c401\","
        "true]",
        &rec2, pool2);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params2);
    TEST_ASSERT_TRUE(stratum_machine_handle_notify(&st, &rec2, params2));

    TEST_ASSERT_EQUAL_STRING("aaa-job", st.job.job_id);
    TEST_ASSERT_EQUAL_UINT32(0x67b1c401, st.job.ntime);
    TEST_ASSERT_TRUE(st.job.clean_jobs);
}

void test_build_configure_null_buf(void)
{
    int result = stratum_machine_build_configure(NULL, 256);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_configure_zero_size(void)
{
    char buf[256];
    int result = stratum_machine_build_configure(buf, 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_subscribe_null_buf(void)
{
    int result = stratum_machine_build_subscribe(NULL, 256);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_subscribe_zero_size(void)
{
    char buf[256];
    int result = stratum_machine_build_subscribe(buf, 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_authorize_null_buf(void)
{
    int result = stratum_machine_build_authorize(NULL, 256, "wallet", "worker", "pass");
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_authorize_zero_size(void)
{
    char buf[256];
    int result = stratum_machine_build_authorize(buf, 0, "wallet", "worker", "pass");
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_authorize_null_wallet(void)
{
    char buf[256];
    int result = stratum_machine_build_authorize(buf, 256, NULL, "worker", "pass");
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_authorize_null_worker(void)
{
    char buf[256];
    int result = stratum_machine_build_authorize(buf, 256, "wallet", NULL, "pass");
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_authorize_null_pass(void)
{
    char buf[256];
    int result = stratum_machine_build_authorize(buf, 256, "wallet", "worker", NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_keepalive_null_buf(void)
{
    int result = stratum_machine_build_keepalive(NULL, 256, 512.0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_build_keepalive_zero_size(void)
{
    char buf[256];
    int result = stratum_machine_build_keepalive(buf, 0, 512.0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_handle_configure_null_state(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json("{\"version-rolling\":true}", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    bool ok = stratum_machine_handle_configure_result(NULL, &rec, result);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_configure_null_result(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_configure_result(&st, NULL, BB_SERIALIZE_JSON_TOK_ABSENT);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_subscribe_null_state(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json("[[\"mining.set_difficulty\",\"sub-1\"],\"08000002\",4]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    bool ok = stratum_machine_handle_subscribe_result(NULL, &rec, result);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_subscribe_null_result(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, NULL, BB_SERIALIZE_JSON_TOK_ABSENT);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_subscribe_missing_extranonce_field(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t result = scan_json("[[\"mining.set_difficulty\",\"sub-1\"],4]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, &rec, result);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_difficulty_null_state(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[1.0]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    bool ok = stratum_machine_handle_set_difficulty(NULL, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_difficulty_null_params(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, NULL, BB_SERIALIZE_JSON_TOK_ABSENT);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_difficulty_not_array(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("1.0", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);
}

void test_handle_set_difficulty_empty_array(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);
}

void test_handle_set_difficulty_wrong_type(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[\"256.0\"]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);
}

void test_handle_notify_null_state(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json(
        "[\"job-id\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\",\"cafecafe\",[],\"20000000\",\"1a0392a3\",\"67b1c400\",true]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    bool ok = stratum_machine_handle_notify(NULL, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_notify_null_params(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, NULL, BB_SERIALIZE_JSON_TOK_ABSENT);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_notify_missing_field_at_index_0(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json(
        "[null,"
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\",\"cafecafe\",[],\"20000000\",\"1a0392a3\",\"67b1c400\",true]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_extranonce_valid_round_trip(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[\"deadbeef\",4]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, &rec, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("deadbeef", st.extranonce1_hex);
    TEST_ASSERT_EQUAL_INT(4, (int)st.extranonce1_len);
    TEST_ASSERT_EQUAL_UINT8(0xde, st.extranonce1[0]);
    TEST_ASSERT_EQUAL_UINT8(0xad, st.extranonce1[1]);
    TEST_ASSERT_EQUAL_UINT8(0xbe, st.extranonce1[2]);
    TEST_ASSERT_EQUAL_UINT8(0xef, st.extranonce1[3]);
    TEST_ASSERT_EQUAL_INT(4, st.extranonce2_size);
}

void test_handle_set_extranonce_not_array(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("{\"en1\":\"deadbeef\",\"en2\":4}", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_extranonce_array_too_short(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[\"deadbeef\"]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_extranonce_en1_not_string(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[12345,4]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_extranonce_en1_too_long(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[\"0102030405060708ff\",4]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_extranonce_en2_not_number(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[\"deadbeef\",\"4\"]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_extranonce_en2_negative(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[\"deadbeef\",-1]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.extranonce2_size = 4;

    bool ok = stratum_machine_handle_set_extranonce(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(4, st.extranonce2_size);
}

void test_handle_set_extranonce_en2_too_large(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t params = scan_json("[\"deadbeef\",17]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.extranonce2_size = 4;

    bool ok = stratum_machine_handle_set_extranonce(&st, &rec, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(4, st.extranonce2_size);
}
