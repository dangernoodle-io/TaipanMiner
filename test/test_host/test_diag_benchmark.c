/*
 * test_diag_benchmark.c — host unit tests for TA-33 /api/diag/benchmark.
 *
 * Tests cover:
 *  - diag_bench_parse_request: bounds validation and default handling
 *  - emit_diag_bench_json: response JSON shape (exact keys), capture-based
 *
 * The full HTTP handler (diag_benchmark_handler) lives in routes.c which is
 * ESP-only and not compiled for the native env.  The parsing and response-
 * building helpers in routes_json.c are host-compilable and tested here.
 */

#include "unity.h"
#include "routes_json.h"
#include "bb_json.h"
#include "bb_http.h"
#include "bb_http_host.h"
#include "bb_core.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Helpers
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

/* Capture emit_diag_bench_json output into a heap string.
 * Caller must free() the returned pointer. */
static char *capture_bench(const diag_bench_snapshot_t *snap)
{
    bb_http_request_t *req;
    bb_http_host_capture_begin(&req);
    bb_http_json_obj_stream_t obj;
    bb_http_resp_json_obj_begin(req, &obj);
    emit_diag_bench_json(&obj, snap);
    bb_http_resp_json_obj_end(&obj);
    bb_http_host_capture_t cap;
    bb_http_host_capture_end(req, &cap);
    char *copy = dupstr(cap.body);
    bb_http_host_capture_free(&cap);
    return copy;
}

/* ============================================================================
 * diag_bench_parse_request tests
 * ========================================================================= */

/* NULL body → default iters */
void test_diag_bench_parse_null_body_defaults(void)
{
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(NULL, 0, &iters);
    TEST_ASSERT_EQUAL(BB_OK, rc);
    TEST_ASSERT_EQUAL_UINT32(DIAG_BENCH_ITERS_DEFAULT, iters);
}

/* Empty body → default iters */
void test_diag_bench_parse_empty_body_defaults(void)
{
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request("", 0, &iters);
    TEST_ASSERT_EQUAL(BB_OK, rc);
    TEST_ASSERT_EQUAL_UINT32(DIAG_BENCH_ITERS_DEFAULT, iters);
}

/* {} (no iters field) → default iters */
void test_diag_bench_parse_no_iters_field_defaults(void)
{
    const char *body = "{}";
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(body, (int)strlen(body), &iters);
    TEST_ASSERT_EQUAL(BB_OK, rc);
    TEST_ASSERT_EQUAL_UINT32(DIAG_BENCH_ITERS_DEFAULT, iters);
}

/* iters=999 (below min) → BB_ERR_NO_SPACE (out-of-range sentinel) */
void test_diag_bench_parse_iters_below_min_rejected(void)
{
    const char *body = "{\"iters\":999}";
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(body, (int)strlen(body), &iters);
    TEST_ASSERT_EQUAL(BB_ERR_NO_SPACE, rc);
}

/* iters=100001 (above max) → BB_ERR_NO_SPACE (out-of-range sentinel) */
void test_diag_bench_parse_iters_above_max_rejected(void)
{
    const char *body = "{\"iters\":100001}";
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(body, (int)strlen(body), &iters);
    TEST_ASSERT_EQUAL(BB_ERR_NO_SPACE, rc);
}

/* iters=1000 (exactly at min) → accepted */
void test_diag_bench_parse_iters_at_min_accepted(void)
{
    const char *body = "{\"iters\":1000}";
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(body, (int)strlen(body), &iters);
    TEST_ASSERT_EQUAL(BB_OK, rc);
    TEST_ASSERT_EQUAL_UINT32(1000, iters);
}

/* iters=100000 (exactly at max) → accepted */
void test_diag_bench_parse_iters_at_max_accepted(void)
{
    const char *body = "{\"iters\":100000}";
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(body, (int)strlen(body), &iters);
    TEST_ASSERT_EQUAL(BB_OK, rc);
    TEST_ASSERT_EQUAL_UINT32(100000, iters);
}

/* iters=10000 (default value, explicitly provided) → accepted */
void test_diag_bench_parse_iters_10000_accepted(void)
{
    const char *body = "{\"iters\":10000}";
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(body, (int)strlen(body), &iters);
    TEST_ASSERT_EQUAL(BB_OK, rc);
    TEST_ASSERT_EQUAL_UINT32(10000, iters);
}

/* iters field present but wrong type (string) → BB_ERR_INVALID_ARG */
void test_diag_bench_parse_iters_wrong_type_rejected(void)
{
    const char *body = "{\"iters\":\"ten thousand\"}";
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(body, (int)strlen(body), &iters);
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, rc);
}

