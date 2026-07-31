#include "unity.h"
#include "work_build.h"
#include "work.h"
#include "sha256.h"
#include "bb_str.h"
#include <string.h>
#include <stdlib.h>

// Test: serialize_header with genesis block data
void test_serialize_header_genesis(void)
{
    uint8_t header[80];
    uint8_t prevhash[32] = {0};  // all zeros
    uint8_t merkle_root[32] = {
        0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
        0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
        0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
        0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
    };
    const uint8_t expected[80] = {
        0x01, 0x00, 0x00, 0x00, // version
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // prevhash
        0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
        0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
        0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
        0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a, // merkle_root
        0x29, 0xab, 0x5f, 0x49, // ntime
        0xff, 0xff, 0x00, 0x1d, // nbits
        0x1d, 0xac, 0x2b, 0x7c, // nonce
    };

    serialize_header(1, prevhash, merkle_root, 0x495fab29, 0x1d00ffff, 0x7c2bac1d, header);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, header, 80);
}

// Test: build_coinbase_hash
// Coinbase: coinb1 + extranonce1 + extranonce2 + coinb2, then SHA256d
void test_build_coinbase_hash(void)
{
    // Use simplified test data
    uint8_t coinb1[] = {0x01, 0x00, 0x00, 0x00};
    uint8_t extranonce1[] = {0xaa, 0xbb, 0xcc, 0xdd};
    uint8_t extranonce2[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t coinb2[] = {0x99};
    uint8_t result[32];

    // Manually compute expected: SHA256d(0x01000000 || aabbccdd || 11223344 || 99)
    uint8_t full[13] = {0x01, 0x00, 0x00, 0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0x11, 0x22, 0x33, 0x44, 0x99};
    uint8_t expected[32];
    sha256d(full, 13, expected);

    build_coinbase_hash(coinb1, 4, extranonce1, 4, extranonce2, 4, coinb2, 1, result);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, result, 32);
}

// Test: build_merkle_root with no branches
// With no branches, root should equal coinbase_hash
void test_build_merkle_root_no_branches(void)
{
    uint8_t coinbase_hash[32] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11};
    uint8_t root[32];

    // Fill rest with pattern
    for (int i = 8; i < 32; i++) {
        coinbase_hash[i] = (uint8_t)(i & 0xff);
    }

    build_merkle_root(coinbase_hash, NULL, 0, root);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(coinbase_hash, root, 32);
}

// Test: build_merkle_root with one branch
// root = SHA256d(coinbase_hash || branch)
void test_build_merkle_root_with_branches(void)
{
    uint8_t coinbase_hash[32];
    uint8_t branch1[32];
    uint8_t branches[1][32];
    uint8_t result[32];

    // Fill test data
    for (int i = 0; i < 32; i++) {
        coinbase_hash[i] = (uint8_t)(i & 0xff);
        branch1[i] = (uint8_t)((i * 2) & 0xff);
    }
    memcpy(branches[0], branch1, 32);

    // Expected: SHA256d(coinbase_hash || branch1)
    uint8_t combined[64];
    memcpy(combined, coinbase_hash, 32);
    memcpy(combined + 32, branch1, 32);
    uint8_t expected[32];
    sha256d(combined, 64, expected);

    build_merkle_root(coinbase_hash, branches, 1, result);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, result, 32);
}

// Test: decode_stratum_prevhash
// Stratum prevhash format: 8 groups of 4 bytes (hex), each group byte-reversed
void test_decode_stratum_prevhash(void)
{
    // Example: all zeros encoded as stratum format
    // In stratum, each 4-byte group is reversed:
    // prevhash[0:4] = 0x00000000 -> "00000000" in stratum
    // Full prevhash of 32 zero bytes = "00000000" repeated 8 times
    const char *stratum_hex = "00000000000000000000000000000000000000000000000000000000000000000000000000000000";
    uint8_t prevhash[32];
    uint8_t expected[32] = {0};

    decode_stratum_prevhash(stratum_hex, prevhash);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, prevhash, 32);
}

// Test: decode_stratum_prevhash truncated/malformed input
// bb_str_hex_to_bytes stops decoding at the first non-hex char or end of
// input and leaves any remaining destination bytes untouched. decode_stratum
// zero-inits its scratch buffer before decoding, so a short input must
// deterministically zero-pad the undecoded tail rather than read
// uninitialized stack memory.
void test_decode_stratum_prevhash_truncated(void)
{
    // Only 16 hex chars (8 bytes) supplied instead of the full 64.
    const char *stratum_hex = "0011223344556677";
    uint8_t prevhash[32];

    // Derivation: bb_str_hex_to_bytes decodes raw[0..7] =
    // {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77}; raw[8..31] stay zero
    // (zero-init). decode_stratum_prevhash then reverses each 4-byte
    // group: group 0 (raw[0..3]) -> {0x33,0x22,0x11,0x00}; group 1
    // (raw[4..7]) -> {0x77,0x66,0x55,0x44}; groups 2-7 read all-zero
    // raw bytes and reverse to all zero.
    uint8_t expected[32] = {
        0x33, 0x22, 0x11, 0x00,
        0x77, 0x66, 0x55, 0x44,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    decode_stratum_prevhash(stratum_hex, prevhash);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, prevhash, 32);
}

