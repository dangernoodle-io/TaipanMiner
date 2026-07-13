#include "unity.h"
#include "stratum_utils.h"
#include "stratum_machine.h"
#include "bb_json.h"

void test_parse_error_code_array_form_21(void)
{
    bb_json_t error = bb_json_arr_new();
    bb_json_arr_append_number(error, 21.0);
    bb_json_arr_append_string(error, "Job not found");
    bb_json_arr_append_string(error, "");

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(21, code);

    bb_json_free(error);
}

void test_parse_error_code_array_form_23(void)
{
    bb_json_t error = bb_json_arr_new();
    bb_json_arr_append_number(error, 23.0);
    bb_json_arr_append_string(error, "Low difficulty share");
    bb_json_arr_append_string(error, "");

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(23, code);

    bb_json_free(error);
}

void test_parse_error_code_object_form_22(void)
{
    bb_json_t error = bb_json_obj_new();
    bb_json_obj_set_number(error, "code", 22.0);
    bb_json_obj_set_string(error, "message", "Duplicate share");

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(22, code);

    bb_json_free(error);
}

void test_parse_error_code_object_form_25(void)
{
    bb_json_t error = bb_json_obj_new();
    bb_json_obj_set_number(error, "code", 25.0);
    bb_json_obj_set_string(error, "message", "Stale prevhash");

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(25, code);

    bb_json_free(error);
}

void test_parse_error_code_empty_array(void)
{
    bb_json_t error = bb_json_arr_new();

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(-1, code);

    bb_json_free(error);
}

void test_parse_error_code_no_code_field(void)
{
    bb_json_t error = bb_json_obj_new();
    bb_json_obj_set_string(error, "message", "Some error");

    int code = stratum_parse_error_code(error);
    TEST_ASSERT_EQUAL_INT(-1, code);

    bb_json_free(error);
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