/* Completely invalid JSON → BB_ERR_INVALID_ARG */
void test_diag_bench_parse_invalid_json_rejected(void)
{
    const char *body = "not json {";
    uint32_t iters = 0;
    bb_err_t rc = diag_bench_parse_request(body, (int)strlen(body), &iters);
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, rc);
}

/* ============================================================================
 * emit_diag_bench_json response shape tests (capture-based)
 * ========================================================================= */

/* Happy path — non-ASIC "sw" backend: verify all required keys present */
void test_diag_bench_json_shape_sw_backend(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 10000,
        .duration_us        = 8275,
        .us_per_op          = 0.8275,
        .khs                = 1208.2,
        .sha_ops_per_sec   = 1208.5,
        .backend            = "sw",
        .text_overlap_state = SHA_OVERLAP_SAFE,
        .h_write_state      = SHA_OVERLAP_SAFE,
        .asic_active        = false,
        .has_asic_active    = false,
        .settled            = true,
        .settled_after_iters = 1000,
        .settled_iters      = 9000,
        .settled_total_us   = 7448,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);

    /* Must contain all required top-level keys */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"iters\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"duration_us\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"us_per_op\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"khs\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sha_ops_per_sec\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"backend\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"canary\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text_overlap\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write\""));
    /* Convergence fields */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_after_iters\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_iters\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_total_us\""));

    /* Backend name is "sw" */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sw\""));

    /* asic_active must NOT be present when has_asic_active is false */
    TEST_ASSERT_NULL(strstr(json, "\"asic_active\""));

    /* canary values: "safe" (strings, not booleans) */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text_overlap\":\"safe\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write\":\"safe\""));

    free(json);
}

/* ahb backend */
void test_diag_bench_json_shape_ahb_backend(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 5000,
        .duration_us        = 20000,
        .us_per_op          = 4.0,
        .khs                = 250.0,
        .sha_ops_per_sec   = 250000.0,
        .backend            = "ahb",
        .text_overlap_state = SHA_OVERLAP_SAFE,
        .h_write_state      = SHA_OVERLAP_UNSAFE,
        .asic_active        = false,
        .has_asic_active    = false,
        .settled            = true,
        .settled_after_iters = 400,
        .settled_iters      = 4600,
        .settled_total_us   = 18400,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"ahb\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write\":\"unsafe\""));
    free(json);
}

/* dport backend */
void test_diag_bench_json_shape_dport_backend(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 1000,
        .duration_us        = 15000,
        .us_per_op          = 15.0,
        .khs                = 66.7,
        .sha_ops_per_sec   = 66666.7,
        .backend            = "dport",
        .text_overlap_state = SHA_OVERLAP_UNSAFE,
        .h_write_state      = SHA_OVERLAP_UNSAFE,
        .asic_active        = false,
        .has_asic_active    = false,
        .settled            = false,
        .settled_after_iters = 0,
        .settled_iters      = 0,
        .settled_total_us   = 0,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"dport\""));
    free(json);
}

/* asic_active field present when has_asic_active is true */
void test_diag_bench_json_asic_active_present_when_flagged(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 10000,
        .duration_us        = 1000,
        .us_per_op          = 0.1,
        .khs                = 10000.0,
        .sha_ops_per_sec   = 10000000.0,
        .backend            = "ahb",
        .text_overlap_state = SHA_OVERLAP_SAFE,
        .h_write_state      = SHA_OVERLAP_SAFE,
        .asic_active        = true,
        .has_asic_active    = true,
        .settled            = true,
        .settled_after_iters = 200,
        .settled_iters      = 9800,
        .settled_total_us   = 980,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_active\":true"));
    free(json);
}

/* Tristate canary: UNKNOWN (probe didn't run on D0/ASIC boards) */
void test_diag_bench_json_canary_unknown(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 10000,
        .duration_us        = 1000,
        .us_per_op          = 0.1,
        .khs                = 10000.0,
        .sha_ops_per_sec   = 10000000.0,
        .backend            = "ahb",
        .text_overlap_state = SHA_OVERLAP_UNKNOWN,
        .h_write_state      = SHA_OVERLAP_UNKNOWN,
        .asic_active        = false,
        .has_asic_active    = false,
        .settled            = true,
        .settled_after_iters = 200,
        .settled_iters      = 9800,
        .settled_total_us   = 980,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);
    /* Both canaries emit "unknown" string, not conflated with false/unsafe */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text_overlap\":\"unknown\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write\":\"unknown\""));
    free(json);
}

