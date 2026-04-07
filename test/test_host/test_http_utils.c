#include "unity.h"
#include "http_server.h"
#include <string.h>

void test_url_decode_basic(void)
{
    char out[256];
    url_decode_field("ssid=TestNetwork&pass=secret", "ssid", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("TestNetwork", out);
}

void test_url_decode_plus_as_space(void)
{
    char out[256];
    url_decode_field("ssid=Test+Network", "ssid", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Test Network", out);
}

void test_url_decode_hex_decode(void)
{
    char out[256];
    url_decode_field("pass=hello%21world", "pass", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("hello!world", out);
}

void test_url_decode_missing_field(void)
{
    char out[256];
    url_decode_field("ssid=TestNetwork", "pass", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_url_decode_truncation(void)
{
    char out[5];
    url_decode_field("ssid=abcdefghij", "ssid", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("abcd", out);
}

void test_url_decode_percent_at_end(void)
{
    char out[256];
    url_decode_field("f=abc%2", "f", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("abc%2", out);
}

void test_url_decode_field_not_first(void)
{
    char out[256];
    url_decode_field("a=1&b=2&c=3", "b", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2", out);
}

void test_url_decode_empty_value(void)
{
    char out[256];
    url_decode_field("ssid=&pass=secret", "ssid", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_url_decode_field_at_end(void)
{
    char out[256];
    url_decode_field("a=1&b=2", "b", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2", out);
}
