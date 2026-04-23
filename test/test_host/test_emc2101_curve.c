#include "unity.h"
#include "emc2101_curve.h"

// Below 50°C floor → minimum duty
void test_curve_below_lower_bound(void) {
    TEST_ASSERT_EQUAL_INT(30, emc2101_duty_for_temp_c(35.0f));
}

// At 50°C floor exactly → minimum duty
void test_curve_at_lower_bound(void) {
    TEST_ASSERT_EQUAL_INT(30, emc2101_duty_for_temp_c(50.0f));
}

// 55°C in 50–60°C segment → 30 + 5*1.0 = 35%
void test_curve_mid_segment_one(void) {
    TEST_ASSERT_EQUAL_INT(35, emc2101_duty_for_temp_c(55.0f));
}

// At 60°C boundary → 40% (matches AxeOS at identical hw)
void test_curve_at_60c(void) {
    TEST_ASSERT_EQUAL_INT(40, emc2101_duty_for_temp_c(60.0f));
}

// Midpoint of 60–75°C → 40 + 7.5*2.0 = 55%
void test_curve_mid_segment_two(void) {
    TEST_ASSERT_EQUAL_INT(55, emc2101_duty_for_temp_c(67.5f));
}

// At 75°C boundary → 70%
void test_curve_at_75c(void) {
    TEST_ASSERT_EQUAL_INT(70, emc2101_duty_for_temp_c(75.0f));
}

// Midpoint of 75–85°C → 70 + 5*3.0 = 85%
void test_curve_mid_segment_three(void) {
    TEST_ASSERT_EQUAL_INT(85, emc2101_duty_for_temp_c(80.0f));
}

// At 85°C upper bound → 100%
void test_curve_at_upper_bound(void) {
    TEST_ASSERT_EQUAL_INT(100, emc2101_duty_for_temp_c(85.0f));
}

// Above 85°C → 100%
void test_curve_above_upper_bound(void) {
    TEST_ASSERT_EQUAL_INT(100, emc2101_duty_for_temp_c(90.0f));
}

// Negative temperature → minimum duty (fail-safe low: hardware is cold)
void test_curve_negative_temp(void) {
    TEST_ASSERT_EQUAL_INT(30, emc2101_duty_for_temp_c(-10.0f));
}

// Very high temperature → 100%
void test_curve_very_high_temp(void) {
    TEST_ASSERT_EQUAL_INT(100, emc2101_duty_for_temp_c(150.0f));
}

// Monotonic non-decreasing across the full range
void test_curve_monotonic(void) {
    float points[] = {35.0f, 50.0f, 55.0f, 60.0f, 67.5f, 75.0f, 80.0f, 85.0f, 90.0f};
    int n = (int)(sizeof(points) / sizeof(points[0]));
    int prev = emc2101_duty_for_temp_c(points[0]);
    for (int i = 1; i < n; i++) {
        int cur = emc2101_duty_for_temp_c(points[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(prev, cur);
        prev = cur;
    }
}
