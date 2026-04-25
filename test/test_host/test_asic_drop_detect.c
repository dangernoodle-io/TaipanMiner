#include "unity.h"
#include "asic_drop_detect.h"

#define COOLDOWN_US (30ULL * 1000 * 1000)  // 30s, matches WARN_COOLDOWN_US

void test_drop_detect_accepts_below_sanity(void)
{
    asic_drop_detect_step_t step = asic_drop_detect_evaluate(
        500.0f, 2000.0f, 100ULL * 1000 * 1000, 0, COOLDOWN_US);
    TEST_ASSERT_TRUE(step.accept);
    TEST_ASSERT_FALSE(step.should_warn);
}

void test_drop_detect_rejects_at_or_above_sanity(void)
{
    /* exactly equal — sanity check is strict less-than */
    asic_drop_detect_step_t s1 = asic_drop_detect_evaluate(
        2000.0f, 2000.0f, 100ULL * 1000 * 1000, 0, COOLDOWN_US);
    TEST_ASSERT_FALSE(s1.accept);

    /* above */
    asic_drop_detect_step_t s2 = asic_drop_detect_evaluate(
        9999.0f, 2000.0f, 100ULL * 1000 * 1000, 0, COOLDOWN_US);
    TEST_ASSERT_FALSE(s2.accept);
}

void test_drop_detect_first_warn_fires_immediately(void)
{
    /* last_warn_us = 0 (never warned before), now = anything → cooldown elapsed */
    asic_drop_detect_step_t step = asic_drop_detect_evaluate(
        9999.0f, 2000.0f, 100ULL * 1000 * 1000, 0, COOLDOWN_US);
    TEST_ASSERT_FALSE(step.accept);
    TEST_ASSERT_TRUE(step.should_warn);
    TEST_ASSERT_EQUAL_UINT64(100ULL * 1000 * 1000, step.new_last_warn_us);
}

void test_drop_detect_warn_cooldown_suppresses(void)
{
    uint64_t last = 100ULL * 1000 * 1000;
    /* now = last + 5s, cooldown = 30s → still in window */
    uint64_t now = last + 5ULL * 1000 * 1000;
    asic_drop_detect_step_t step = asic_drop_detect_evaluate(
        9999.0f, 2000.0f, now, last, COOLDOWN_US);
    TEST_ASSERT_FALSE(step.accept);
    TEST_ASSERT_FALSE(step.should_warn);
    TEST_ASSERT_EQUAL_UINT64(last, step.new_last_warn_us);  /* unchanged */
}

void test_drop_detect_warn_after_cooldown_elapsed(void)
{
    uint64_t last = 100ULL * 1000 * 1000;
    /* now = last + 30s exactly → cooldown elapsed */
    uint64_t now = last + COOLDOWN_US;
    asic_drop_detect_step_t step = asic_drop_detect_evaluate(
        9999.0f, 2000.0f, now, last, COOLDOWN_US);
    TEST_ASSERT_FALSE(step.accept);
    TEST_ASSERT_TRUE(step.should_warn);
    TEST_ASSERT_EQUAL_UINT64(now, step.new_last_warn_us);
}

void test_drop_detect_domain_smaller_cap(void)
{
    /* Per-domain cap is 500 (¼ of chip 2000). 600 GH/s on a domain → reject. */
    asic_drop_detect_step_t step = asic_drop_detect_evaluate(
        600.0f, 500.0f, 100ULL * 1000 * 1000, 0, COOLDOWN_US);
    TEST_ASSERT_FALSE(step.accept);
    TEST_ASSERT_TRUE(step.should_warn);
}

void test_drop_detect_zero_cooldown_always_warns(void)
{
    /* edge case: 0 cooldown → every reject warns */
    uint64_t last = 100;
    uint64_t now = 100;  /* same instant */
    asic_drop_detect_step_t step = asic_drop_detect_evaluate(
        9999.0f, 2000.0f, now, last, 0);
    TEST_ASSERT_FALSE(step.accept);
    TEST_ASSERT_TRUE(step.should_warn);
    TEST_ASSERT_EQUAL_UINT64(now, step.new_last_warn_us);
}
