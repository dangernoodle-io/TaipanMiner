#include "unity.h"
#include "asic_nonce_dedup.h"

// TA-234: asic_nonce_dedup

void test_nonce_dedup_fresh_state_has_no_dups(void)
{
    asic_nonce_dedup_t d = {0};
    asic_nonce_dedup_reset(&d);

    // Fresh state: any insert should return false (not a dup)
    bool result = asic_nonce_dedup_check_and_insert(&d, 1, 0x12345678, 0x100);
    TEST_ASSERT_FALSE(result);
}

void test_nonce_dedup_immediate_redundant_insert_returns_true(void)
{
    asic_nonce_dedup_t d = {0};
    asic_nonce_dedup_reset(&d);

    // Insert (1, 0xABCD, 0x100)
    bool result1 = asic_nonce_dedup_check_and_insert(&d, 1, 0xABCD, 0x100);
    TEST_ASSERT_FALSE(result1);

    // Insert same triple again
    bool result2 = asic_nonce_dedup_check_and_insert(&d, 1, 0xABCD, 0x100);
    TEST_ASSERT_TRUE(result2);
}

void test_nonce_dedup_different_job_id_is_not_dup(void)
{
    asic_nonce_dedup_t d = {0};
    asic_nonce_dedup_reset(&d);

    bool result1 = asic_nonce_dedup_check_and_insert(&d, 1, 0xABCD, 0x100);
    TEST_ASSERT_FALSE(result1);

    // Different job_id with same nonce and ver
    bool result2 = asic_nonce_dedup_check_and_insert(&d, 2, 0xABCD, 0x100);
    TEST_ASSERT_FALSE(result2);
}

void test_nonce_dedup_different_nonce_is_not_dup(void)
{
    asic_nonce_dedup_t d = {0};
    asic_nonce_dedup_reset(&d);

    bool result1 = asic_nonce_dedup_check_and_insert(&d, 1, 0xABCD, 0x100);
    TEST_ASSERT_FALSE(result1);

    // Same job_id and ver, different nonce
    bool result2 = asic_nonce_dedup_check_and_insert(&d, 1, 0x5678, 0x100);
    TEST_ASSERT_FALSE(result2);
}

void test_nonce_dedup_different_ver_is_not_dup(void)
{
    asic_nonce_dedup_t d = {0};
    asic_nonce_dedup_reset(&d);

    bool result1 = asic_nonce_dedup_check_and_insert(&d, 1, 0xABCD, 0x100);
    TEST_ASSERT_FALSE(result1);

    // Same job_id and nonce, different ver
    bool result2 = asic_nonce_dedup_check_and_insert(&d, 1, 0xABCD, 0x200);
    TEST_ASSERT_FALSE(result2);
}

void test_nonce_dedup_ring_wraparound_evicts_oldest(void)
{
    asic_nonce_dedup_t d = {0};
    asic_nonce_dedup_reset(&d);

    // Fill 16 distinct entries
    for (int i = 0; i < ASIC_NONCE_DEDUP_SIZE; i++) {
        bool result = asic_nonce_dedup_check_and_insert(&d, (uint8_t)i, (uint32_t)(0x1000 + i), 0x100 + i);
        TEST_ASSERT_FALSE(result);
    }

    // After 16 inserts, next_idx should be 0 (will overwrite entry 0 next)
    TEST_ASSERT_EQUAL_UINT8(0, d.next_idx);

    // Insert a 17th entry with distinct values — should return false, overwriting entry 0
    bool result = asic_nonce_dedup_check_and_insert(&d, 99, 0x5000, 0x200);
    TEST_ASSERT_FALSE(result);  // Not a dup (new values)

    // Now entry 0's old value (0, 0x1000, 0x100) should be gone; insert it
    result = asic_nonce_dedup_check_and_insert(&d, 0, 0x1000, 0x100);
    TEST_ASSERT_FALSE(result);  // Not a dup because entry 0 was evicted and overwritten

    // Insert again — now it's a dup
    result = asic_nonce_dedup_check_and_insert(&d, 0, 0x1000, 0x100);
    TEST_ASSERT_TRUE(result);
}

void test_nonce_dedup_reset_clears_all(void)
{
    asic_nonce_dedup_t d = {0};
    asic_nonce_dedup_reset(&d);

    // Insert some entries
    asic_nonce_dedup_check_and_insert(&d, 1, 0xABCD, 0x100);
    asic_nonce_dedup_check_and_insert(&d, 2, 0x5678, 0x200);

    // Reset
    asic_nonce_dedup_reset(&d);

    // Entry 1 should no longer be a dup
    bool result = asic_nonce_dedup_check_and_insert(&d, 1, 0xABCD, 0x100);
    TEST_ASSERT_FALSE(result);
}

void test_nonce_dedup_next_idx_advances_cyclically(void)
{
    asic_nonce_dedup_t d = {0};
    asic_nonce_dedup_reset(&d);

    // Check initial state
    TEST_ASSERT_EQUAL_UINT8(0, d.next_idx);

    // Insert 16 distinct entries, check next_idx advances
    for (int i = 0; i < ASIC_NONCE_DEDUP_SIZE; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, d.next_idx);
        asic_nonce_dedup_check_and_insert(&d, (uint8_t)i, (uint32_t)(0x1000 + i), 0x100 + i);
    }

    // After 16 inserts, next_idx should be back at 0
    TEST_ASSERT_EQUAL_UINT8(0, d.next_idx);
}
