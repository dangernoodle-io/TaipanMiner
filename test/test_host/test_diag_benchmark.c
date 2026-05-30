/*
 * test_diag_benchmark.c — host unit tests for TA-33 /api/diag/benchmark.
 *
 * Tests cover:
 *  - diag_bench_parse_request: bounds validation and default handling
 *  - build_diag_bench_json: response JSON shape (exact keys)
 *
 * The full HTTP handler (diag_benchmark_handler) lives in routes.c which is
 * ESP-only and not compiled for the native env.  The parsing and response-
 * building helpers in routes_json.c are host-compilable and tested here.
 */

#include "unity.h"
#include "routes_json.h"
#include "bb_json.h"
#include "bb_core.h"
#include <string.h>
#include <stdlib.h>

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
 * build_diag_bench_json response shape tests
 * ========================================================================= */

/* Helper: serialize snapshot and free root; returns heap string (caller frees) */
static char *build_and_serialize(const diag_bench_snapshot_t *s)
{
    bb_json_t root = bb_json_obj_new();
    build_diag_bench_json(s, root);
    char *json = bb_json_serialize(root);
    bb_json_free(root);
    return json;
}

/* Happy path — non-ASIC "sw" backend: verify all required keys present */
void test_diag_bench_json_shape_sw_backend(void)
{
    diag_bench_snapshot_t s = {
        .iters           = 10000,
        .duration_us     = 8275,
        .us_per_op       = 0.8275,
        .khs             = 1208.2,
        .backend         = "sw",
        .text_overlap_ok = true,
        .h_write_ok      = true,
        .asic_active     = false,
        .has_asic_active = false,
    };

    char *json = build_and_serialize(&s);
    TEST_ASSERT_NOT_NULL(json);

    /* Must contain all required top-level keys */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"iters\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"duration_us\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"us_per_op\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"khs\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"backend\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"canary\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text_overlap_ok\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write_ok\""));

    /* Backend name is "sw" */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sw\""));

    /* asic_active must NOT be present when has_asic_active is false */
    TEST_ASSERT_NULL(strstr(json, "\"asic_active\""));

    /* canary values: true */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text_overlap_ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write_ok\":true"));

    bb_json_free_str(json);
}

/* ahb backend */
void test_diag_bench_json_shape_ahb_backend(void)
{
    diag_bench_snapshot_t s = {
        .iters           = 5000,
        .duration_us     = 20000,
        .us_per_op       = 4.0,
        .khs             = 250.0,
        .backend         = "ahb",
        .text_overlap_ok = true,
        .h_write_ok      = false,
        .asic_active     = false,
        .has_asic_active = false,
    };

    char *json = build_and_serialize(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"ahb\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"h_write_ok\":false"));
    bb_json_free_str(json);
}

/* dport backend */
void test_diag_bench_json_shape_dport_backend(void)
{
    diag_bench_snapshot_t s = {
        .iters           = 1000,
        .duration_us     = 15000,
        .us_per_op       = 15.0,
        .khs             = 66.7,
        .backend         = "dport",
        .text_overlap_ok = false,
        .h_write_ok      = false,
        .asic_active     = false,
        .has_asic_active = false,
    };

    char *json = build_and_serialize(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"dport\""));
    bb_json_free_str(json);
}

/* asic_active field present when has_asic_active is true */
void test_diag_bench_json_asic_active_present_when_flagged(void)
{
    diag_bench_snapshot_t s = {
        .iters           = 10000,
        .duration_us     = 1000,
        .us_per_op       = 0.1,
        .khs             = 10000.0,
        .backend         = "ahb",
        .text_overlap_ok = true,
        .h_write_ok      = true,
        .asic_active     = true,
        .has_asic_active = true,
    };

    char *json = build_and_serialize(&s);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"asic_active\":true"));
    bb_json_free_str(json);
}
