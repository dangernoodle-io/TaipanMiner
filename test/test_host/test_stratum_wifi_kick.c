#include "unity.h"
#include "stratum_machine.h"
#include <stdbool.h>

// TA-440: unit tests for stratum_should_kick_wifi()
// The function is a pure decision helper — no sockets, no FreeRTOS.

// Below threshold: never kick regardless of IP state
void test_wifi_kick_below_threshold_no_ip(void)
{
    TEST_ASSERT_FALSE(stratum_should_kick_wifi(STRATUM_WIFI_KICK_THRESHOLD - 1, false));
}

void test_wifi_kick_below_threshold_has_ip(void)
{
    TEST_ASSERT_FALSE(stratum_should_kick_wifi(STRATUM_WIFI_KICK_THRESHOLD - 1, true));
}

void test_wifi_kick_zero_count_no_ip(void)
{
    TEST_ASSERT_FALSE(stratum_should_kick_wifi(0, false));
}

void test_wifi_kick_zero_count_has_ip(void)
{
    TEST_ASSERT_FALSE(stratum_should_kick_wifi(0, true));
}

// At threshold: only kick when IP is up (zombie-connected state)
void test_wifi_kick_at_threshold_has_ip(void)
{
    TEST_ASSERT_TRUE(stratum_should_kick_wifi(STRATUM_WIFI_KICK_THRESHOLD, true));
}

void test_wifi_kick_at_threshold_no_ip(void)
{
    // WiFi is down — don't kick; the FSM will handle it
    TEST_ASSERT_FALSE(stratum_should_kick_wifi(STRATUM_WIFI_KICK_THRESHOLD, false));
}

// Above threshold: same gating applies
void test_wifi_kick_above_threshold_has_ip(void)
{
    TEST_ASSERT_TRUE(stratum_should_kick_wifi(STRATUM_WIFI_KICK_THRESHOLD + 3, true));
}

void test_wifi_kick_above_threshold_no_ip(void)
{
    TEST_ASSERT_FALSE(stratum_should_kick_wifi(STRATUM_WIFI_KICK_THRESHOLD + 3, false));
}
