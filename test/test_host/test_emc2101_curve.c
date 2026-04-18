#include "unity.h"
#include "emc2101_curve.h"

// Below lower bound → minimum duty
void test_curve_below_lower_bound(void) {
    TEST_ASSERT_EQUAL_INT(30, emc2101_duty_for_temp_c(35.0f));
}

// At lower bound exactly
void test_curve_at_lower_bound(void) {
    TEST_ASSERT_EQUAL_INT(30, emc2101_duty_for_temp_c(45.0f));
}

// Midpoint of 45–70°C segment (57.5°C → 30 + 12.5*1.0 = 42%)
void test_curve_mid_segment_one(void) {
    TEST_ASSERT_EQUAL_INT(42, emc2101_duty_for_temp_c(57.5f));
}

// At 70°C boundary → 55%
void test_curve_at_segment_boundary(void) {
    TEST_ASSERT_EQUAL_INT(55, emc2101_duty_for_temp_c(70.0f));
}

// Midpoint of 70–85°C segment (77.5°C → 55 + 7.5*3.0 = 77%)
void test_curve_mid_segment_two(void) {
    TEST_ASSERT_EQUAL_INT(77, emc2101_duty_for_temp_c(77.5f));
}

// At upper bound exactly → 100%
void test_curve_at_upper_bound(void) {
    TEST_ASSERT_EQUAL_INT(100, emc2101_duty_for_temp_c(85.0f));
}

// Above upper bound → 100%
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

// Monotonic non-decreasing across key boundary points
void test_curve_monotonic(void) {
    float points[] = {35.0f, 45.0f, 57.5f, 70.0f, 77.5f, 85.0f, 90.0f};
    int n = (int)(sizeof(points) / sizeof(points[0]));
    int prev = emc2101_duty_for_temp_c(points[0]);
    for (int i = 1; i < n; i++) {
        int cur = emc2101_duty_for_temp_c(points[i]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(prev, cur);
        prev = cur;
    }
}
