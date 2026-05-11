#include "unity.h"
#include "bb_nv.h"
#include "config.h"
#include <stdbool.h>

// Config knot enabled setters — getter round-trip

void test_set_knot_enabled_round_trip(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    TEST_ASSERT_EQUAL(0, config_set_knot_enabled(true));
    TEST_ASSERT_TRUE(config_knot_enabled());
    TEST_ASSERT_EQUAL(0, config_set_knot_enabled(false));
    TEST_ASSERT_FALSE(config_knot_enabled());
}

void test_knot_enabled_default_true(void)
{
    TEST_ASSERT_EQUAL(0, bb_nv_config_init());
    TEST_ASSERT_EQUAL(0, config_init());
    // Default is true when nothing persisted
    TEST_ASSERT_TRUE(config_knot_enabled());
}

void test_set_knot_enabled_to_true(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_knot_enabled(true));
    TEST_ASSERT_TRUE(config_knot_enabled());
}

void test_set_knot_enabled_to_false(void)
{
    bb_nv_config_init();
    config_init();
    TEST_ASSERT_EQUAL(0, config_set_knot_enabled(false));
    TEST_ASSERT_FALSE(config_knot_enabled());
}
