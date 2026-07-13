#include "unity.h"
#include "sha256.h"
#include "mining.h"

// Test: sha256_sw_self_test passes on known NIST vector
// Verifies the self-test function correctly validates SHA-256("abc")
void test_sha256_sw_self_test_passes(void)
{
    bb_err_t result = sha256_sw_self_test();
    TEST_ASSERT_EQUAL_INT(BB_OK, result);
}

// Test: mining_sha_self_test_failed returns false initially
// State is process-static, so this must run first
void test_mining_self_test_flag_default_false(void)
{
    bool failed = mining_sha_self_test_failed();
    TEST_ASSERT_FALSE(failed);
}

// Test: mining_set_sha_self_test_failed flips flag to true
void test_mining_set_self_test_failed_flips_flag(void)
{
    mining_set_sha_self_test_failed();
    bool failed = mining_sha_self_test_failed();
    TEST_ASSERT_TRUE(failed);
}

// Test: sha256_check_abc_vector accepts the canonical NIST digest
void test_sha256_check_abc_vector_accepts_correct_digest(void)
{
    const uint8_t correct[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    TEST_ASSERT_EQUAL_INT(BB_OK, sha256_check_abc_vector("test", correct));
}

// Test: sha256_check_abc_vector rejects a wrong digest (covers FAIL log branch)
void test_sha256_check_abc_vector_rejects_wrong_digest(void)
{
    uint8_t wrong[32] = {0};  // all zeros — definitely not SHA('abc')
    TEST_ASSERT_EQUAL_INT(BB_ERR_INVALID_STATE, sha256_check_abc_vector("test", wrong));
}

// Test: single-byte corruption at the digest tail is caught
void test_sha256_check_abc_vector_rejects_one_byte_flip(void)
{
    uint8_t corrupted[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xae   // last byte: ad -> ae
    };
    TEST_ASSERT_EQUAL_INT(BB_ERR_INVALID_STATE, sha256_check_abc_vector("test", corrupted));
}
