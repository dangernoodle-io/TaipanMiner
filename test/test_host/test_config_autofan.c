#include "unity.h"
#include "bb_nv.h"
#include "config.h"
#include <stdbool.h>

// TA-315: autofan setters — clamping + getter round-trip

void test_set_autofan_enabled_round_trip(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    TEST_ASSERT_EQUAL(0, config_set_autofan_enabled(true));
    TEST_ASSERT_TRUE(config_autofan_enabled());
    TEST_ASSERT_EQUAL(0, config_set_autofan_enabled(false));
    TEST_ASSERT_FALSE(config_autofan_enabled());
}

void test_set_die_target_in_range(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_die_target_c(60));
    TEST_ASSERT_EQUAL(60, config_die_target_c());
}

void test_set_die_target_clamps_low(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_die_target_c(10));
    TEST_ASSERT_EQUAL(35, config_die_target_c());
}

void test_set_die_target_clamps_high(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_die_target_c(200));
    TEST_ASSERT_EQUAL(85, config_die_target_c());
}

void test_set_vr_target_in_range(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_vr_target_c(75));
    TEST_ASSERT_EQUAL(75, config_vr_target_c());
}

void test_set_vr_target_clamps_low(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_vr_target_c(10));
    TEST_ASSERT_EQUAL(50, config_vr_target_c());
}

void test_set_vr_target_clamps_high(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_vr_target_c(200));
    TEST_ASSERT_EQUAL(100, config_vr_target_c());
}

void test_set_manual_fan_pct_in_range(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_manual_fan_pct(75));
    TEST_ASSERT_EQUAL(75, config_manual_fan_pct());
}

void test_set_manual_fan_pct_clamps_high(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_manual_fan_pct(250));
    TEST_ASSERT_EQUAL(100, config_manual_fan_pct());
}

void test_set_min_fan_pct_in_range(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_min_fan_pct(25));
    TEST_ASSERT_EQUAL(25, config_min_fan_pct());
}

void test_set_min_fan_pct_clamps_high(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_min_fan_pct(150));
    TEST_ASSERT_EQUAL(100, config_min_fan_pct());
}
