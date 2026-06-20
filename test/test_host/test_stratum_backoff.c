#include "unity.h"
#include "stratum_backoff.h"

void test_stratum_backoff_init(void)
{
    stratum_backoff_t b;
    stratum_backoff_init(&b);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_INITIAL_MS, b.delay_ms);
    TEST_ASSERT_EQUAL_INT(0, b.fail_count);
}

void test_stratum_backoff_first_fail_sleeps_initial_then_doubles(void)
{
    stratum_backoff_t b;
    stratum_backoff_init(&b);
    stratum_backoff_step_t s = stratum_backoff_on_fail(&b);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_INITIAL_MS, s.sleep_ms);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_INITIAL_MS * 2, b.delay_ms);
    TEST_ASSERT_EQUAL_INT(1, b.fail_count);
}

void test_stratum_backoff_progression_doubles_to_cap(void)
{
    stratum_backoff_t b;
    stratum_backoff_init(&b);

    /* fail 1: sleep 5s, next=10s */
    TEST_ASSERT_EQUAL_UINT32(5000, stratum_backoff_on_fail(&b).sleep_ms);
    /* fail 2: sleep 10s, next=20s */
    TEST_ASSERT_EQUAL_UINT32(10000, stratum_backoff_on_fail(&b).sleep_ms);
    /* fail 3: sleep 20s, next=40s */
    TEST_ASSERT_EQUAL_UINT32(20000, stratum_backoff_on_fail(&b).sleep_ms);
    /* fail 4: sleep 40s, next=60s (cap) */
    TEST_ASSERT_EQUAL_UINT32(40000, stratum_backoff_on_fail(&b).sleep_ms);
    TEST_ASSERT_EQUAL_INT(4, b.fail_count);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_CAP_MS, b.delay_ms);

    /* fail 5 and beyond: still bumps fail_count, sleeps at cap */
    stratum_backoff_step_t s = stratum_backoff_on_fail(&b);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_CAP_MS, s.sleep_ms);
    TEST_ASSERT_EQUAL_INT(5, b.fail_count);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_CAP_MS, b.delay_ms);

    /* fail 6: still capped, fail_count continues to increment */
    s = stratum_backoff_on_fail(&b);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_CAP_MS, s.sleep_ms);
    TEST_ASSERT_EQUAL_INT(6, b.fail_count);
}

void test_stratum_backoff_caps_at_60s(void)
{
    stratum_backoff_t b;
    stratum_backoff_init(&b);
    b.delay_ms = STRATUM_BACKOFF_CAP_MS;
    b.fail_count = 1;
    stratum_backoff_step_t s = stratum_backoff_on_fail(&b);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_CAP_MS, s.sleep_ms);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_CAP_MS, b.delay_ms);
}

void test_stratum_backoff_reset_on_success(void)
{
    stratum_backoff_t b;
    b.delay_ms = 40000;
    b.fail_count = 3;
    stratum_backoff_reset(&b);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_INITIAL_MS, b.delay_ms);
    TEST_ASSERT_EQUAL_INT(0, b.fail_count);
}

void test_stratum_backoff_reset_restarts_doubling(void)
{
    /* After many failures followed by a successful reconnect (reset),
       the next failure stream restarts at 5000. */
    stratum_backoff_t b;
    stratum_backoff_init(&b);
    for (int i = 0; i < 6; i++) stratum_backoff_on_fail(&b);
    /* simulate successful reconnect */
    stratum_backoff_reset(&b);
    /* fresh fail */
    stratum_backoff_step_t s = stratum_backoff_on_fail(&b);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_BACKOFF_INITIAL_MS, s.sleep_ms);
    TEST_ASSERT_EQUAL_INT(1, b.fail_count);
}