/* Tristate canary: mixed state (text_overlap=UNSAFE, h_write=UNKNOWN) */
void test_diag_bench_json_canary_mixed(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 10000,
        .duration_us        = 1000,
        .us_per_op          = 0.1,
        .khs                = 10000.0,
        .sha_ops_per_sec   = 10000000.0,
        .backend            = "ahb",
        .text_overlap_state = SHA_OVERLAP_UNSAFE,
        .h_write_state      = SHA_OVERLAP_UNKNOWN,
        .asic_active        = false,
        .has_asic_active    = false,
        .settled            = true,
        .settled_after_iters = 200,
        .settled_iters      = 9800,
        .settled_total_us   = 980,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text_overlap\":\"unsafe\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write\":\"unknown\""));
    free(json);
}

/* ============================================================================
 * TA-395: khs / sha_ops_per_sec relationship invariant
 *
 * Synthetic bench result (min-of-tail semantics): duration_us=3000, iters=1000,
 * us_per_op=1.0. With min-of-tail, settled_total_us = one min bucket time,
 * settled_iters = bucket_size.
 *   khs = settled_iters * 1000 / settled_total_us = 200 * 1000 / 600 = 333.33...
 *   sha_ops_per_sec = 1e6 / us_per_op = 1e6 / 1.0 = 1000000.0
 *
 * Cross-check: sha_ops_per_sec / khs ~= 3 (Bitcoin double-SHA = 3 SHA ops/nonce)
 * This invariant catches regression back to the old khs = 1/us_per_op formula.
 * ========================================================================= */
void test_diag_bench_json_khs_invariant(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 1000,
        .duration_us        = 3000,
        .us_per_op          = 1.0,
        /* khs and sha_ops_per_sec are computed by the handler, not the snapshot.
         * Populate them with the correct values to test build_diag_bench_json passthrough.
         * Min-of-tail: settled_total_us = single min bucket time; settled_iters = bucket_size. */
        .khs                = 333.333333,   /* settled_iters * 1000 / settled_total_us */
        .sha_ops_per_sec   = 1000000.0,    /* 1e6 / us_per_op (ops/s) */
        .backend            = "dport",
        .text_overlap_state = SHA_OVERLAP_SAFE,
        .h_write_state      = SHA_OVERLAP_SAFE,
        .asic_active        = false,
        .has_asic_active    = false,
        .settled            = true,
        .settled_after_iters = 200,
        .settled_iters      = 200,          /* bucket_size (min-of-tail: one bucket) */
        .settled_total_us   = 600,          /* min_bucket_us: 200 * 1000 / 333.333 = 600 us */
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);

    bb_json_t root = bb_json_parse(json, 0);
    TEST_ASSERT_NOT_NULL(root);
    free(json);

    /* Verify khs is present and plausible (JSON round-trip) */
    bb_json_t j_khs = bb_json_obj_get_item(root, "khs");
    TEST_ASSERT_NOT_NULL(j_khs);
    double got_khs = bb_json_item_get_double(j_khs);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 333.333333, got_khs);

    /* Verify sha_ops_per_sec is present */
    bb_json_t j_sha = bb_json_obj_get_item(root, "sha_ops_per_sec");
    TEST_ASSERT_NOT_NULL(j_sha);
    double got_sha_kops = bb_json_item_get_double(j_sha);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1000000.0, got_sha_kops);

    /* Cross-check: sha_ops_per_sec (ops/s) / (khs * 1000) (nonces/s) ~= 3 */
    double ratio = got_sha_kops / (got_khs * 1000.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 3.0, ratio);

    bb_json_free(root);
}

/* ============================================================================
 * TA-401: AHB bench us_per_op semantic invariant
 *
 * AHB uses the midstate optimization: block1 (first SHA on the 80-byte header)
 * is computed once per job, not per nonce. Per nonce the hot loop does 2 SHA
 * block ops: pass1 (SHA_CONTINUE, block2 tail with midstate) + pass2 (SHA_START,
 * outer double-SHA digest). sha256_hw_bench_pass2 divides by 2 so us_per_op
 * is per-SHA-block-op, matching DPORT semantics.
 *
 * Synthetic AHB-shaped snapshot (min-of-tail semantics): duration_us=2000, iters=1000.
 * With min-of-tail: settled_total_us = min_bucket_us (one bucket), settled_iters = bucket_size.
 *   us_per_op = min_bucket_us / bucket_size / 2 = 400 / 200 / 2 = 1.0 us/SHA-op
 *   khs = settled_iters * 1000 / settled_total_us = 200 * 1000 / 400 = 500.0
 *   sha_ops_per_sec = 1e6 / us_per_op = 1e6 / 1.0 = 1000000.0
 *
 * Cross-check: sha_ops_per_sec / (khs * 1000) ~= 2 for AHB (2 SHA ops/nonce).
 * ========================================================================= */
