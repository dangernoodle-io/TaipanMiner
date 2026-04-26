#include "unity.h"
#include "bb_nv.h"
#include "taipan_config.h"
#include <string.h>

void test_valid_hostname_single_char(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, taipan_config_init());
    TEST_ASSERT_EQUAL(0, taipan_config_set_hostname("a"));
    TEST_ASSERT_EQUAL_STRING("a", taipan_config_hostname());
}

void test_valid_hostname_lowercase_digits_hyphen(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_EQUAL(0, taipan_config_set_hostname("abc-def"));
    TEST_ASSERT_EQUAL_STRING("abc-def", taipan_config_hostname());
}

void test_valid_hostname_mixed(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_EQUAL(0, taipan_config_set_hostname("tdongles3-1"));
    TEST_ASSERT_EQUAL_STRING("tdongles3-1", taipan_config_hostname());
}

void test_valid_hostname_max_length(void)
{
    bb_nv_config_init();
    taipan_config_init();
    char hostname[33];
    memset(hostname, 'a', 32);
    hostname[32] = '\0';
    TEST_ASSERT_EQUAL(0, taipan_config_set_hostname(hostname));
    TEST_ASSERT_EQUAL_STRING(hostname, taipan_config_hostname());
}

void test_invalid_hostname_empty(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_NOT_EQUAL(0, taipan_config_set_hostname(""));
}

void test_invalid_hostname_leading_hyphen(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_NOT_EQUAL(0, taipan_config_set_hostname("-leading"));
}

void test_invalid_hostname_trailing_hyphen(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_NOT_EQUAL(0, taipan_config_set_hostname("trailing-"));
}

void test_invalid_hostname_uppercase(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_NOT_EQUAL(0, taipan_config_set_hostname("UPPER"));
}

void test_invalid_hostname_underscore(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_NOT_EQUAL(0, taipan_config_set_hostname("with_underscore"));
}

void test_invalid_hostname_dot(void)
{
    bb_nv_config_init();
    taipan_config_init();
    TEST_ASSERT_NOT_EQUAL(0, taipan_config_set_hostname("with.dot"));
}

void test_invalid_hostname_too_long(void)
{
    bb_nv_config_init();
    taipan_config_init();
    char hostname[35];
    memset(hostname, 'a', 33);
    hostname[33] = '\0';
    TEST_ASSERT_NOT_EQUAL(0, taipan_config_set_hostname(hostname));
}

void test_hostname_persists_after_set(void)
{
    bb_nv_config_init();
    // Set hostname and verify it persists
    TEST_ASSERT_EQUAL(0, taipan_config_set_hostname("miner-test-1"));
    // Read it back
    TEST_ASSERT_EQUAL_STRING("miner-test-1", taipan_config_hostname());
}
