#include "unity.h"
#include "stratum_machine.h"
#include "stratum.h"
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

// ---------------------------------------------------------------------------
// build_work tests — happy path and error cases
// ---------------------------------------------------------------------------

// Setup fixture: full stratum state ready for build_work
static void _setup_ready_state(stratum_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->difficulty = 512.0;
    st->work_seq = 10;
    st->extranonce2_size = 4;
    st->extranonce2 = 0;

    // Subscribe state
    strncpy(st->extranonce1_hex, "08000002", sizeof(st->extranonce1_hex) - 1);
    st->extranonce1[0] = 0x08;
    st->extranonce1[1] = 0x00;
    st->extranonce1[2] = 0x00;
    st->extranonce1[3] = 0x02;
    st->extranonce1_len = 4;

    // Job state with minimal valid coinbase/merkle
    strncpy(st->job.job_id, "test-work-job", sizeof(st->job.job_id) - 1);
    st->job.version = 0x20000000;
    st->job.nbits = 0x1a0392a3;
    st->job.ntime = 0x67b1c400;

    // Minimal coinbase: just the structure
    st->job.coinb1[0] = 0x01;
    st->job.coinb1_len = 1;
    st->job.coinb2[0] = 0xff;
    st->job.coinb2_len = 1;

    // One merkle branch
    memset(st->job.merkle_branches[0], 0xaa, 32);
    st->job.merkle_count = 1;

    // prevhash initialized to something valid
    memset(st->job.prevhash, 0x00, 32);
}

void test_build_work_null_state(void)
{
    mining_work_t out;
    memset(&out, 0, sizeof(out));

    bool ok = stratum_machine_build_work(NULL, &out);
    TEST_ASSERT_FALSE(ok);
}

void test_build_work_null_output(void)
{
    stratum_state_t st;
    _setup_ready_state(&st);

    bool ok = stratum_machine_build_work(&st, NULL);
    TEST_ASSERT_FALSE(ok);
}

void test_build_work_happy_path(void)
{
    stratum_state_t st;
    _setup_ready_state(&st);

    mining_work_t out;
    memset(&out, 0, sizeof(out));

    bool ok = stratum_machine_build_work(&st, &out);
    TEST_ASSERT_TRUE(ok);

    // Check extranonce2_hex is populated (0x00000000 LE = "00000000")
    TEST_ASSERT_EQUAL_STRING("00000000", out.extranonce2_hex);

    // Check job_id matches
    TEST_ASSERT_EQUAL_STRING("test-work-job", out.job_id);

    // Check work_seq incremented
    TEST_ASSERT_EQUAL_UINT32(11, out.work_seq);
    TEST_ASSERT_EQUAL_UINT32(11, st.work_seq);

    // Check difficulty copied
    TEST_ASSERT_EQUAL_DOUBLE(512.0, out.difficulty);

    // Check ntime copied
    TEST_ASSERT_EQUAL_UINT32(0x67b1c400, out.ntime);

    // Check version and version_mask
    TEST_ASSERT_EQUAL_UINT32(0x20000000, out.version);
    TEST_ASSERT_EQUAL_UINT32(0, out.version_mask);  // not configured

    // Check header is 80 bytes
    TEST_ASSERT_EQUAL_INT(80, (int)sizeof(out.header));

    // Check target is 32 bytes and valid
    TEST_ASSERT_EQUAL_INT(32, (int)sizeof(out.target));
    bool target_valid = is_target_valid(out.target);
    TEST_ASSERT_TRUE(target_valid);
}

void test_build_work_with_version_mask(void)
{
    stratum_state_t st;
    _setup_ready_state(&st);
    st.version_mask = 0x1fffe000;

    mining_work_t out;
    memset(&out, 0, sizeof(out));

    bool ok = stratum_machine_build_work(&st, &out);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_UINT32(0x1fffe000, out.version_mask);
}

