#include "unity.h"
#include "log_stream.h"
#include <string.h>
#include <stdarg.h>

static int call_format(char *buf, size_t len, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int result = log_stream_format(buf, len, fmt, args);
    va_end(args);
    return result;
}

void test_log_stream_format_basic(void)
{
    char buf[64];
    int n = call_format(buf, sizeof(buf), "hello %s", "world");
    TEST_ASSERT_EQUAL_STRING("hello world", buf);
    TEST_ASSERT_EQUAL_INT(11, n);
}

void test_log_stream_format_truncation(void)
{
    char buf[8];
    int n = call_format(buf, sizeof(buf), "abcdefghijklmnop");
    TEST_ASSERT_EQUAL_INT(7, n);
    TEST_ASSERT_EQUAL_STRING("abcdefg", buf);
}

void test_log_stream_format_empty(void)
{
    char buf[16];
    int n = call_format(buf, sizeof(buf), "%s", "");
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_log_stream_format_null_buf(void)
{
    int n = call_format(NULL, 0, "test");
    TEST_ASSERT_EQUAL_INT(-1, n);
}

void test_log_stream_format_null_fmt(void)
{
    char buf[16];
    int n = log_stream_format(buf, sizeof(buf), NULL, (va_list){0});
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_log_stream_format_int(void)
{
    char buf[32];
    int n = call_format(buf, sizeof(buf), "count=%d", 42);
    TEST_ASSERT_EQUAL_STRING("count=42", buf);
    TEST_ASSERT_EQUAL_INT(8, n);
}

void test_log_stream_format_size_one(void)
{
    char buf[1];
    int n = call_format(buf, sizeof(buf), "hello");
    TEST_ASSERT_EQUAL_INT(0, n);
    TEST_ASSERT_EQUAL_UINT8('\0', buf[0]);
}
