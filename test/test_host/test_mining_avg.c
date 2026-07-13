#include "unity.h"
#include "mining_avg.h"

#include <math.h>

// TA-234: mining_avg

void test_avg_nan_safe_empty_all_nan(void)
{
    float buf[4] = {NAN, NAN, NAN, NAN};
    float result = mining_avg_nan_safe(buf, 4);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result);
}

void test_avg_nan_safe_single_value(void)
{
    float buf[4] = {5.0f, NAN, NAN, NAN};
    float result = mining_avg_nan_safe(buf, 4);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, result);
}

void test_avg_nan_safe_partial_nan(void)
{
    float buf[4] = {2.0f, NAN, 4.0f, NAN};
    float result = mining_avg_nan_safe(buf, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, result);
}

void test_avg_nan_safe_all_populated(void)
{
    float buf[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float result = mining_avg_nan_safe(buf, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, result);
}

void test_update_warmup_1m(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // First call: sample 10.0
    mining_avg_update(0, 10.0f,
                           buf_1m, buf_10m, buf_1h,
                           &prev_10m, &prev_1h,
                           &out_1m, &out_10m, &out_1h);

    // With only one non-NaN in 1m buffer, out_1m should be 10.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, out_1m);
}

void test_update_full_1m_window(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Feed 12 samples of constant value 100.0
    float sample_val = 100.0f;
    for (unsigned long pc = 0; pc < MINING_AVG_1M_SIZE; pc++) {
        mining_avg_update(pc, sample_val,
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }

    // After filling 1m window, out_1m should be exactly 100.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, out_1m);
}

void test_update_step_change(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Feed 12 samples of 50.0
    for (unsigned long pc = 0; pc < MINING_AVG_1M_SIZE; pc++) {
        mining_avg_update(pc, 50.0f,
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, out_1m);

    // Now feed 12 samples of 100.0 (step change)
    for (unsigned long pc = MINING_AVG_1M_SIZE; pc < 2 * MINING_AVG_1M_SIZE; pc++) {
        mining_avg_update(pc, 100.0f,
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }

    // 1m should now be 100.0 (fully replaced old values)
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, out_1m);
    // 10m should be partially blended — not yet fully at 100.0
    // At poll_count == 24 (end of second window), blend_10m == 0, so we read prev
    // and blend toward 100.0; it should be between 50 and 100
    TEST_ASSERT_TRUE(out_10m > 50.0f && out_10m < 100.0f);
}

void test_update_ring_wraparound(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Feed > 12 samples (wrap around the 1m ring buffer)
    for (unsigned long pc = 0; pc < 20; pc++) {
        float val = (float)(pc + 1);
        mining_avg_update(pc, val,
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }

    // After 20 updates, the 1m buffer (size 12) should contain the last 12 values: 9..20
    // Mean of 9..20 = (9+20)/2 = 14.5
    float expected = (9.0f + 20.0f) / 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected, out_1m);
}

void test_update_10m_blend_formula(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Fill 1m with value A = 50.0
    for (unsigned long pc = 0; pc < MINING_AVG_1M_SIZE; pc++) {
        mining_avg_update(pc, 50.0f,
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }

    // At poll_count == 12 (start of second 1m cycle), blend_10m wraps to 0
    // buf_10m[0] gets the first value (current out_1m which was 50.0)
    // prev_10m gets set to buf_10m[0] which is NAN initially, so blend doesn't happen
    // After one more 1m cycle, the 10m buffer has one real value and rest NAN
    // so out_10m should reflect that one value

    // Check that at poll_count boundary transitions are smooth
    for (unsigned long pc = MINING_AVG_1M_SIZE; pc < 2 * MINING_AVG_1M_SIZE; pc++) {
        mining_avg_update(pc, 50.0f,
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }

    // After two complete 1m windows with stable 50.0, 10m should reflect 50.0
    // (accounting for blend-in from NAN prev)
    TEST_ASSERT_TRUE(out_10m > 0.0f);  // No longer NAN
}

void test_update_1h_accumulation(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Feed samples to reach poll_count == 120 (end of 10 full 1m windows)
    // blend_1h = pc % (10 * 12) = pc % 120
    // At pc == 120, blend_1h == 0, so prev_1h is set and 1h blend begins
    unsigned long target_pc = 120;
    for (unsigned long pc = 0; pc <= target_pc; pc++) {
        mining_avg_update(pc, 75.0f,
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }

    // After 120 polls with stable 75.0, all windows should be close to 75.0
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 75.0f, out_1m);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 75.0f, out_10m);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 75.0f, out_1h);
}

void test_update_with_zero_samples(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Feed zeros (not NAN, but zero value)
    for (unsigned long pc = 0; pc < MINING_AVG_1M_SIZE; pc++) {
        mining_avg_update(pc, 0.0f,
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out_1m);
}

void test_update_mixed_values(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Alternate between 10.0 and 20.0
    float values[] = {10.0f, 20.0f, 10.0f, 20.0f, 10.0f, 20.0f,
                      10.0f, 20.0f, 10.0f, 20.0f, 10.0f, 20.0f};
    for (unsigned long pc = 0; pc < MINING_AVG_1M_SIZE; pc++) {
        mining_avg_update(pc, values[pc % 2],
                               buf_1m, buf_10m, buf_1h,
                               &prev_10m, &prev_1h,
                               &out_1m, &out_10m, &out_1h);
    }

    // Mean of alternating 10 and 20 should be 15.0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 15.0f, out_1m);
}

// TA-363: mining_pool_eff_tick
void test_mining_pool_eff_tick_zero_delta(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;
    double prev_sum = 100.0;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Sum doesn't change → sample 0 → all outputs 0
    mining_pool_eff_tick(100.0, 5.0, &prev_sum, 0,
                         buf_1m, buf_10m, buf_1h,
                         &prev_10m, &prev_1h,
                         &out_1m, &out_10m, &out_1h);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out_1m);
}

void test_mining_pool_eff_tick_typical(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;
    double prev_sum = 0.0;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Constant diff per tick: 100.0 diff over 5s
    // sample = 100.0 * 2^32 / 5.0 = 100.0 * 858993459.2 ≈ 85.9e9
    double D = 100.0;
    for (unsigned long pc = 0; pc < MINING_AVG_1M_SIZE; pc++) {
        mining_pool_eff_tick(D * (pc + 1), 5.0, &prev_sum, pc,
                             buf_1m, buf_10m, buf_1h,
                             &prev_10m, &prev_1h,
                             &out_1m, &out_10m, &out_1h);
    }

    // After 12 ticks at constant 100.0/5s, 1m should be stable at that rate
    float expected = (float)(100.0 * 4294967296.0 / 5.0);
    TEST_ASSERT_FLOAT_WITHIN(0.1f * expected, expected, out_1m);
}

void test_mining_pool_eff_tick_sum_decrease_clamps_to_zero(void)
{
    float buf_1m[MINING_AVG_1M_SIZE];
    float buf_10m[MINING_AVG_10M_SIZE];
    float buf_1h[MINING_AVG_1H_SIZE];
    float prev_10m = NAN, prev_1h = NAN;
    float out_1m, out_10m, out_1h;
    double prev_sum = 1000.0;

    for (int i = 0; i < MINING_AVG_1M_SIZE; i++) {
        buf_1m[i] = NAN;
        buf_10m[i] = NAN;
        buf_1h[i] = NAN;
    }

    // Pool reconnect: sum drops from 1000 to 500 (delta would be negative)
    // Should clamp to 0, so sample = 0
    mining_pool_eff_tick(500.0, 5.0, &prev_sum, 0,
                         buf_1m, buf_10m, buf_1h,
                         &prev_10m, &prev_1h,
                         &out_1m, &out_10m, &out_1h);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out_1m);
    // prev_sum should be updated to 500.0
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 500.0, prev_sum);
}