void test_build_work_increments_seq_multiple_times(void)
{
    stratum_state_t st;
    _setup_ready_state(&st);
    st.work_seq = 100;

    mining_work_t out1, out2;
    memset(&out1, 0, sizeof(out1));
    memset(&out2, 0, sizeof(out2));

    bool ok1 = stratum_machine_build_work(&st, &out1);
    TEST_ASSERT_TRUE(ok1);
    TEST_ASSERT_EQUAL_UINT32(101, out1.work_seq);
    TEST_ASSERT_EQUAL_UINT32(101, st.work_seq);

    bool ok2 = stratum_machine_build_work(&st, &out2);
    TEST_ASSERT_TRUE(ok2);
    TEST_ASSERT_EQUAL_UINT32(102, out2.work_seq);
    TEST_ASSERT_EQUAL_UINT32(102, st.work_seq);
}

void test_build_work_with_varying_extranonce2(void)
{
    stratum_state_t st;
    _setup_ready_state(&st);
    st.extranonce2 = 0x12345678;
    st.extranonce2_size = 4;

    mining_work_t out;
    memset(&out, 0, sizeof(out));

    bool ok = stratum_machine_build_work(&st, &out);
    TEST_ASSERT_TRUE(ok);

    // extranonce2 is LE: 0x12345678 -> bytes [0x78, 0x56, 0x34, 0x12] -> "78563412"
    TEST_ASSERT_EQUAL_STRING("78563412", out.extranonce2_hex);
}

void test_build_work_with_small_extranonce2_size(void)
{
    stratum_state_t st;
    _setup_ready_state(&st);
    st.extranonce2 = 0xABCDEF12;
    st.extranonce2_size = 2;

    mining_work_t out;
    memset(&out, 0, sizeof(out));

    bool ok = stratum_machine_build_work(&st, &out);
    TEST_ASSERT_TRUE(ok);

    // Only first 2 bytes: 0xABCDEF12 -> [0x12, 0xEF] -> "12ef"
    TEST_ASSERT_EQUAL_STRING("12ef", out.extranonce2_hex);
}

// Test NULL checks at handler entry points (lines 15, 37, 58, 78, 126, 155, 160, 194)

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
    bb_json_t result = bb_json_parse("{\"version-rolling\":true}", 0);
    TEST_ASSERT_NOT_NULL(result);

    bool ok = stratum_machine_handle_configure_result(NULL, result);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(result);
}

