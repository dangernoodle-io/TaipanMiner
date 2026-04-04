#include "unity.h"
#include "sha256.h"
#include <string.h>

// Test: SHA-256 of empty string
void test_sha256_empty_string(void)
{
    uint8_t hash[32];
    const uint8_t expected[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
    };

    sha256(NULL, 0, hash);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, hash, 32);
}

// Test: SHA-256 of "abc"
void test_sha256_abc(void)
{
    uint8_t hash[32];
    const char *input = "abc";
    const uint8_t expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };

    sha256((const uint8_t *)input, 3, hash);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, hash, 32);
}

// Test: SHA-256 of a 55-byte message (spans two blocks)
void test_sha256_two_blocks(void)
{
    uint8_t hash[32];
    const char *input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const uint8_t expected[32] = {
        0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
        0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
        0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
        0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1,
    };

    sha256((const uint8_t *)input, strlen(input), hash);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, hash, 32);
}

// Test: SHA256d (double SHA-256) of empty data
// Used in Bitcoin as the standard hash function
void test_sha256d_known(void)
{
    uint8_t hash[32];
    const uint8_t expected[32] = {
        0x5d, 0xf6, 0xe0, 0xe2, 0x76, 0x13, 0x59, 0xd3,
        0x0a, 0x82, 0x75, 0x05, 0x8e, 0x29, 0x9f, 0xcc,
        0x03, 0x81, 0x53, 0x45, 0x45, 0xf5, 0x5c, 0xf4,
        0x3e, 0x41, 0x98, 0x3f, 0x5d, 0x4c, 0x94, 0x56,
    };

    sha256d(NULL, 0, hash);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, hash, 32);
}

// Test: Midstate optimization
// Process 64-byte block, clone context (save midstate), then update with remaining data
// Should produce same result as hashing entire data in one go
void test_sha256_midstate(void)
{
    sha256_ctx_t ctx1, ctx2;
    uint8_t hash1[32], hash2[32];

    // Test data: 80 bytes (first 64 as block, remaining 16 as update)
    const uint8_t data[80] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
        0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
        0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
        0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
        0x29, 0xab, 0x5f, 0x49, 0xff, 0xff, 0x00, 0x1d,
        0x1d, 0xac, 0x2b, 0x7c, 0x00, 0x00, 0x00, 0x00,
    };

    // Method 1: Hash all 80 bytes in one shot
    sha256(data, 80, hash1);

    // Method 2: Process first 64 bytes as block, clone, then update with remaining 16
    sha256_init(&ctx1);
    sha256_process_block(&ctx1, data);
    sha256_clone(&ctx2, &ctx1);
    sha256_update(&ctx2, data + 64, 16);
    sha256_final(&ctx2, hash2);

    // Results must match
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hash1, hash2, 32);
}

// Test: SHA256d of Bitcoin genesis block header
// Genesis block header (80 bytes):
// 01000000 (version=1) + 32 zero bytes (prevhash) + merkle_root (32 bytes) +
// 29ab5f49 (ntime) + ffff001d (nbits) + 1dac2b7c (nonce)
void test_sha256_genesis_header(void)
{
    uint8_t hash[32];
    uint8_t genesis_header[80] = {
        0x01, 0x00, 0x00, 0x00, // version (little-endian)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // prevhash (all zeros)
        0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
        0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
        0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
        0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a, // merkle_root
        0x29, 0xab, 0x5f, 0x49, // ntime (little-endian)
        0xff, 0xff, 0x00, 0x1d, // nbits (little-endian)
        0x1d, 0xac, 0x2b, 0x7c, // nonce (little-endian)
    };

    // Expected: SHA256d of genesis header = 6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000
    // (in internal byte order, little-endian viewed as bytes)
    const uint8_t expected[32] = {
        0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
        0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
        0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
        0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    sha256d(genesis_header, 80, hash);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, hash, 32);
}

// Test: sha256_transform_words produces identical output to sha256_transform
// This validates the word-based variant (used in SW mining optimization)
void test_sha256_transform_words(void)
{
    // Use a simple test block (known pattern)
    uint8_t block[64];
    memset(block, 0, 64);
    block[0] = 0x01;
    block[1] = 0x00;
    block[2] = 0x00;
    block[3] = 0x00;

    // Convert block to words (big-endian)
    uint32_t words[16];
    for (int i = 0; i < 16; i++) {
        words[i] = ((uint32_t)block[i*4] << 24) |
                   ((uint32_t)block[i*4+1] << 16) |
                   ((uint32_t)block[i*4+2] << 8) |
                   (uint32_t)block[i*4+3];
    }

    // Run both transforms from same initial state
    uint32_t state1[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint32_t state2[8];
    memcpy(state2, state1, 32);

    sha256_transform(state1, block);
    sha256_transform_words(state2, words);

    TEST_ASSERT_EQUAL_UINT32_ARRAY(state1, state2, 8);
}

// Test: SHA-256 transform performance benchmark
// Validates consistency of sha256_transform and sha256_transform_words over high iteration count
void test_sha256_transform_performance(void)
{
    // Initial state (H0)
    uint32_t state1[8], state2[8];
    const uint32_t H0[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    // Test block (arbitrary but deterministic)
    uint8_t block[64];
    uint32_t words[16];
    for (int i = 0; i < 64; i++) block[i] = (uint8_t)(i * 7 + 13);
    for (int i = 0; i < 16; i++) words[i] = ((uint32_t)block[i*4] << 24) |
                                              ((uint32_t)block[i*4+1] << 16) |
                                              ((uint32_t)block[i*4+2] << 8) |
                                              (uint32_t)block[i*4+3];

    // Run 100K iterations of each to verify consistency
    uint32_t iterations = 100000;
    for (uint32_t i = 0; i < iterations; i++) {
        memcpy(state1, H0, 32);
        sha256_transform(state1, block);
        memcpy(state2, H0, 32);
        sha256_transform_words(state2, words);
    }

    // Final states must match
    TEST_ASSERT_EQUAL_HEX32_ARRAY(state1, state2, 8);
}
