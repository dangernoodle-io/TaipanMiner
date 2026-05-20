/*
 * test_share_validate.c — host tests for share_validate and share_meets_network_target.
 */
#include "unity.h"
#include "share_validate.h"
#include "work.h"
#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * share_meets_network_target tests
 * ------------------------------------------------------------------------- */

/*
 * Genesis block (block 0).
 *   nbits : 0x1d00ffff
 *   hash  : 000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f
 *           (big-endian display / RPC format)
 *
 * share_meets_network_target() expects the hash in Bitcoin internal
 * little-endian order (same as sha256d output / meets_target convention):
 * reverse the display bytes → LE bytes below.
 *
 * The network target for 0x1d00ffff in LE is 0x00...00ffff00...00 with
 * 0xff,0xff at LE indices 27,28 (= BE indices 4,3 reversed). The genesis
 * hash LE value is much smaller → hash <= target → returns true.
 */
void test_share_meets_network_target_genesis_block(void)
{
    /* Genesis hash in little-endian internal byte order (reversed display) */
    static const uint8_t genesis_hash_le[32] = {
        0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
        0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
        0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
        0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    TEST_ASSERT_TRUE(share_meets_network_target(genesis_hash_le, 0x1d00ffffu));
}

/*
 * A typical stratum pool-share hash should NOT meet a 2024-era mainnet target.
 *
 * Use nbits 0x17053894 (approx mainnet 2024 difficulty ~90T):
 *   exponent=0x17=23, coefficient=0x053894
 *   LE target has 0x05,0x38,0x94 at indices 9,10,11 (= 32-23=9 reversed),
 *   all higher LE bytes zero — an extremely small value.
 *
 * A pool share at diff ~8192 has LE hash[31]=0, LE hash[30]=0, rest non-zero,
 * giving a 256-bit value far above the 2024 mainnet target → false.
 */
void test_share_meets_network_target_pool_share_fails(void)
{
    /* Pool-share hash in LE internal order: high LE bytes (indices 29-31) are
     * zero (difficulty ~8192), but LE bytes below index 29 are non-zero.
     * The 2024 mainnet LE target has all bytes ≥ index 12 as zero, so
     * comparing from LE index 31 down, this hash is larger → false. */
    static const uint8_t pool_share_hash_le[32] = {
        0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18, 0x29,
        0x3a, 0x4b, 0x5c, 0x6d, 0x7e, 0x8f, 0x90, 0xa1,
        0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18, 0x29,
        0x3a, 0x4b, 0x5c, 0x6d, 0x1f, 0x4a, 0x00, 0x00,
    };
    /* nbits 0x17053894 — approx Bitcoin mainnet 2024 difficulty */
    TEST_ASSERT_FALSE(share_meets_network_target(pool_share_hash_le, 0x17053894u));
}

/*
 * Edge case: hash exactly equals the network target → true (≤ comparison).
 * For nbits 0x1d00ffff the LE target has 0xff at LE indices 27 and 28.
 * Construct an LE hash equal to that LE target and expect true.
 */
void test_share_meets_network_target_exact_equals_target(void)
{
    /* nbits 0x1d00ffff: BE target = [0,0,0,0,0xff,0xff,0,...,0]
     * LE (reversed)     = [0,...,0,0xff,0xff,0,0,0,0]
     * 0xff appears at LE indices 27 (= 31-4) and 26 (= 31-5). */
    static const uint8_t hash_equals_target_le[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    };
    TEST_ASSERT_TRUE(share_meets_network_target(hash_equals_target_le, 0x1d00ffffu));
}