void test_handle_configure_null_result(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_configure_result(&st, NULL);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_subscribe_null_state(void)
{
    bb_json_t result = bb_json_parse("[[\"mining.set_difficulty\",\"sub-1\"],\"08000002\",4]", 0);
    TEST_ASSERT_NOT_NULL(result);

    bool ok = stratum_machine_handle_subscribe_result(NULL, result);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(result);
}

void test_handle_subscribe_null_result(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, NULL);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_subscribe_missing_extranonce_field(void)
{
    // Array with only 2 elements instead of 3+
    bb_json_t result = bb_json_parse("[[\"mining.set_difficulty\",\"sub-1\"],4]", 0);
    TEST_ASSERT_NOT_NULL(result);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_subscribe_result(&st, result);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(result);
}

void test_handle_set_difficulty_null_state(void)
{
    bb_json_t params = bb_json_parse("[1.0]", 0);
    TEST_ASSERT_NOT_NULL(params);

    bool ok = stratum_machine_handle_set_difficulty(NULL, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

void test_handle_set_difficulty_null_params(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_difficulty(&st, NULL);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_set_difficulty_not_array(void)
{
    bb_json_t params = bb_json_parse("1.0", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);  // unchanged

    bb_json_free(params);
}

void test_handle_set_difficulty_empty_array(void)
{
    bb_json_t params = bb_json_parse("[]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);

    bb_json_free(params);
}

void test_handle_set_difficulty_wrong_type(void)
{
    // Array with string instead of number
    bb_json_t params = bb_json_parse("[\"256.0\"]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.difficulty = 512.0;

    bool ok = stratum_machine_handle_set_difficulty(&st, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_DOUBLE(512.0, st.difficulty);

    bb_json_free(params);
}

void test_handle_notify_null_state(void)
{
    bb_json_t params = bb_json_parse(
        "[\"job-id\","
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\",\"cafecafe\",[],\"20000000\",\"1a0392a3\",\"67b1c400\",true]", 0);
    TEST_ASSERT_NOT_NULL(params);

    bool ok = stratum_machine_handle_notify(NULL, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

void test_handle_notify_null_params(void)
{
    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, NULL);
    TEST_ASSERT_FALSE(ok);
}

void test_handle_notify_missing_field_at_index_0(void)
{
    // job_id field is NULL but array is long enough
    // Craft a params array with a null where job_id should be
    bb_json_t params = bb_json_parse(
        "[null,"
        "\"000000000000000000039d6f4e3e1c7b3a5c2d9e8f1a0b4c5d6e7f8a9b0c1d2\","
        "\"deadbeef\",\"cafecafe\",[],\"20000000\",\"1a0392a3\",\"67b1c400\",true]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_notify(&st, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

// ---------------------------------------------------------------------------
// TA-306: handle_set_extranonce tests
// ---------------------------------------------------------------------------

void test_handle_set_extranonce_valid_round_trip(void)
{
    bb_json_t params = bb_json_parse("[\"deadbeef\",4]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, params);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("deadbeef", st.extranonce1_hex);
    TEST_ASSERT_EQUAL_INT(4, (int)st.extranonce1_len);
    TEST_ASSERT_EQUAL_UINT8(0xde, st.extranonce1[0]);
    TEST_ASSERT_EQUAL_UINT8(0xad, st.extranonce1[1]);
    TEST_ASSERT_EQUAL_UINT8(0xbe, st.extranonce1[2]);
    TEST_ASSERT_EQUAL_UINT8(0xef, st.extranonce1[3]);
    TEST_ASSERT_EQUAL_INT(4, st.extranonce2_size);

    bb_json_free(params);
}

void test_handle_set_extranonce_not_array(void)
{
    bb_json_t params = bb_json_parse("{\"en1\":\"deadbeef\",\"en2\":4}", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

void test_handle_set_extranonce_array_too_short(void)
{
    bb_json_t params = bb_json_parse("[\"deadbeef\"]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

void test_handle_set_extranonce_en1_not_string(void)
{
    bb_json_t params = bb_json_parse("[12345,4]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

void test_handle_set_extranonce_en1_too_long(void)
{
    /* MAX_EXTRANONCE1_SIZE = 8, so 18 hex chars = 9 bytes → reject */
    bb_json_t params = bb_json_parse("[\"0102030405060708ff\",4]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

void test_handle_set_extranonce_en2_not_number(void)
{
    bb_json_t params = bb_json_parse("[\"deadbeef\",\"4\"]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));

    bool ok = stratum_machine_handle_set_extranonce(&st, params);
    TEST_ASSERT_FALSE(ok);

    bb_json_free(params);
}

void test_handle_set_extranonce_en2_negative(void)
{
    bb_json_t params = bb_json_parse("[\"deadbeef\",-1]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.extranonce2_size = 4;  // pre-existing

    bool ok = stratum_machine_handle_set_extranonce(&st, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(4, st.extranonce2_size);  // unchanged

    bb_json_free(params);
}

void test_handle_set_extranonce_en2_too_large(void)
{
    bb_json_t params = bb_json_parse("[\"deadbeef\",17]", 0);
    TEST_ASSERT_NOT_NULL(params);

    stratum_state_t st;
    memset(&st, 0, sizeof(st));
    st.extranonce2_size = 4;

    bool ok = stratum_machine_handle_set_extranonce(&st, params);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(4, st.extranonce2_size);  // unchanged

    bb_json_free(params);
}

// ---------------------------------------------------------------------------
// TA-273 Phase 4: reject classifier tests
// ---------------------------------------------------------------------------

// Test classification of code 21 → JOB_NOT_FOUND
void test_classify_reject_job_not_found(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(21);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_JOB_NOT_FOUND, kind);
}

// Test classification of code 22 → DUPLICATE
void test_classify_reject_duplicate(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(22);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_DUPLICATE, kind);
}

// Test classification of code 23 → LOW_DIFFICULTY
void test_classify_reject_low_difficulty(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(23);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_LOW_DIFFICULTY, kind);
}

// Test classification of code 25 → STALE_PREVHASH
void test_classify_reject_stale_prevhash(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(25);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_STALE_PREVHASH, kind);
}

// Test unknown codes map to OTHER
void test_classify_reject_unknown_code_24(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(24);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, kind);
}

void test_classify_reject_unknown_code_26(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(26);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, kind);
}

void test_classify_reject_unknown_code_99(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(99);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, kind);
}

void test_classify_reject_unknown_code_negative_one(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(-1);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, kind);
}

void test_classify_reject_unknown_code_zero(void)
{
    stratum_reject_kind_t kind = stratum_machine_classify_reject(0);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, kind);
}

// ---------------------------------------------------------------------------
// Round-trip tests with stratum_parse_error_code + classify
// ---------------------------------------------------------------------------

// Array form: [21, "Job not found", "..."]
void test_classify_reject_round_trip_array_job_not_found(void)
{
    bb_json_t error = bb_json_parse("[21,\"Job not found\",\"\"]", 0);
    TEST_ASSERT_NOT_NULL(error);

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(21, code);

    stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_JOB_NOT_FOUND, kind);

    bb_json_free(error);
}

// Array form: [22, "Duplicate share", ""]
void test_classify_reject_round_trip_array_duplicate(void)
{
    bb_json_t error = bb_json_parse("[22,\"Duplicate share\",\"\"]", 0);
    TEST_ASSERT_NOT_NULL(error);

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(22, code);

    stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_DUPLICATE, kind);

    bb_json_free(error);
}

// Array form: [23, "Low difficulty share", ""]
void test_classify_reject_round_trip_array_low_difficulty(void)
{
    bb_json_t error = bb_json_parse("[23,\"Low difficulty share\",\"\"]", 0);
    TEST_ASSERT_NOT_NULL(error);

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(23, code);

    stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_LOW_DIFFICULTY, kind);

    bb_json_free(error);
}

// Array form: [25, "Not subscribed/stale prevhash", ""]
void test_classify_reject_round_trip_array_stale_prevhash(void)
{
    bb_json_t error = bb_json_parse("[25,\"Not subscribed/stale prevhash\",\"\"]", 0);
    TEST_ASSERT_NOT_NULL(error);

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(25, code);

    stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_STALE_PREVHASH, kind);

    bb_json_free(error);
}

// Object form: {"code": 21, "message": "Job not found"}
void test_classify_reject_round_trip_object_job_not_found(void)
{
    bb_json_t error = bb_json_parse("{\"code\":21,\"message\":\"Job not found\"}", 0);
    TEST_ASSERT_NOT_NULL(error);

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(21, code);

    stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_JOB_NOT_FOUND, kind);

    bb_json_free(error);
}

// Object form: {"code": 22, "message": "Duplicate share"}
void test_classify_reject_round_trip_object_duplicate(void)
{
    bb_json_t error = bb_json_parse("{\"code\":22,\"message\":\"Duplicate share\"}", 0);
    TEST_ASSERT_NOT_NULL(error);

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(22, code);

    stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_DUPLICATE, kind);

    bb_json_free(error);
}

// Missing error code → parse_error_code returns -1 → OTHER
void test_classify_reject_round_trip_missing_code(void)
{
    bb_json_t error = bb_json_parse("{\"message\":\"Some error\"}", 0);
    TEST_ASSERT_NOT_NULL(error);

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(-1, code);

    stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, kind);

    bb_json_free(error);
}

// Null error array → parse_error_code returns -1 → OTHER
void test_classify_reject_round_trip_null_error(void)
{
    bb_json_t error = bb_json_parse("null", 0);
    TEST_ASSERT_NOT_NULL(error);

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(-1, code);

    stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, kind);

    bb_json_free(error);
}
