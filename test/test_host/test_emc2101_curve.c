#include "unity.h"
#include "emc2101_curve.h"

// Below lower bound → minimum duty
void test_curve_below_lower_bound(void) {
    TEST_ASSERT_EQUAL_INT(30, emc2101_duty_for_temp_c(35.0f));
}

// At lower bound exactly
void test_curve_at_lower_bound(void) {
    TEST_ASSERT_EQUAL_INT(30, emc2101_duty_for_temp_c(40.0f));
}

// Midpoint of 40–60°C segment (50°C → 30 + 10*1.5 = 45%)
void test_curve_mid_segment_one(void) {
    TEST_ASSERT_EQUAL_INT(45, emc2101_duty_for_temp_c(50.0f));
}

// At 60°C boundary → 60%
void test_curve_at_segment_boundary(void) {
    TEST_ASSERT_EQUAL_INT(60, emc2101_duty_for_temp_c(60.0f));
}

// Midpoint of 60–75°C segment (67.5°C → 60 + 7.5*(40/15) = 60 + 20 = 80%)
void test_curve_mid_segment_two(void) {
    TEST_ASSERT_EQUAL_INT(80, emc2101_duty_for_temp_c(67.5f));
}

// At upper bound exactly → 100%
void test_curve_at_upper_bound(void) {
    TEST_ASSERT_EQUAL_INT(100, emc2101_duty_for_temp_c(75.0f));
}

// Above upper bound → 100%
void test_curve_above_upper_bound(void) {
    TEST_ASSERT_EQUAL_INT(100, emc2101_duty_for_temp_c(80.0f));
}

// Negative temperature → minimum duty (fail-safe low: hardware is cold)
void test_curve_negative_temp(void) {
    TEST_ASSERT_EQUAL_INT(30, emc2101_duty_for_temp_c(-10.0f));
}

// Very high temperature → 100%
void test_curve_very_high_temp(void) {
    TEST_ASSERT_EQUAL_INT(100, emc2101_duty_for_temp_c(150.0f));
}

// Monotonic non-decreasing across key boundary points
void test_curve_monotonic(void) {
    float points[] = {35.0f, 40.0f, 50.0f, 60.0f, 67.5f, 75.0f, 80.0f};
    int n = (int)(sizeof(points) / sizeof(points[0]));
    int prev = emc2101_duty_for_temp_c(points[0]);
    for (int i = 1; i < n; i++) {
        int cur = emc2101_duty_for_temp_c(points[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(prev, cur);
        prev = cur;
    }
}
