/*
 * test_share_validate.c — host tests for share_validate and share_meets_network_target.
 */
#include "unity.h"
#include "share_validate.h"
#include "work.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

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

/* ---------------------------------------------------------------------------
 * Bitcoin block 100,000 fixture — byte-order regression test
 *
 * Block 100,000:
 *   display hash (BE/RPC): 000000000003ba27aa200b1cecaad478d2b00432346c3f1f3986da1afd33e506
 *   nbits: 0x1b04864c
 *   raw SHA256d output (= mining_hash_from_state output for this block):
 *     06 e5 33 fd 1a da 86 39 1f 3f 6c 34 32 04 b0 d2
 *     78 d4 aa ec 1c 0b 20 aa 27 ba 03 00 00 00 00 00
 *   (display bytes reversed; raw[31]=0x00, raw[30]=0x00 = LE-MSB bytes)
 *
 * share_meets_network_target expects the hash in raw SHA256d byte order
 * (= Bitcoin internal LE convention: raw[31] = LE-MSB = 0 for this block).
 * This matches the output of mining_hash_from_state(canonical_state, hash)
 * where canonical_state[0..7] = SHA256d H[0..7].
 *
 * Regression: the LE form (raw SHA256d) MUST return true, the BE form
 * (display/reversed) MUST return false.  If the register-mapping or
 * hash-serialization byte order regresses, one of these assertions flips.
 * ------------------------------------------------------------------------- */
void test_share_meets_network_target_known_hit(void)
{
    /* Block 100,000 hash in raw SHA256d byte order (= LE internal convention).
     * This is the format mining_hash_from_state produces for a real mined block.
     * raw[31]=0x00, raw[30]=0x00 ... raw[5]=0x03 (leading zeros of the display
     * hash appear at the END of raw bytes). */
    static const uint8_t hash_raw_sha256d[32] = {
        0x06,0xe5,0x33,0xfd,0x1a,0xda,0x86,0x39,
        0x1f,0x3f,0x6c,0x34,0x32,0x04,0xb0,0xd2,
        0x78,0xd4,0xaa,0xec,0x1c,0x0b,0x20,0xaa,
        0x27,0xba,0x03,0x00,0x00,0x00,0x00,0x00,
    };
    /* Same hash reversed to display/BE order (MSB at index 0). */
    uint8_t hash_display[32];
    for (int i = 0; i < 32; i++) hash_display[i] = hash_raw_sha256d[31 - i];

    uint32_t nbits = 0x1b04864cu;

    bool raw_result     = share_meets_network_target(hash_raw_sha256d, nbits);
    bool display_result = share_meets_network_target(hash_display, nbits);

    printf("raw SHA256d (LE) form: meets_network_target = %d (expect 1)\n", raw_result);
    printf("display (BE) form:     meets_network_target = %d (expect 0)\n", display_result);

    /* Raw SHA256d (LE internal) form MUST return true — this is the format
     * mining_hash_from_state produces and the block WAS mined. */
    TEST_ASSERT_TRUE(raw_result);
    /* Display (BE reversed) form MUST return false — wrong byte order. */
    TEST_ASSERT_FALSE(display_result);
}

/*
 * A pool-share-like hash with ~24 leading zero bits should NOT meet the
 * block-100,000 network target (far harder than any pool difficulty).
 * Both byte-order forms must return false — neither form of a weak hash
 * accidentally clears a real network target.
 */
void test_share_meets_network_target_share_misses_network(void)
{
    /* LE form: ~24 leading zero bits at the MSB end (indices 31..29).
     * Represents roughly pool difficulty ~16M — nowhere near block-100k
     * difficulty (~14M difficulty, but with a much tighter coefficient). */
    uint8_t hash_le[32];
    memset(hash_le, 0xFF, 32);
    hash_le[31] = 0x00;
    hash_le[30] = 0x00;
    hash_le[29] = 0x00;
    hash_le[28] = 0x01;  /* 24+ leading zero bits; rest non-zero */

    uint8_t hash_be[32];
    for (int i = 0; i < 32; i++) hash_be[i] = hash_le[31 - i];

    uint32_t nbits = 0x1b04864cu;  /* block 100,000 difficulty */

    bool be_result = share_meets_network_target(hash_be, nbits);
    bool le_result = share_meets_network_target(hash_le, nbits);

    printf("share-not-block BE: %d  LE: %d\n", be_result, le_result);

    /* Neither form of a weak pool share should clear a real network target. */
    TEST_ASSERT_FALSE(be_result);
    TEST_ASSERT_FALSE(le_result);
}

/* ---------------------------------------------------------------------------
 * Regression: pool-diff hash meets pool target but NOT network target.
 *
 * Synthesizes a hash in raw SHA256d / LE-internal format that satisfies
 * pool difficulty ~0.006 (leading zeros at LE-MSB end = raw[31..29]=0,
 * raw[28]=0x50 < target[28]=0xA6) but is far above any real mainnet target
 * (2024-era nbits 0x17053894).  share_meets_network_target MUST return false.
 *
 * This is the regression guard for the false-positive bug where incorrect
 * DPORT register mapping caused ~5977 spurious block detections in minutes.
 * ------------------------------------------------------------------------- */
void test_share_meets_network_target_pool_diff_not_network(void)
{
    /* Raw SHA256d format (LE-internal): leading zeros at high indices.
     * raw[31..29]=0x00, raw[28]=0x50 → meets pool diff ~0.006 target.
     * raw[27..0] = non-trivial pattern, far above any mainnet target. */
    uint8_t hash[32];
    memset(hash, 0xAB, 32);  /* fill with non-zero pattern */
    hash[31] = 0x00;
    hash[30] = 0x00;
    hash[29] = 0x00;
    hash[28] = 0x50;  /* pool diff ~0.006: target[28]=0xA6, 0x50 < 0xA6 → valid share */
    /* hash[27..0] = 0xAB — large values, nowhere near mainnet target */

    /* Verify the hash meets pool diff ~0.006 target (sanity check). */
    uint8_t pool_target[32];
    difficulty_to_target(0.006, pool_target);
    TEST_ASSERT_TRUE(meets_target(hash, pool_target));

    /* 2024-era mainnet nbits 0x17053894 → very hard network target.
     * A pool-difficulty hash MUST NOT meet it. */
    TEST_ASSERT_FALSE(share_meets_network_target(hash, 0x17053894u));
}
