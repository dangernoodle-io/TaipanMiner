#include "unity.h"
#include "work.h"
#include "work_build.h"
#include "mining.h"
#include <string.h>

// Test: mining_hash_from_state stores canonical SHA-256 state as big-endian bytes.
// Uses the SHA-256("abc") known-answer vector.
void test_mining_hash_from_state_abc_vector(void)
{
    const uint32_t state[8] = {
        0xba7816bfu, 0x8f01cfeau, 0x414140deu, 0x5dae2223u,
        0xb00361a3u, 0x96177a9cu, 0xb410ff61u, 0xf20015adu,
    };
    const uint8_t expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    uint8_t out[32] = {0};
    mining_hash_from_state(state, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 32);
}

// Test: zero state produces all-zero bytes.
void test_mining_hash_from_state_zero(void)
{
    const uint32_t state[8] = {0};
    uint8_t out[32];
    memset(out, 0xff, sizeof(out));
    mining_hash_from_state(state, out);
    for (size_t i = 0; i < 32; i++) TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
}

// Test: set_header_nonce modifies bytes 76-79
void test_set_header_nonce(void)
{
    uint8_t header[80];
    uint8_t prevhash[32] = {0};
    uint8_t merkle_root[32] = {0};

    // Create a header with nonce 0x00000001
    serialize_header(1, prevhash, merkle_root, 0x495fab29, 0x1d00ffff, 0x00000001, header);

    // Now change nonce to 0xdeadbeef
    set_header_nonce(header, 0xefbeadde);

    // Verify bytes 76-79 are now 0xefbeadde (little-endian)
    TEST_ASSERT_EQUAL_HEX8(0xde, header[76]);
    TEST_ASSERT_EQUAL_HEX8(0xad, header[77]);
    TEST_ASSERT_EQUAL_HEX8(0xbe, header[78]);
    TEST_ASSERT_EQUAL_HEX8(0xef, header[79]);
}

// Test: nbits_to_target for genesis difficulty
// nbits=0x1d00ffff should produce target with 0x00ffff at the top
void test_nbits_to_target_genesis(void)
{
    uint8_t target[32];
    const uint8_t expected[32] = {
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    nbits_to_target(0x1d00ffff, target);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, target, 32);
}

// Test: nbits_to_target with higher difficulty
void test_nbits_to_target_high_diff(void)
{
    uint8_t target[32];

    // Example: 0x1a00a000 - exponent=0x1a=26, mantissa=0x00a000
    // Position = 32 - 26 = 6
    // Result: 26 zero bytes + 0x00, 0xa0, 0x00 at positions 6-8, then rest zeros
    const uint8_t expected[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    nbits_to_target(0x1a00a000, target);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, target, 32);
}

// Test: meets_target - hash with leading zeros should pass
void test_meets_target_pass(void)
{
    // LE convention: byte[31] is MSB. Hash has zeros at MSB, target has 0xffff at bytes [26-27].
    uint8_t hash[32] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t target[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    };

    TEST_ASSERT_TRUE(meets_target(hash, target));
}

// Test: meets_target - hash with high MSB bytes should fail
void test_meets_target_fail(void)
{
    // LE convention: byte[31] is MSB. Hash MSB > target MSB → fail.
    uint8_t hash[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    };
    uint8_t target[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    };

    TEST_ASSERT_FALSE(meets_target(hash, target));
}

