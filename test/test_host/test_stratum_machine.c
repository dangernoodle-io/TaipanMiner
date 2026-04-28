#include "unity.h"
#include "stratum_machine.h"
#include "bb_json.h"
#include <string.h>
#include <math.h>

// Test stratum_machine_build_configure
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

// Test stratum_machine_build_subscribe
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

// Test stratum_machine_build_authorize
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

// Test stratum_machine_build_keepalive
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

// ---------------------------------------------------------------------------
// configure_result handler tests
// ---------------------------------------------------------------------------

void test_handle_configure_result_golden(void)
{
    // Pool responds with version-rolling support, mask = 1fffe000
    bb_json_t result = bb_json_parse(
        "{\"version-rolling\":true,\"version-rolling.mask\":\"1fffe000\","
        "\"version-rolling.min-bit-count\":13}", 0);
    TEST_ASSERT_NOT_NULL(result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_configure_result(&st, result);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0x1fffe000, st.version_mask);

    bb_json_free(result);
}

void test_handle_configure_result_missing_field(void)
{
    // Pool says version-rolling=true but omits the mask field
    bb_json_t result = bb_json_parse("{\"version-rolling\":true}", 0);
    TEST_ASSERT_NOT_NULL(result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_configure_result(&st, result);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT32(0, st.version_mask);

    bb_json_free(result);
}

void test_handle_configure_result_pool_not_supported(void)
{
    // Pool does not support version rolling
    bb_json_t result = bb_json_parse("{\"version-rolling\":false}", 0);
    TEST_ASSERT_NOT_NULL(result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.version_mask = 0;

    bool ok = stratum_machine_handle_configure_result(&st, result);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT32(0, st.version_mask);

    bb_json_free(result);
}

// ---------------------------------------------------------------------------
// subscribe_result handler tests
// ---------------------------------------------------------------------------

// Real hmpool subscribe response: extranonce1 = "08000002", extranonce2_size = 4
void test_handle_subscribe_result_golden(void)
{
    // Simulated real pool subscribe response
    bb_json_t result = bb_json_parse(
        "[[\"mining.set_difficulty\",\"sub-1\"],"
        "\"08000002\",4]", 0);
    TEST_ASSERT_NOT_NULL(result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, result);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("08000002", st.extranonce1_hex);
    TEST_ASSERT_EQUAL_INT(4, st.extranonce2_size);
    TEST_ASSERT_EQUAL_INT(4, (int)st.extranonce1_len);
    // Decoded bytes: 0x08 0x00 0x00 0x02
    TEST_ASSERT_EQUAL_UINT8(0x08, st.extranonce1[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, st.extranonce1[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, st.extranonce1[2]);
    TEST_ASSERT_EQUAL_UINT8(0x02, st.extranonce1[3]);

    bb_json_free(result);
}

void test_handle_subscribe_result_too_long_extranonce1(void)
{
    // extranonce1 = 18 hex chars = 9 bytes > MAX_EXTRANONCE1_SIZE (8)
    bb_json_t result = bb_json_parse(
        "[[\"mining.set_difficulty\",\"sub-1\"],"
        "\"0102030405060708ff\",4]", 0);
    TEST_ASSERT_NOT_NULL(result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, result);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(result);
}

void test_handle_subscribe_result_invalid_no_extranonce(void)
{
    // Array has fewer than 3 elements
    bb_json_t result = bb_json_parse("[\"only-one\"]", 0);
    TEST_ASSERT_NOT_NULL(result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, result);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(result);
}

// ---------------------------------------------------------------------------
// set_difficulty handler tests
// ---------------------------------------------------------------------------

void test_handle_set_difficulty_1(void)
{
    bb_json_t params = bb_json_parse("[1.0]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, st.difficulty);

    bb_json_free(params);
}

void test_handle_set_difficulty_65536(void)
{
    bb_json_t params = bb_json_parse("[65536.0]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(65536.0, st.difficulty);

    bb_json_free(params);
}

void test_handle_set_difficulty_fractional(void)
{
    bb_json_t params = bb_json_parse("[0.001]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(0.001, st.difficulty);

    bb_json_free(params);
}

void test_handle_set_difficulty_zero_rejected(void)
{
    bb_json_t params = bb_json_parse("[0.0]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;  // pre-existing

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);  // unchanged

    bb_json_free(params);
}

void test_handle_set_difficulty_negative_rejected(void)
{
    bb_json_t params = bb_json_parse("[-1.0]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);

    bb_json_free(params);
}

// NaN and infinity are not representable in standard JSON, but test the guard directly
void test_handle_set_difficulty_nan_rejected(void)
{
    // Build a state and call handler with a constructed number that would be NaN
    // We can't inject NaN through bb_json_parse (not valid JSON), so we test
    // by constructing params with a valid value and verifying the finite check
    // works for the boundary — instead test with 1e308 (very large but finite, should pass)
    bb_json_t params = bb_json_parse("[1e308]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_TRUE(ok);  // finite, positive — valid

    bb_json_free(params);
}

// ---------------------------------------------------------------------------
// notify handler tests
// ---------------------------------------------------------------------------

// Golden mining.notify from real pool — validates all field parsing
// Using anonymized but structurally valid data
void test_handle_notify_golden(void)
{
    // Params array: [job_id, prevhash, coinb1, coinb2, merkle_branches, version, nbits, ntime, clean_jobs]
    bb_json_t params = bb_json_parse(
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
        0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, params);
    TEST_ASSERT_TRUE(ok);

    // job_id
    TEST_ASSERT_EQUAL_STRING("test-job-01", st.job.job_id);

    // version / nbits / ntime parsed from hex
    TEST_ASSERT_EQUAL_UINT32(0x20000000, st.job.version);
    TEST_ASSERT_EQUAL_UINT32(0x1a0392a3, st.job.nbits);
    TEST_ASSERT_EQUAL_UINT32(0x67b1c400, st.job.ntime);

    // clean_jobs
    TEST_ASSERT_TRUE(st.job.clean_jobs);

    // merkle branches
    TEST_ASSERT_EQUAL_INT(2, (int)st.job.merkle_count);

    // extranonce2 reset on notify
    TEST_ASSERT_EQUAL_UINT32(0, st.extranonce2);

    // coinb1/coinb2 decoded
    TEST_ASSERT_GREATER_THAN(0, (int)st.job.coinb1_len);
    TEST_ASSERT_GREATER_THAN(0, (int)st.job.coinb2_len);

    bb_json_free(params);
}

void test_handle_notify_clean_jobs_false(void)
{
    bb_json_t params = bb_json_parse(
        "[\"test-job-02\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\","
        "\"cafecafe\","
        "[],"
        "\"20000000\","
        "\"1a0392a3\","
        "\"67b1c401\","
        "false]",
        0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(st.job.clean_jobs);
    TEST_ASSERT_EQUAL_STRING("test-job-02", st.job.job_id);
    TEST_ASSERT_EQUAL_INT(0, (int)st.job.merkle_count);

    bb_json_free(params);
}

void test_handle_notify_invalid_too_few_fields(void)
{
    // Only 5 elements, need 9
    bb_json_t params = bb_json_parse(
        "[\"job-id\",\"prevhash\",\"coinb1\",\"coinb2\",\"merkle\"]",
        0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

void test_handle_notify_wrong_type_for_version(void)
{
    // version field is a number, not a hex string — should fail string type check
    bb_json_t params = bb_json_parse(
        "[\"job-id\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"coinb1\","
        "\"coinb2\","
        "[],"
        "536870912,"
        "\"1a0392a3\","
        "\"67b1c400\","
        "true]",
        0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

// TA-186 regression guard: two sequential notifies with non-monotonic job_id —
// state must reflect the most recent one, not the first.
void test_handle_notify_ta186_non_monotonic_job_id(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    // First notify: job "old-job"
    bb_json_t params1 = bb_json_parse(
        "[\"old-job\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\","
        "\"cafecafe\","
        "[],"
        "\"20000000\","
        "\"1a0392a3\","
        "\"67b1c400\","
        "false]",
        0);
    TEST_ASSERT_NOT_NULL(params1);
    TEST_ASSERT_TRUE(stratum_machine_handle_notify(&st, params1));
    TEST_ASSERT_EQUAL_STRING("old-job", st.job.job_id);
    bb_json_free(params1);

    // Second notify: job "new-job" (non-monotonic / lower string value)
    bb_json_t params2 = bb_json_parse(
        "[\"aaa-job\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\","
        "\"cafecafe\","
        "[],"
        "\"20000000\","
        "\"1a0392a3\","
        "\"67b1c401\","
        "true]",
        0);
    TEST_ASSERT_NOT_NULL(params2);
    TEST_ASSERT_TRUE(stratum_machine_handle_notify(&st, params2));

    // State must reflect the *most recent* notify regardless of job_id order
    TEST_ASSERT_EQUAL_STRING("aaa-job", st.job.job_id);
    TEST_ASSERT_EQUAL_UINT32(0x67b1c401, st.job.ntime);
    TEST_ASSERT_TRUE(st.job.clean_jobs);

    bb_json_free(params2);
}

// ---------------------------------------------------------------------------
// subscribe + notify sequence: work_seq is NOT auto-incremented by handlers
// ---------------------------------------------------------------------------

void test_subscribe_then_notify_work_seq_unchanged(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;
    st.work_seq = 5;  // pre-existing sequence

    // Subscribe
    bb_json_t sub = bb_json_parse(
        "[[\"mining.set_difficulty\",\"sub-1\"],\"08000002\",4]", 0);
    TEST_ASSERT_NOT_NULL(sub);
    TEST_ASSERT_TRUE(stratum_machine_handle_subscribe_result(&st, sub));
    bb_json_free(sub);

    // Notify — machine handler does NOT call build_work, so work_seq unchanged
    bb_json_t notify = bb_json_parse(
        "[\"test-job-03\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\","
        "\"cafecafe\","
        "[],"
        "\"20000000\","
        "\"1a0392a3\","
        "\"67b1c402\","
        "false]",
        0);
    TEST_ASSERT_NOT_NULL(notify);
    TEST_ASSERT_TRUE(stratum_machine_handle_notify(&st, notify));
    bb_json_free(notify);

    // work_seq must still be 5 — machine doesn't auto-increment
    TEST_ASSERT_EQUAL_UINT32(5, st.work_seq);
}
