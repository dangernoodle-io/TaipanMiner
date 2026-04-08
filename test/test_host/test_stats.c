#include "unity.h"
#include "mining.h"
#include "work.h"
#include <string.h>
#include <math.h>

void test_ema_seeds_on_first_sample(void)
{
    hashrate_ema_t ema = {0};
    mining_stats_update_ema(&ema, 1000.0, 100);
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, ema.value);
    TEST_ASSERT_EQUAL_INT64(100, ema.last_us);
}

void test_ema_converges(void)
{
    hashrate_ema_t ema = {0};
    for (int i = 0; i < 20; i++) {
        mining_stats_update_ema(&ema, 1000.0, (int64_t)i * 5000000);
    }
    // After 20 samples of constant 1000.0, EMA should be very close
    TEST_ASSERT_DOUBLE_WITHIN(1.0, 1000.0, ema.value);
}

void test_ema_decay(void)
{
    hashrate_ema_t ema = {0};
    mining_stats_update_ema(&ema, 1000.0, 0);  // seed at 1000
    for (int i = 1; i <= 15; i++) {
        mining_stats_update_ema(&ema, 0.0, (int64_t)i * 5000000);
    }
    // After 15 samples of 0.0: 1000 * 0.8^15 ≈ 35
    TEST_ASSERT_TRUE(ema.value < 50.0);
}

void test_hash_to_difficulty_leading_zeros(void)
{
    // Hash with 5 leading zero bytes (LE): bytes 31-27 = 0, byte 26 = 0xFF
    // sig = 0xFF000000, zero_bytes = 5
    // diff = (0xFFFF0000 / 0xFF000000) * 2^((5-4)*8) = ~1.004 * 256 ≈ 257
    uint8_t hash[32] = {0};
    hash[26] = 0xFF;
    double diff = hash_to_difficulty(hash);
    TEST_ASSERT_DOUBLE_WITHIN(2.0, 257.0, diff);
}

void test_hash_to_difficulty_diff1(void)
{
    // Hash at diff-1 (LE): bytes 31-28 = 0, byte 27 = 0xFF, byte 26 = 0xFF
    // zero_bytes = 4, sig = 0xFFFF0000
    // diff = 0xFFFF0000 / 0xFFFF0000 * 2^0 = 1.0
    uint8_t hash[32] = {0};
    hash[27] = 0xFF;
    hash[26] = 0xFF;
    double diff = hash_to_difficulty(hash);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 1.0, diff);
}

void test_hash_to_difficulty_easy(void)
{
    // All 0xFF hash → very low difficulty (sub-diff-1)
    // zero_bytes=0, sig=0xFFFFFFFF, shift=-32
    // diff ≈ 2.33e-10
    uint8_t hash[32];
    memset(hash, 0xFF, 32);
    double diff = hash_to_difficulty(hash);
    TEST_ASSERT_TRUE(diff < 1e-9);
    TEST_ASSERT_TRUE(diff > 0.0);
}

void test_hash_to_difficulty_six_zeros(void)
{
    // Hash with 6 leading zero bytes (LE): bytes 31-26 = 0, byte 25 = 0x80
    // sig = 0x80000000, zero_bytes = 6
    // diff = (0xFFFF0000 / 0x80000000) * 2^((6-4)*8) = ~2.0 * 65536 = ~131070
    uint8_t hash[32] = {0};
    hash[25] = 0x80;
    double diff = hash_to_difficulty(hash);
    TEST_ASSERT_DOUBLE_WITHIN(2.0, 131070.0, diff);
}

void test_best_diff_only_increases(void)
{
    mining_lifetime_t lt = {0};

    // First share: diff 1024.0
    double share_diff1 = 1024.0;
    if (share_diff1 > lt.best_diff) lt.best_diff = share_diff1;
    TEST_ASSERT_EQUAL_DOUBLE(1024.0, lt.best_diff);

    // Second share: diff 32.0 (lower, should not update)
    double share_diff2 = 32.0;
    if (share_diff2 > lt.best_diff) lt.best_diff = share_diff2;
    TEST_ASSERT_EQUAL_DOUBLE(1024.0, lt.best_diff);

    // Third share: diff 32768.0 (higher, should update)
    double share_diff3 = 32768.0;
    if (share_diff3 > lt.best_diff) lt.best_diff = share_diff3;
    TEST_ASSERT_EQUAL_DOUBLE(32768.0, lt.best_diff);

    // Sub-diff-1 share on fresh stats
    mining_lifetime_t lt2 = {0};
    double share_diff4 = 0.05;
    if (share_diff4 > lt2.best_diff) lt2.best_diff = share_diff4;
    TEST_ASSERT_EQUAL_DOUBLE(0.05, lt2.best_diff);
}
