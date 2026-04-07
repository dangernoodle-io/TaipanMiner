#include "unity.h"
#include "stratum_utils.h"
#include <string.h>

// Tests for format_submit_params
void test_format_submit_no_version(void)
{
    char buf[256];
    int result = format_submit_params(buf, sizeof(buf),
                                      "tk-test-000", "test-worker",
                                      "abc123", "deadbeef",
                                      "12345678", "abcdef00",
                                      "");
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[\"tk-test-000.test-worker\",\"abc123\",\"deadbeef\",\"12345678\",\"abcdef00\"]", buf);
}

void test_format_submit_with_version(void)
{
    char buf[256];
    int result = format_submit_params(buf, sizeof(buf),
                                      "tk-test-000", "test-worker",
                                      "abc123", "deadbeef",
                                      "12345678", "abcdef00",
                                      "20000000");
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[\"tk-test-000.test-worker\",\"abc123\",\"deadbeef\",\"12345678\",\"abcdef00\",\"20000000\"]", buf);
}

void test_format_submit_null_version(void)
{
    char buf[256];
    int result = format_submit_params(buf, sizeof(buf),
                                      "tk-test-000", "test-worker",
                                      "abc123", "deadbeef",
                                      "12345678", "abcdef00",
                                      NULL);
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("[\"tk-test-000.test-worker\",\"abc123\",\"deadbeef\",\"12345678\",\"abcdef00\"]", buf);
}

void test_format_submit_truncation(void)
{
    char buf[10];
    int result = format_submit_params(buf, sizeof(buf),
                                      "tk-test-000", "test-worker",
                                      "abc123", "deadbeef",
                                      "12345678", "abcdef00",
                                      "");
    TEST_ASSERT_EQUAL_INT(-1, result);
}

// Tests for format_stratum_request
void test_format_request_basic(void)
{
    char buf[256];
    char params[] = "[\"TaipanMiner/0.1\"]";
    int result = format_stratum_request(buf, sizeof(buf),
                                        1, "mining.subscribe", params);
    TEST_ASSERT_GREATER_THAN(-1, result);
    TEST_ASSERT_EQUAL_STRING("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"TaipanMiner/0.1\"]}\n", buf);
}

void test_format_request_truncation(void)
{
    char buf[10];
    char params[] = "[\"TaipanMiner/0.1\"]";
    int result = format_stratum_request(buf, sizeof(buf),
                                        1, "mining.subscribe", params);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_format_request_newline(void)
{
    char buf[256];
    char params[] = "[]";
    int result = format_stratum_request(buf, sizeof(buf),
                                        2, "mining.submit", params);
    TEST_ASSERT_GREATER_THAN(-1, result);
    // Verify string ends with newline
    TEST_ASSERT_EQUAL_CHAR('\n', buf[result - 1]);
}