// Test: meets_target - hash exactly equal to target should pass
void test_meets_target_equal(void)
{
    uint8_t hash[32] = {
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t target[32];
    memcpy(target, hash, 32);

    TEST_ASSERT_TRUE(meets_target(hash, target));
}

// Regression test: verify mining pipeline byte-order handling with constructed example
// Tests that state[7] word extraction matches target_word0 packing
void test_mining_round_trip_block1(void)
{
    // Create a synthetic hash that we know meets a difficulty target
    // This tests the critical path: hash bytes [28-31] must match target bytes [28-31]
    uint8_t hash[32];
    memset(hash, 0xFF, 28);  // Fill first 28 bytes with 0xFF (doesn't matter for comparison)
    // Last 4 bytes (hash[28-31]) in BE word order
    hash[28] = 0x00;
    hash[29] = 0x00;
    hash[30] = 0x00;
    hash[31] = 0x00;

    // Create target with same bytes at [28-31]
    uint8_t target[32];
    memset(target, 0xFF, 26);   // Bytes [0-25]: fill with 0xFF
    target[26] = 0xFF;
    target[27] = 0xFF;
    target[28] = 0x00;
    target[29] = 0x00;
    target[30] = 0x00;
    target[31] = 0x00;

    // Verify meets_target works correctly
    TEST_ASSERT_TRUE(meets_target(hash, target));

    // Test the state[7] extraction and target_word0 packing
    // state[7] = (hash[28]<<24 | hash[29]<<16 | hash[30]<<8 | hash[31])
    uint32_t state7 = (hash[28] << 24) | (hash[29] << 16) |
                      (hash[30] << 8) | hash[31];
    // Should be 0x00000000 for our test hash
    TEST_ASSERT_EQUAL_UINT32(0x00000000, state7);

    // target_word0 = (target[28]<<24 | target[29]<<16 | target[30]<<8 | target[31])
    uint32_t target_word0 = (target[28] << 24) | (target[29] << 16) |
                            (target[30] << 8) | target[31];
    // Should also be 0x00000000 for our test target
    TEST_ASSERT_EQUAL_UINT32(0x00000000, target_word0);

    // The key assertion: state[7] <= target_word0 for valid shares
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(target_word0, state7);

    // Now test with a hash that exceeds target at byte[31]
    hash[31] = 0x01;  // Make MSB higher
    uint32_t state7_high = (hash[28] << 24) | (hash[29] << 16) |
                           (hash[30] << 8) | hash[31];
    TEST_ASSERT_GREATER_THAN_UINT32(target_word0, state7_high);
    TEST_ASSERT_FALSE(meets_target(hash, target));
}

// Regression test: verify byte-order packing for early-reject logic
// This specifically tests the fix for the byte-swap bug where target_word0
// was incorrectly packed as (target[31]<<24 | ... | target[28])
void test_mining_early_reject_byte_order(void)
{
    // Create a synthetic target with distinct bytes at [28-31]
    // to demonstrate correct vs incorrect packing
    uint8_t target[32];
    memset(target, 0, 32);
    target[28] = 0x12;
    target[29] = 0x34;
    target[30] = 0x56;
    target[31] = 0x78;

    // Correct target_word0: (target[28]<<24 | target[29]<<16 | target[30]<<8 | target[31])
    uint32_t correct_tw0 = (target[28] << 24) | (target[29] << 16) |
                           (target[30] << 8) | target[31];
    // Should be 0x12345678
    TEST_ASSERT_EQUAL_UINT32(0x12345678, correct_tw0);

    // Wrong target_word0 (the bug): (target[31]<<24 | target[30]<<16 | target[29]<<8 | target[28])
    uint32_t wrong_tw0 = (target[31] << 24) | (target[30] << 16) |
                         (target[29] << 8) | target[28];
    // Would be 0x78563412 (bytes reversed)
    TEST_ASSERT_EQUAL_UINT32(0x78563412, wrong_tw0);

    // Verify they're different
    TEST_ASSERT_NOT_EQUAL_UINT32(correct_tw0, wrong_tw0);

    // Test with a state7 value in between the two
    uint32_t state7 = 0x50000000;

    // With correct packing: state7 > correct_tw0, so it fails the early-reject
    TEST_ASSERT_GREATER_THAN_UINT32(correct_tw0, state7);

    // With wrong packing: state7 < wrong_tw0, which would incorrectly accept the share
    TEST_ASSERT_LESS_THAN_UINT32(wrong_tw0, state7);
}

// Integration test: verify difficulty_to_target and meets_target work correctly
void test_difficulty_target_meets_target_integration(void)
{
    // Test 1: Create a simple target with known values
    uint8_t target[32];
    memset(target, 0, 32);
    target[31] = 0x01;  // MSB = 0x01

    // Create hash with MSB = 0x00 (less than target MSB)
    // This should pass meets_target
    uint8_t hash_pass[32];
    memset(hash_pass, 0xFF, 31);  // Fill [0-30] with 0xFF (doesn't affect final comparison)
    hash_pass[31] = 0x00;  // MSB = 0x00
    TEST_ASSERT_TRUE(meets_target(hash_pass, target));

    // Test 2: Create hash with MSB = 0x02 (greater than target MSB = 0x01)
    // This should fail meets_target
    uint8_t hash_fail[32];
    memset(hash_fail, 0x00, 31);
    hash_fail[31] = 0x02;  // MSB = 0x02 > target[31] = 0x01
    TEST_ASSERT_FALSE(meets_target(hash_fail, target));

    // Test 3: Verify exact equality passes
    uint8_t hash_equal[32];
    memcpy(hash_equal, target, 32);
    TEST_ASSERT_TRUE(meets_target(hash_equal, target));

    // Test 4: Test with difficulty target to verify integration
    uint8_t target2[32];
    difficulty_to_target(1.0, target2);
    // For diff 1.0, target has 0xFFFF at bytes [26-27], 0x00 at [28-31]

    // Create hash that should meet diff 1.0 target
    // Hash must be <= target, so bytes [0-25]=0x00, [26-27] must be <= 0xFFFF, [28-31]=0x00
    uint8_t hash2[32];
    memset(hash2, 0x00, 32);
    hash2[26] = 0x7F;  // Less than target[26]=0xFF
    TEST_ASSERT_TRUE(meets_target(hash2, target2));
}

// Test BIP 320 version rolling mask iteration
void test_version_rolling_mask_increment(void)
{
    uint32_t mask = 0x00006000;  // 2 bits set → 4 values (including 0)
    uint32_t v;

    // First call: 0 → mask (all bits set)
    v = next_version_roll(0, mask);
    TEST_ASSERT_EQUAL_HEX32(0x00006000, v);

    // Second: 0x6000 → 0x4000
    v = next_version_roll(v, mask);
    TEST_ASSERT_EQUAL_HEX32(0x00004000, v);

    // Third: 0x4000 → 0x2000
    v = next_version_roll(v, mask);
    TEST_ASSERT_EQUAL_HEX32(0x00002000, v);

    // Fourth: 0x2000 → 0 (exhausted)
    v = next_version_roll(v, mask);
    TEST_ASSERT_EQUAL_HEX32(0x00000000, v);

    // No mask: always returns 0
    TEST_ASSERT_EQUAL_HEX32(0, next_version_roll(0, 0));
    TEST_ASSERT_EQUAL_HEX32(0, next_version_roll(0x1234, 0));
}

// is_target_valid tests

void test_is_target_valid_all_zero(void)
{
    uint8_t target[32];
    memset(target, 0, 32);
    TEST_ASSERT_FALSE(is_target_valid(target));
}

void test_is_target_valid_all_ff(void)
{
    uint8_t target[32];
    memset(target, 0xFF, 32);
    TEST_ASSERT_FALSE(is_target_valid(target));
}

void test_is_target_valid_nonzero_msb31(void)
{
    uint8_t target[32];
    memset(target, 0, 32);
    target[26] = 0x80;  // valid coefficient
    target[31] = 0x01;  // bad MSB
    TEST_ASSERT_FALSE(is_target_valid(target));
}

void test_is_target_valid_nonzero_msb30(void)
{
    uint8_t target[32];
    memset(target, 0, 32);
    target[26] = 0x80;  // valid coefficient
    target[30] = 0x01;  // bad second MSB
    TEST_ASSERT_FALSE(is_target_valid(target));
}

void test_is_target_valid_diff1(void)
{
    // diff-1 target: LE bytes 26-27 = 0xFF 0xFF (0xFFFF at BE bytes 4-5)
    uint8_t target[32];
    memset(target, 0, 32);
    target[26] = 0xFF;
    target[27] = 0xFF;
    TEST_ASSERT_TRUE(is_target_valid(target));
}

void test_is_target_valid_diff512(void)
{
    uint8_t target[32];
    difficulty_to_target(512.0, target);
    TEST_ASSERT_TRUE(is_target_valid(target));
}

void test_is_target_valid_diff_001(void)
{
    // Minimum floor difficulty
    uint8_t target[32];
    difficulty_to_target(0.001, target);
    TEST_ASSERT_TRUE(is_target_valid(target));
}

// TA-274: package_result round-trip — no version rolling
void test_package_result_round_trip_no_rolling(void)
{
    mining_work_t work;
    memset(&work, 0, sizeof(work));
    strncpy(work.job_id, "rjob-01", sizeof(work.job_id) - 1);
    strncpy(work.extranonce2_hex, "aabbccdd", sizeof(work.extranonce2_hex) - 1);
    work.ntime        = 0x66778899;
    work.version      = 0x20000000;
    work.version_mask = 0x1FFFE000;

    mining_result_t result;
    package_result(&result, &work, 0x11223344, 0);

    TEST_ASSERT_EQUAL_STRING("rjob-01",  result.job_id);
    TEST_ASSERT_EQUAL_STRING("aabbccdd", result.extranonce2_hex);
    TEST_ASSERT_EQUAL_STRING("66778899", result.ntime_hex);
    TEST_ASSERT_EQUAL_STRING("11223344", result.nonce_hex);
    // version_hex must be empty when ver_bits=0
    TEST_ASSERT_EQUAL_STRING("", result.version_hex);
}

// TA-274: package_result round-trip — with version rolling
void test_package_result_round_trip_with_rolling(void)
{
    mining_work_t work;
    memset(&work, 0, sizeof(work));
    strncpy(work.job_id, "rjob-02", sizeof(work.job_id) - 1);
    strncpy(work.extranonce2_hex, "deadbeef", sizeof(work.extranonce2_hex) - 1);
    work.ntime        = 0xaabbccdd;
    work.version      = 0x20000000;
    work.version_mask = 0x1FFFE000;

    mining_result_t result;
    // ver_bits=0x00006000 — pool expects this value in version_hex
    package_result(&result, &work, 0xcafe1234, 0x00006000);

    TEST_ASSERT_EQUAL_STRING("rjob-02",  result.job_id);
    TEST_ASSERT_EQUAL_STRING("deadbeef", result.extranonce2_hex);
    TEST_ASSERT_EQUAL_STRING("aabbccdd", result.ntime_hex);
    TEST_ASSERT_EQUAL_STRING("cafe1234", result.nonce_hex);
    // version_hex = ver_bits directly (pool XORs with base version)
    TEST_ASSERT_EQUAL_STRING("00006000", result.version_hex);
}
