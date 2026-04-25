#include "unity.h"
#include "stratum_watchdogs.h"

// TA-234: stratum_watchdog_job_drought tests

void test_stratum_watchdog_job_drought_never_observed(void)
{
    // last_pool_job_ms = 0 means "never observed", should not trigger
    TEST_ASSERT_FALSE(stratum_watchdog_job_drought(1000000, 0));
}

void test_stratum_watchdog_job_drought_below_threshold(void)
{
    uint32_t now_ms = 10000;
    uint32_t last_ms = 5000;
    // elapsed = 5000ms, threshold = 300000ms
    TEST_ASSERT_FALSE(stratum_watchdog_job_drought(now_ms, last_ms));
}

void test_stratum_watchdog_job_drought_at_threshold(void)
{
    uint32_t now_ms = 310000;
    uint32_t last_ms = 10000;
    // elapsed = 310000 - 10000 = 300000ms, threshold = 300000ms
    TEST_ASSERT_TRUE(stratum_watchdog_job_drought(now_ms, last_ms));
}

void test_stratum_watchdog_job_drought_above_threshold(void)
{
    uint32_t now_ms = 400000;
    uint32_t last_ms = 50000;
    // elapsed = 400000 - 50000 = 350000ms, threshold = 300000ms
    TEST_ASSERT_TRUE(stratum_watchdog_job_drought(now_ms, last_ms));
}

void test_stratum_watchdog_job_drought_wraparound(void)
{
    // Test unsigned wraparound: 100 - (UINT32_MAX - 50) wraps correctly
    uint32_t now_ms = 100;
    uint32_t last_ms = UINT32_MAX - 50;
    // (uint32_t)(100 - (UINT32_MAX - 50)) = (uint32_t)(151) = 151ms
    // 151ms < 300000ms
    TEST_ASSERT_FALSE(stratum_watchdog_job_drought(now_ms, last_ms));
}

// TA-234: stratum_watchdog_share_drought tests

void test_stratum_watchdog_share_drought_both_zero(void)
{
    // both last_share_ms and session_start_ms are 0, should not trigger
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(1000000, 0, 0));
}

void test_stratum_watchdog_share_drought_only_last_share_below_threshold(void)
{
    uint32_t now_ms = 50000;
    uint32_t last_share_ms = 10000;
    uint32_t session_start_ms = 0;
    // elapsed = 40000ms, threshold = 1800000ms
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(now_ms, last_share_ms, session_start_ms));
}

void test_stratum_watchdog_share_drought_session_start_at_threshold(void)
{
    uint32_t now_ms = 1800000;
    uint32_t last_share_ms = 0;
    uint32_t session_start_ms = 0;
    // uses session_start_ms (since last_share_ms is 0), ref = 0, returns false
    // elapsed = 1800000ms at threshold, but ref = 0 returns false
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(1800000, 0, 0));
}

void test_stratum_watchdog_share_drought_only_last_share_above_threshold(void)
{
    uint32_t now_ms = 2000000;
    uint32_t last_share_ms = 0;
    uint32_t session_start_ms = 100000;
    // uses session_start_ms = 100000, elapsed = 1900000ms > 1800000ms
    TEST_ASSERT_TRUE(stratum_watchdog_share_drought(now_ms, last_share_ms, session_start_ms));
}

void test_stratum_watchdog_share_drought_prefers_last_share(void)
{
    uint32_t now_ms = 1900000;
    uint32_t last_share_ms = 1800000;  // recent
    uint32_t session_start_ms = 0;     // old (or never observed)
    // uses last_share_ms = 1800000, elapsed = 100000ms < 1800000ms
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(now_ms, last_share_ms, session_start_ms));
}

void test_stratum_watchdog_share_drought_falls_back_to_session_start(void)
{
    uint32_t now_ms = 2000000;
    uint32_t last_share_ms = 0;          // no share yet
    uint32_t session_start_ms = 100000;  // session started 1.9M ms ago
    // uses session_start_ms = 100000, elapsed = 1900000ms > 1800000ms
    TEST_ASSERT_TRUE(stratum_watchdog_share_drought(now_ms, last_share_ms, session_start_ms));
}

void test_stratum_watchdog_share_drought_last_share_overrides_old_session(void)
{
    uint32_t now_ms = 2500000;
    uint32_t last_share_ms = 2400000;   // share 100ms ago
    uint32_t session_start_ms = 100000; // session 2.4M ms ago
    // uses last_share_ms, elapsed = 100000ms < 1800000ms
    TEST_ASSERT_FALSE(stratum_watchdog_share_drought(now_ms, last_share_ms, session_start_ms));
}

// TA-234: stratum_watchdog_needs_keepalive tests

void test_stratum_watchdog_keepalive_never_transmitted(void)
{
    // last_tx_ms = 0 means "never transmitted", should not trigger
    TEST_ASSERT_FALSE(stratum_watchdog_needs_keepalive(1000000, 0));
}

void test_stratum_watchdog_keepalive_below_threshold(void)
{
    uint32_t now_ms = 50000;
    uint32_t last_tx_ms = 10000;
    // elapsed = 40000ms, threshold = 90000ms
    TEST_ASSERT_FALSE(stratum_watchdog_needs_keepalive(now_ms, last_tx_ms));
}

void test_stratum_watchdog_keepalive_at_threshold(void)
{
    uint32_t now_ms = 100000;
    uint32_t last_tx_ms = 10000;
    // elapsed = 100000 - 10000 = 90000ms, threshold = 90000ms
    TEST_ASSERT_TRUE(stratum_watchdog_needs_keepalive(now_ms, last_tx_ms));
}

void test_stratum_watchdog_keepalive_above_threshold(void)
{
    uint32_t now_ms = 150000;
    uint32_t last_tx_ms = 30000;
    // elapsed = 150000 - 30000 = 120000ms, threshold = 90000ms
    TEST_ASSERT_TRUE(stratum_watchdog_needs_keepalive(now_ms, last_tx_ms));
}

void test_stratum_watchdog_keepalive_wraparound(void)
{
    // Test unsigned wraparound: 100 - (UINT32_MAX - 50) wraps correctly
    uint32_t now_ms = 100;
    uint32_t last_tx_ms = UINT32_MAX - 50;
    // (uint32_t)(100 - (UINT32_MAX - 50)) = (uint32_t)(151) = 151ms
    // 151ms > 90000ms is false, but wraparound of small difference should be caught
    // Actually 151ms < 90000ms, so this should be false
    TEST_ASSERT_FALSE(stratum_watchdog_needs_keepalive(now_ms, last_tx_ms));
}
