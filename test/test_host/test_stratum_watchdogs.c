#include "unity.h"
#include "stratum_watchdogs.h"

void test_stratum_watchdog_job_drought_never_observed(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_job_drought(1000000, 0));
}

void test_stratum_watchdog_job_drought_below_threshold(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_job_drought(10000, 5000));
}

void test_stratum_watchdog_job_drought_at_threshold(void)
{
    TEST_ASSERT_TRUE(stratum_watchdog_job_drought(310000, 10000));
}

void test_stratum_watchdog_job_drought_above_threshold(void)
{
    TEST_ASSERT_TRUE(stratum_watchdog_job_drought(400000, 50000));
}

void test_stratum_watchdog_job_drought_wraparound(void)
{
    uint32_t now_ms = 100;
    uint32_t last_ms = UINT32_MAX - 50;
    TEST_ASSERT_FALSE(stratum_watchdog_job_drought(now_ms, last_ms));
}

void test_stratum_watchdog_share_drought_both_zero(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(1000000, 0, 0));
}

void test_stratum_watchdog_share_drought_only_last_share_below_threshold(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(50000, 10000, 0));
}

void test_stratum_watchdog_share_drought_session_start_at_threshold(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(1800000, 0, 0));
}

void test_stratum_watchdog_share_drought_only_last_share_above_threshold(void)
{
    TEST_ASSERT_TRUE(stratum_watchdog_share_drought(2000000, 0, 100000));
}

void test_stratum_watchdog_share_drought_prefers_last_share(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(1900000, 1800000, 0));
}

void test_stratum_watchdog_share_drought_falls_back_to_session_start(void)
{
    TEST_ASSERT_TRUE(stratum_watchdog_share_drought(2000000, 0, 100000));
}

void test_stratum_watchdog_share_drought_last_share_overrides_old_session(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(2500000, 2400000, 100000));
}

void test_stratum_watchdog_keepalive_never_transmitted(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_needs_keepalive(1000000, 0));
}

void test_stratum_watchdog_keepalive_below_threshold(void)
{
    TEST_ASSERT_FALSE(stratum_watchdog_needs_keepalive(50000, 10000));
}

void test_stratum_watchdog_keepalive_at_threshold(void)
{
    TEST_ASSERT_TRUE(stratum_watchdog_needs_keepalive(100000, 10000));
}

void test_stratum_watchdog_keepalive_above_threshold(void)
{
    TEST_ASSERT_TRUE(stratum_watchdog_needs_keepalive(150000, 30000));
}

void test_stratum_watchdog_keepalive_wraparound(void)
{
    uint32_t now_ms = 100;
    uint32_t last_tx_ms = UINT32_MAX - 50;
    TEST_ASSERT_FALSE(stratum_watchdog_needs_keepalive(now_ms, last_tx_ms));
}
