#include "unity.h"
#include "stratum_machine.h"
#include <string.h>

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
