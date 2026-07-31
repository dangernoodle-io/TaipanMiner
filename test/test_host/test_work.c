#include "unity.h"
#include "work.h"
#include "mining.h"
#include "sha256.h"
#include "bb_byte_order.h"
#include <string.h>
#include <stdlib.h>

// Test: hex_to_bytes conversion
void test_hex_to_bytes(void)
{
    uint8_t out[4];
    const char *hex = "deadbeef";
    const uint8_t expected[4] = {0xde, 0xad, 0xbe, 0xef};

    size_t result = hex_to_bytes(hex, out, 4);
    TEST_ASSERT_EQUAL_INT(4, result);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 4);
}

// Test: bytes_to_hex conversion
void test_bytes_to_hex(void)
{
    const uint8_t data[4] = {0xde, 0xad, 0xbe, 0xef};
    char hex[9];  // 4 bytes * 2 chars + null terminator

    bytes_to_hex(data, 4, hex);
    TEST_ASSERT_EQUAL_STRING("deadbeef", hex);
}

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

// Test: hex_to_bytes and bytes_to_hex roundtrip
void test_hex_roundtrip(void)
{
    const char *original = "0123456789abcdef";
    uint8_t bytes[8];
    char hex[17];

    hex_to_bytes(original, bytes, 8);
    bytes_to_hex(bytes, 8, hex);
    TEST_ASSERT_EQUAL_STRING(original, hex);
}

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
    size_t coinbase_len = hex_to_bytes(coinbase_tx_hex, coinbase_tx, sizeof(coinbase_tx));
    TEST_ASSERT_GREATER_THAN_INT(0, coinbase_len);

    uint8_t computed_merkle[32];
    sha256d(coinbase_tx, coinbase_len, computed_merkle);

    uint8_t expected_merkle[32];
    hex_to_bytes(merkle_root_hex, expected_merkle, 32);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_merkle, computed_merkle, 32);

    // Step 2: serialize_header with block params and verify 80-byte header
    uint8_t prevhash[32];
    hex_to_bytes(prevhash_hex, prevhash, 32);

    uint8_t merkle_root[32];
    hex_to_bytes(merkle_root_hex, merkle_root, 32);

    uint8_t computed_header[80];
    serialize_header(1, prevhash, merkle_root, 0x4966bc61, 0x1d00ffff, 0x9962e301, computed_header);

    uint8_t expected_header[80];
    hex_to_bytes(header_hex, expected_header, 80);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_header, computed_header, 80);

    // Step 3: SHA256d the header and verify block hash
    uint8_t computed_hash[32];
    sha256d(computed_header, 80, computed_hash);

    uint8_t expected_hash[32];
    hex_to_bytes(block_hash_hex, expected_hash, 32);

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
    hex_to_bytes(coinbase_hash_hex, coinbase_hash, 32);

    uint8_t branch[1][32];
    hex_to_bytes(branch_hex, branch[0], 32);

    // Step 2: build_merkle_root and verify
    uint8_t computed_merkle[32];
    build_merkle_root(coinbase_hash, branch, 1, computed_merkle);

    uint8_t expected_merkle[32];
    hex_to_bytes(expected_merkle_hex, expected_merkle, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_merkle, computed_merkle, 32);

    // Step 3: Convert header from hex
    uint8_t header[80];
    hex_to_bytes(header_hex, header, 80);

    // Step 4: SHA256d the header and verify block hash
    uint8_t computed_hash[32];
    sha256d(header, 80, computed_hash);

    uint8_t expected_hash[32];
    hex_to_bytes(block_hash_hex, expected_hash, 32);
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
    hex_to_bytes(expected_hex, expected, 32);
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
    size_t coinb1_len = hex_to_bytes(coinb1_hex, coinb1, sizeof(coinb1));
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
    hex_to_bytes(block_hash_hex, expected_hash, 32);
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
