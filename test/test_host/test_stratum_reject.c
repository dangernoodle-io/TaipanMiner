// Ported to the bb_serialize_json tok-recorder scanner (stratum_parse_error_
// code now takes a scanned rec+idx rather than a bb_json_t error item; see
// stratum_machine.h).
#include "unity.h"
#include "stratum_machine.h"
#include "bb_serialize_json.h"
#include <string.h>

#define TEST_TOK_POOL_CAP 16

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

void test_parse_error_code_array_form_21(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t error = scan_json("[21,\"Job not found\",\"\"]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, error);

    int code = stratum_parse_error_code(&rec, error);
    TEST_ASSERT_EQUAL_INT(21, code);
}

void test_parse_error_code_array_form_23(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t error = scan_json("[23,\"Low difficulty share\",\"\"]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, error);

    int code = stratum_parse_error_code(&rec, error);
    TEST_ASSERT_EQUAL_INT(23, code);
}

void test_parse_error_code_object_form_22(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t error = scan_json("{\"code\":22,\"message\":\"Duplicate share\"}", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, error);

    int code = stratum_parse_error_code(&rec, error);
    TEST_ASSERT_EQUAL_INT(22, code);
}

void test_parse_error_code_object_form_25(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t error = scan_json("{\"code\":25,\"message\":\"Stale prevhash\"}", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, error);

    int code = stratum_parse_error_code(&rec, error);
    TEST_ASSERT_EQUAL_INT(25, code);
}

void test_parse_error_code_empty_array(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t error = scan_json("[]", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, error);

    int code = stratum_parse_error_code(&rec, error);
    TEST_ASSERT_EQUAL_INT(-1, code);
}

void test_parse_error_code_no_code_field(void)
{
    bb_serialize_json_tok_t pool[TEST_TOK_POOL_CAP];
    bb_serialize_json_tok_recorder_t rec;
    bb_serialize_json_tok_idx_t error = scan_json("{\"message\":\"Some error\"}", &rec, pool);
    TEST_ASSERT_NOT_EQUAL(BB_SERIALIZE_JSON_TOK_ABSENT, error);

    int code = stratum_parse_error_code(&rec, error);
    TEST_ASSERT_EQUAL_INT(-1, code);
}

void test_parse_error_code_null_recorder(void)
{
    int code = stratum_parse_error_code(NULL, BB_SERIALIZE_JSON_TOK_ABSENT);
    TEST_ASSERT_EQUAL_INT(-1, code);
}

void test_classify_reject_job_not_found(void)
{
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_JOB_NOT_FOUND, stratum_machine_classify_reject(21));
}

void test_classify_reject_duplicate(void)
{
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_DUPLICATE, stratum_machine_classify_reject(22));
}

void test_classify_reject_low_difficulty(void)
{
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_LOW_DIFFICULTY, stratum_machine_classify_reject(23));
}

void test_classify_reject_stale_prevhash(void)
{
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_STALE_PREVHASH, stratum_machine_classify_reject(25));
}

void test_classify_reject_unknown_code(void)
{
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, stratum_machine_classify_reject(99));
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, stratum_machine_classify_reject(-1));
    TEST_ASSERT_EQUAL_INT(STRATUM_REJECT_OTHER, stratum_machine_classify_reject(0));
}
