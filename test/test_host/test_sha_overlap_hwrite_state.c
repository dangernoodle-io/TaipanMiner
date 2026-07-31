// test_sha_overlap_hwrite_state.c — host tests for the SHA TEXT-overlap and
// H-write-during-compute canary state setters/getters (mining.c:127-144).
// Mirrors test_sha_self_test_gate.c's pattern for the neighboring
// process-static self-test flag.
#include "unity.h"
#include "mining.h"

// State is process-static, so the default-state assertions must run first
// (before any set_*_safe call anywhere in the suite).

void test_mining_get_sha_overlap_state_default_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(SHA_OVERLAP_UNKNOWN, mining_get_sha_overlap_state());
}

void test_mining_set_sha_overlap_safe_true_sets_safe(void)
{
    mining_set_sha_overlap_safe(true);
    TEST_ASSERT_EQUAL_INT(SHA_OVERLAP_SAFE, mining_get_sha_overlap_state());
}

void test_mining_set_sha_overlap_safe_false_sets_unsafe(void)
{
    mining_set_sha_overlap_safe(false);
    TEST_ASSERT_EQUAL_INT(SHA_OVERLAP_UNSAFE, mining_get_sha_overlap_state());
}

void test_mining_get_sha_hwrite_state_default_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(SHA_OVERLAP_UNKNOWN, mining_get_sha_hwrite_state());
}

void test_mining_set_sha_hwrite_safe_true_sets_safe(void)
{
    mining_set_sha_hwrite_safe(true);
    TEST_ASSERT_EQUAL_INT(SHA_OVERLAP_SAFE, mining_get_sha_hwrite_state());
}

void test_mining_set_sha_hwrite_safe_false_sets_unsafe(void)
{
    mining_set_sha_hwrite_safe(false);
    TEST_ASSERT_EQUAL_INT(SHA_OVERLAP_UNSAFE, mining_get_sha_hwrite_state());
}