// Integration tests
// Test: test_block1_full_pipeline
// Validates the full pipeline for Bitcoin block #1 (single-tx block)
void test_block1_full_pipeline(void)
{
    // Raw coinbase transaction
    const char *coinbase_tx_hex = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000";

    // Expected merkle root (internal byte order)
    const char *merkle_root_hex = "982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e";

    // Expected prevhash (internal byte order)
    const char *prevhash_hex = "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000";

    // Full header hex
    const char *header_hex = "010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff001d01e36299";

    // Expected block hash (internal byte order)
    const char *block_hash_hex = "4860eb18bf1b1620e37e9490fc8a427514416fd75159ab86688e9a8300000000";

    // Step 1: SHA256d the coinbase transaction and verify merkle root
    uint8_t coinbase_tx[256];
    size_t coinbase_len = bb_str_hex_to_bytes(coinbase_tx_hex, coinbase_tx, sizeof(coinbase_tx));
    TEST_ASSERT_GREATER_THAN_INT(0, coinbase_len);

    uint8_t computed_merkle[32];
    sha256d(coinbase_tx, coinbase_len, computed_merkle);

    uint8_t expected_merkle[32];
    bb_str_hex_to_bytes(merkle_root_hex, expected_merkle, 32);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_merkle, computed_merkle, 32);

    // Step 2: serialize_header with block params and verify 80-byte header
    uint8_t prevhash[32];
    bb_str_hex_to_bytes(prevhash_hex, prevhash, 32);

    uint8_t merkle_root[32];
    bb_str_hex_to_bytes(merkle_root_hex, merkle_root, 32);

    uint8_t computed_header[80];
    serialize_header(1, prevhash, merkle_root, 0x4966bc61, 0x1d00ffff, 0x9962e301, computed_header);

    uint8_t expected_header[80];
    bb_str_hex_to_bytes(header_hex, expected_header, 80);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_header, computed_header, 80);

    // Step 3: SHA256d the header and verify block hash
    uint8_t computed_hash[32];
    sha256d(computed_header, 80, computed_hash);

    uint8_t expected_hash[32];
    bb_str_hex_to_bytes(block_hash_hex, expected_hash, 32);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, computed_hash, 32);
}

// Test: test_block170_merkle_and_hash
// Validates merkle root construction and header hashing for block #170 (2-tx block)
void test_block170_merkle_and_hash(void)
{
    // Coinbase hash (internal)
    const char *coinbase_hash_hex = "82501c1178fa0b222c1f3d474ec726b832013f0a532b44bb620cce8624a5feb1";

    // Branch[0] (internal)
    const char *branch_hex = "169e1e83e930853391bc6f35f605c6754cfead57cf8387639d3b4096c54f18f4";

    // Expected merkle root (internal)
    const char *expected_merkle_hex = "ff104ccb05421ab93e63f8c3ce5c2c2e9dbb37de2764b3a3175c8166562cac7d";

    // Full header hex
    const char *header_hex = "0100000055bd840a78798ad0da853f68974f3d183e2bd1db6a842c1feecf222a00000000ff104ccb05421ab93e63f8c3ce5c2c2e9dbb37de2764b3a3175c8166562cac7d51b96a49ffff001d283e9e70";

    // Expected block hash (internal)
    const char *block_hash_hex = "eea2d48d2fced4346842835c659e493d323f06d4034469a8905714d100000000";

    // Step 1: Convert coinbase_hash and branch from hex
    uint8_t coinbase_hash[32];
    bb_str_hex_to_bytes(coinbase_hash_hex, coinbase_hash, 32);

    uint8_t branch[1][32];
    bb_str_hex_to_bytes(branch_hex, branch[0], 32);

    // Step 2: build_merkle_root and verify
    uint8_t computed_merkle[32];
    build_merkle_root(coinbase_hash, branch, 1, computed_merkle);

    uint8_t expected_merkle[32];
    bb_str_hex_to_bytes(expected_merkle_hex, expected_merkle, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_merkle, computed_merkle, 32);

    // Step 3: Convert header from hex
    uint8_t header[80];
    bb_str_hex_to_bytes(header_hex, header, 80);

    // Step 4: SHA256d the header and verify block hash
    uint8_t computed_hash[32];
    sha256d(header, 80, computed_hash);

    uint8_t expected_hash[32];
    bb_str_hex_to_bytes(block_hash_hex, expected_hash, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, computed_hash, 32);
}