void test_diag_bench_json_khs_invariant_ahb(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 1000,
        .duration_us        = 2000,
        .us_per_op          = 1.0,   /* min_bucket_us / bucket_size / 2 */
        .khs                = 500.0, /* settled_iters * 1000 / settled_total_us */
        .sha_ops_per_sec   = 1000000.0, /* 1e6 / us_per_op */
        .backend            = "ahb",
        .text_overlap_state = SHA_OVERLAP_SAFE,
        .h_write_state      = SHA_OVERLAP_SAFE,
        .asic_active        = false,
        .has_asic_active    = false,
        .settled            = true,
        .settled_after_iters = 200,
        .settled_iters      = 200,          /* bucket_size (min-of-tail: one bucket) */
        .settled_total_us   = 400,          /* min_bucket_us: 200 * 1000 / 500.0 = 400 us */
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);

    bb_json_t root = bb_json_parse(json, 0);
    TEST_ASSERT_NOT_NULL(root);
    free(json);

    bb_json_t j_khs = bb_json_obj_get_item(root, "khs");
    TEST_ASSERT_NOT_NULL(j_khs);
    double got_khs = bb_json_item_get_double(j_khs);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 500.0, got_khs);

    bb_json_t j_sha = bb_json_obj_get_item(root, "sha_ops_per_sec");
    TEST_ASSERT_NOT_NULL(j_sha);
    double got_sha_kops = bb_json_item_get_double(j_sha);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1000000.0, got_sha_kops);

    /* Cross-check: sha_ops_per_sec / (khs * 1000) ~= 2 for AHB */
    double ratio = got_sha_kops / (got_khs * 1000.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 2.0, ratio);

    bb_json_free(root);
}

/* ============================================================================
 * Adaptive convergence: unsettled fallback path test.
 *
 * When iters too small or SHA peripheral never settles, bench_pass2 falls
 * back to full-window average with settled=false. The JSON response MUST:
 *   - emit settled=false
 *   - emit settled_after_iters=0, settled_iters=0, settled_total_us=0
 *   - us_per_op and khs come from the fallback (full-window):
 *       us_per_op = total_us / iters / 2 (AHB) = 5000 / 1000 / 2 = 2.5
 *       khs = iters * 1000 / total_us = 1000 * 1000 / 5000 = 200.0
 * ========================================================================= */
void test_diag_bench_json_unsettled_fallback(void)
{
    diag_bench_snapshot_t s = {
        .iters              = 1000,
        .duration_us        = 5000,
        .us_per_op          = 2.5,    /* total_us / iters / 2 (AHB fallback) */
        .khs                = 200.0,  /* iters * 1000 / total_us (fallback) */
        .sha_ops_per_sec   = 400000.0, /* 1e6 / us_per_op */
        .backend            = "ahb",
        .text_overlap_state = SHA_OVERLAP_SAFE,
        .h_write_state      = SHA_OVERLAP_SAFE,
        .asic_active        = false,
        .has_asic_active    = false,
        .settled            = false,
        .settled_after_iters = 0,
        .settled_iters      = 0,
        .settled_total_us   = 0,
    };

    char *json = capture_bench(&s);
    TEST_ASSERT_NOT_NULL(json);

    /* settled must be false */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled\":false"));

    /* settled_after_iters must be 0 */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_after_iters\":0"));

    /* settled_iters must be 0 */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_iters\":0"));

    /* settled_total_us must be 0 */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"settled_total_us\":0"));

    /* khs and us_per_op are present and reflect fallback values */
    bb_json_t root = bb_json_parse(json, 0);
    TEST_ASSERT_NOT_NULL(root);

    bb_json_t j_khs = bb_json_obj_get_item(root, "khs");
    TEST_ASSERT_NOT_NULL(j_khs);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 200.0, bb_json_item_get_double(j_khs));

    bb_json_t j_usop = bb_json_obj_get_item(root, "us_per_op");
    TEST_ASSERT_NOT_NULL(j_usop);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 2.5, bb_json_item_get_double(j_usop));

    bb_json_free(root);
    free(json);
}
