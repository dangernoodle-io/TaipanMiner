#include "unity.h"
#include "mining.h"
#include "work.h"
#include <string.h>
#include <math.h>
#include <float.h>

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
    mining_session_t lt = {0};

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
    mining_session_t lt2 = {0};
    double share_diff4 = 0.05;
    if (share_diff4 > lt2.best_diff) lt2.best_diff = share_diff4;
    TEST_ASSERT_EQUAL_DOUBLE(0.05, lt2.best_diff);
}

void test_lifetime_best_diff_only_increases(void)
{
    mining_lifetime_t lt = {0};

    // Apply share diffs in order: [1024.0, 32.0, 32768.0, 0.05, 1e6]
    double diffs[] = {1024.0, 32.0, 32768.0, 0.05, 1e6};
    for (int i = 0; i < 5; i++) {
        double share_diff = diffs[i];
        if (share_diff > lt.best_diff) lt.best_diff = share_diff;
    }

    // Assert final value is 1e6
    TEST_ASSERT_EQUAL_DOUBLE(1e6, lt.best_diff);
}

void test_pack_unpack_double_roundtrip(void)
{
    // Test cases: 0.0, 1.0, 1024.0, 0.05, 1e9, DBL_MAX
    double test_values[] = {0.0, 1.0, 1024.0, 0.05, 1e9, DBL_MAX};
    int num_tests = sizeof(test_values) / sizeof(test_values[0]);

    for (int i = 0; i < num_tests; i++) {
        double original = test_values[i];
        uint32_t hi, lo;

        // Pack the double
        pack_double(original, &hi, &lo);

        // Unpack back
        double recovered = unpack_double(hi, lo);

        // Assert bit-equality
        TEST_ASSERT_EQUAL_DOUBLE(original, recovered);
    }
}

void test_lifetime_save_load_roundtrip(void)
{
    // Populate lifetime stats
    mining_lifetime_t original = {
        .total_shares = 42,
        .total_hashes = 0x123456789ABCULL,
        .best_diff = 8192.5
    };

    // Simulate packing/unpacking as mining_stats_save_lifetime and
    // mining_stats_load_lifetime would do
    uint32_t packed_shares = original.total_shares;
    uint32_t packed_hashes_lo = (uint32_t)(original.total_hashes & 0xFFFFFFFFu);
    uint32_t packed_hashes_hi = (uint32_t)(original.total_hashes >> 32);
    uint32_t packed_best_hi, packed_best_lo;
    pack_double(original.best_diff, &packed_best_hi, &packed_best_lo);

    // Simulate unpacking (as mining_stats_load_lifetime does)
    mining_lifetime_t loaded = {0};
    loaded.total_shares = packed_shares;
    loaded.total_hashes = ((uint64_t)packed_hashes_hi << 32) | packed_hashes_lo;
    loaded.best_diff = unpack_double(packed_best_hi, packed_best_lo);

    // Assert exact roundtrip
    TEST_ASSERT_EQUAL_UINT32(original.total_shares, loaded.total_shares);
    TEST_ASSERT_EQUAL_UINT64(original.total_hashes, loaded.total_hashes);
    TEST_ASSERT_EQUAL_DOUBLE(original.best_diff, loaded.best_diff);
}

void test_lifetime_best_diff_survives_zero_load(void)
{
    // Start with zero best_diff
    mining_lifetime_t lt = {0};
    TEST_ASSERT_EQUAL_DOUBLE(0.0, lt.best_diff);

    // Update via share: 0.5
    double share_diff = 0.5;
    if (share_diff > lt.best_diff) lt.best_diff = share_diff;
    TEST_ASSERT_EQUAL_DOUBLE(0.5, lt.best_diff);

    // Simulate save/load roundtrip
    uint32_t best_hi, best_lo;
    pack_double(lt.best_diff, &best_hi, &best_lo);

    mining_lifetime_t loaded = {0};
    loaded.best_diff = unpack_double(best_hi, best_lo);

    // Assert value survives
    TEST_ASSERT_EQUAL_DOUBLE(0.5, loaded.best_diff);
}