// Test: test_decode_stratum_prevhash_real
// Validates decode_stratum_prevhash with real block #1 data
void test_decode_stratum_prevhash_real(void)
{
    // Stratum hex (8 groups of 4 bytes, each group byte-reversed)
    const char *stratum_hex = "0a8ce26f72b3f1b646a2a6c14ff763ae65831e939c085ae10019d66800000000";

    // Expected internal byte order
    const char *expected_hex = "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000";

    uint8_t prevhash[32];
    decode_stratum_prevhash(stratum_hex, prevhash);

    uint8_t expected[32];
    bb_str_hex_to_bytes(expected_hex, expected, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, prevhash, 32);
}

// End-to-end Stratum pipeline test using Bitcoin block #1
void test_stratum_pipeline_block1(void)
{
    const char *version_hex = "00000001";
    const char *stratum_prevhash = "0a8ce26f72b3f1b646a2a6c14ff763ae65831e939c085ae10019d66800000000";
    const char *ntime_hex = "4966bc61";
    const char *nbits_hex = "1d00ffff";
    uint32_t nonce = 0x9962e301;

    const char *coinb1_hex = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000";

    const char *block_hash_hex = "4860eb18bf1b1620e37e9490fc8a427514416fd75159ab86688e9a8300000000";

    uint8_t prevhash[32];
    decode_stratum_prevhash(stratum_prevhash, prevhash);

    uint8_t coinb1[256];
    size_t coinb1_len = bb_str_hex_to_bytes(coinb1_hex, coinb1, sizeof(coinb1));
    uint8_t coinbase_hash[32];
    build_coinbase_hash(coinb1, coinb1_len, NULL, 0, NULL, 0, NULL, 0, coinbase_hash);

    uint8_t merkle_root[32];
    build_merkle_root(coinbase_hash, NULL, 0, merkle_root);

    uint32_t version = (uint32_t)strtoul(version_hex, NULL, 16);
    uint32_t ntime = (uint32_t)strtoul(ntime_hex, NULL, 16);
    uint32_t nbits = (uint32_t)strtoul(nbits_hex, NULL, 16);

    uint8_t header[80];
    serialize_header(version, prevhash, merkle_root, ntime, nbits, nonce, header);

    uint8_t hash[32];
    sha256d(header, 80, hash);

    uint8_t expected_hash[32];
    bb_str_hex_to_bytes(block_hash_hex, expected_hash, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, hash, 32);
}

void test_difficulty_to_target_diff1(void)
{
    // LE convention: diff1 target has 0xFFFF at bytes [26-27]
    uint8_t target[32];
    const uint8_t expected[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    };

    difficulty_to_target(1.0, target);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, target, 32);
}

void test_difficulty_to_target_easy(void)
{
    // diff 0.001: val = 65535000 = 0x03E7FC18
    // LE: byte[26]=0x18, [27]=0xFC, [28]=0xE7, [29]=0x03
    uint8_t target[32];
    const uint8_t expected[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x18, 0xFC, 0xE7, 0x03, 0x00, 0x00,
    };

    difficulty_to_target(0.001, target);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, target, 32);
}

void test_difficulty_to_target_hard(void)
{
    // diff 32768: val = 65535/32768 ≈ 1.9999694824
    // LE: byte[26]=0x01 (integer), byte[25]=0xFF, byte[24]=0xFE (fractional)
    uint8_t target[32];
    const uint8_t expected[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFE, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    difficulty_to_target(32768.0, target);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, target, 32);
}

// difficulty_to_target edge case tests

void test_difficulty_to_target_nan(void)
{
    uint8_t target[32];
    difficulty_to_target(NAN, target);
    // NaN should be clamped to minimum diff — target must be valid
    TEST_ASSERT_TRUE(is_target_valid(target));
}

void test_difficulty_to_target_inf(void)
{
    uint8_t target[32];
    difficulty_to_target(INFINITY, target);
    TEST_ASSERT_TRUE(is_target_valid(target));
}

void test_difficulty_to_target_neg_inf(void)
{
    uint8_t target[32];
    difficulty_to_target(-INFINITY, target);
    TEST_ASSERT_TRUE(is_target_valid(target));
}

void test_difficulty_to_target_negative(void)
{
    uint8_t target[32];
    difficulty_to_target(-1.0, target);
    TEST_ASSERT_TRUE(is_target_valid(target));
}

void test_difficulty_to_target_zero(void)
{
    uint8_t target[32];
    difficulty_to_target(0.0, target);
    TEST_ASSERT_TRUE(is_target_valid(target));
}

void test_difficulty_to_target_tiny(void)
{
    uint8_t target[32];
    difficulty_to_target(1e-10, target);
    // Clamped to 0.001 — target must be valid
    TEST_ASSERT_TRUE(is_target_valid(target));
}

void test_difficulty_to_target_normal(void)
{
    uint8_t target[32];
    difficulty_to_target(512.0, target);
    TEST_ASSERT_TRUE(is_target_valid(target));
    // At diff 512, LE MSBs should be zero
    TEST_ASSERT_EQUAL_UINT8(0, target[31]);
    TEST_ASSERT_EQUAL_UINT8(0, target[30]);
}
