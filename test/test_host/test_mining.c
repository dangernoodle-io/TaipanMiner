#include "unity.h"
#include "mining.h"
#include "work.h"
#include "sha256.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

// --- Helper: set up Bitcoin block #1 work for mining tests ---
static void setup_block1_work(mining_work_t *work)
{
    memset(work, 0, sizeof(*work));

    // Block #1 stratum data (genesis block next, with known work parameters)
    const char *stratum_prevhash = "0a8ce26f72b3f1b646a2a6c14ff763ae65831e939c085ae10019d66800000000";
    const char *coinb1_hex = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000";

    uint8_t prevhash[32];
    decode_stratum_prevhash(stratum_prevhash, prevhash);

    uint8_t coinb1[256];
    size_t coinb1_len = hex_to_bytes(coinb1_hex, coinb1, sizeof(coinb1));
    uint8_t coinbase_hash[32];
    build_coinbase_hash(coinb1, coinb1_len, NULL, 0, NULL, 0, NULL, 0, coinbase_hash);

    uint8_t merkle_root[32];
    build_merkle_root(coinbase_hash, NULL, 0, merkle_root);

    serialize_header(1, prevhash, merkle_root, 0x4966bc61, 0x1d00ffff, 0, work->header);
    difficulty_to_target(1.0, work->target);
    work->version = 1;
    work->ntime = 0x4966bc61;
    strncpy(work->job_id, "test-job-1", sizeof(work->job_id) - 1);
    work->job_id[sizeof(work->job_id) - 1] = '\0';
    strncpy(work->extranonce2_hex, "00000000", sizeof(work->extranonce2_hex) - 1);
    work->extranonce2_hex[sizeof(work->extranonce2_hex) - 1] = '\0';
    work->work_seq = 1;
}

// --- Mock backends for testing ---

// Counting backend: counts calls to hash_nonce and tracks last nonce
typedef struct {
    int call_count;
    uint32_t last_nonce;
} counting_ctx_t;

static void counting_prepare(hash_backend_t *b, const mining_work_t *w, const uint8_t bl[64])
{
    (void)b;
    (void)w;
    (void)bl;
}

static hash_result_t counting_hash_miss(hash_backend_t *b, uint32_t nonce, uint8_t hash_out[32])
{
    counting_ctx_t *c = (counting_ctx_t *)b->ctx;
    c->call_count++;
    c->last_nonce = nonce;
    (void)hash_out;
    return HASH_MISS;
}

static hash_result_t counting_hash_hit(hash_backend_t *b, uint32_t nonce, uint8_t hash_out[32])
{
    counting_ctx_t *c = (counting_ctx_t *)b->ctx;
    c->call_count++;
    c->last_nonce = nonce;
    // all-zero hash will meet any target
    memset(hash_out, 0, 32);
    return HASH_CHECK;
}

// Test: SW backend finds Bitcoin block #1 at known nonce
void test_sw_backend_finds_block1_share(void)
{
    mining_work_t work;
    setup_block1_work(&work);

    sw_backend_ctx_t ctx;
    hash_backend_t backend;
    sw_backend_setup(&backend, &ctx);

    // Mine just the single known nonce
    mine_params_t params = {
        .nonce_start = 0x9962e301,
        .nonce_end = 0x9962e301,
        .yield_mask = 0xFFFFFFFF,
        .log_mask = 0xFFFFFFFF,
        .ver_bits = 0,
        .base_version = 1,
        .version_mask = 0,
    };

    mining_result_t result;
    bool found = false;
    mine_nonce_range(&backend, &work, &params, &result, &found);

    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_STRING("9962e301", result.nonce_hex);
    TEST_ASSERT_EQUAL_STRING("test-job-1", result.job_id);
}

// Test: SW backend early reject at low difficulty
void test_sw_backend_early_reject_low_diff(void)
{
    mining_work_t work;
    setup_block1_work(&work);

    // Set a very easy difficulty (0.001) — target_word0 will have nonzero upper 16 bits
    difficulty_to_target(0.001, work.target);

    sw_backend_ctx_t ctx;
    hash_backend_t backend;
    sw_backend_setup(&backend, &ctx);

    // Mine block #1 nonce — at diff 0.001 it should definitely be found
    mine_params_t params = {
        .nonce_start = 0x9962e301,
        .nonce_end = 0x9962e301,
        .yield_mask = 0xFFFFFFFF,
        .log_mask = 0xFFFFFFFF,
        .ver_bits = 0,
        .base_version = 1,
        .version_mask = 0,
    };

    mining_result_t result;
    bool found = false;
    mine_nonce_range(&backend, &work, &params, &result, &found);

    TEST_ASSERT_TRUE(found);
}

// Test: SW backend early reject at high difficulty
void test_sw_backend_early_reject_high_diff(void)
{
    mining_work_t work;
    setup_block1_work(&work);

    // Set a harder difficulty — block 1 nonce should still meet genesis target
    difficulty_to_target(1.0, work.target);

    sw_backend_ctx_t ctx;
    hash_backend_t backend;
    sw_backend_setup(&backend, &ctx);

    mine_params_t params = {
        .nonce_start = 0x9962e301,
        .nonce_end = 0x9962e301,
        .yield_mask = 0xFFFFFFFF,
        .log_mask = 0xFFFFFFFF,
        .ver_bits = 0,
        .base_version = 1,
        .version_mask = 0,
    };

    mining_result_t result;
    bool found = false;
    mine_nonce_range(&backend, &work, &params, &result, &found);

    TEST_ASSERT_TRUE(found);
}

// Test: mine_nonce_range calls backend exact number of times for range [0, 9]
void test_mine_nonce_range_counts(void)
{
    counting_ctx_t cctx = {0, 0};
    hash_backend_t backend = {
        .init = NULL,
        .prepare_job = counting_prepare,
        .hash_nonce = counting_hash_miss,
        .ctx = &cctx,
    };

    mining_work_t work;
    memset(&work, 0, sizeof(work));

    mine_params_t params = {
        .nonce_start = 0,
        .nonce_end = 9,
        .yield_mask = 0xFFFFFFFF,
        .log_mask = 0xFFFFFFFF,
        .ver_bits = 0,
        .base_version = 1,
        .version_mask = 0,
    };

    mine_nonce_range(&backend, &work, &params, NULL, NULL);

    TEST_ASSERT_EQUAL_INT(10, cctx.call_count);
    TEST_ASSERT_EQUAL_UINT32(9, cctx.last_nonce);
}

// Test: mine_nonce_range stops on first hit when found_out is set
void test_mine_nonce_range_stops_on_hit(void)
{
    counting_ctx_t cctx = {0, 0};
    hash_backend_t backend = {
        .init = NULL,
        .prepare_job = counting_prepare,
        .hash_nonce = counting_hash_hit,
        .ctx = &cctx,
    };

    mining_work_t work;
    memset(&work, 0, sizeof(work));
    // Set an easy target so all-zero hash meets it
    memset(work.target, 0xFF, 32);

    mine_params_t params = {
        .nonce_start = 0,
        .nonce_end = 99,
        .yield_mask = 0xFFFFFFFF,
        .log_mask = 0xFFFFFFFF,
        .ver_bits = 0,
        .base_version = 1,
        .version_mask = 0,
    };

    mining_result_t result;
    bool found = false;
    mine_nonce_range(&backend, &work, &params, &result, &found);

    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_STRING("00000000", result.nonce_hex);
    // Should have stopped after first nonce (call_count should be 1)
    TEST_ASSERT_EQUAL_INT(1, cctx.call_count);
}

// Test: mine_nonce_range handles no-hit case (found_out stays false)
void test_mine_nonce_range_no_hit(void)
{
    counting_ctx_t cctx = {0, 0};
    hash_backend_t backend = {
        .init = NULL,
        .prepare_job = counting_prepare,
        .hash_nonce = counting_hash_miss,
        .ctx = &cctx,
    };

    mining_work_t work;
    memset(&work, 0, sizeof(work));

    mine_params_t params = {
        .nonce_start = 100,
        .nonce_end = 109,
        .yield_mask = 0xFFFFFFFF,
        .log_mask = 0xFFFFFFFF,
        .ver_bits = 0,
        .base_version = 1,
        .version_mask = 0,
    };

    mining_result_t result;
    bool found = false;
    mine_nonce_range(&backend, &work, &params, &result, &found);

    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_EQUAL_INT(10, cctx.call_count);
}

// Test: result has version_hex when version rolling is active
// Pool expects ver_bits only (XOR delta from base version), not the full
// rolled version.  Submitting the full version causes "Difficulty too low"
// because the pool hashes with a wrong block version.
void test_mine_result_has_version_hex(void)
{
    mining_work_t work;
    setup_block1_work(&work);
    work.version_mask = 0x1FFFE000;  // realistic BM1370 mask
    memset(work.target, 0xFF, 32);

    counting_ctx_t cctx = {0, 0};
    hash_backend_t backend = {
        .init = NULL,
        .prepare_job = counting_prepare,
        .hash_nonce = counting_hash_hit,
        .ctx = &cctx,
    };

    mine_params_t params = {
        .nonce_start = 0,
        .nonce_end = 0,
        .yield_mask = 0xFFFFFFFF,
        .log_mask = 0xFFFFFFFF,
        .ver_bits = 0x00006000,
        .base_version = 0x20000000,
        .version_mask = work.version_mask,
    };

    mining_result_t result;
    bool found = false;
    mine_nonce_range(&backend, &work, &params, &result, &found);

    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(result.version_hex[0] != '\0');
    // Must be ver_bits only, NOT full rolled version (0x20006000)
    TEST_ASSERT_EQUAL_STRING("00006000", result.version_hex);
}

// Test: pack_target_word0 with difficulty 1.0
void test_pack_target_word0_diff1(void)
{
    mining_work_t work;
    setup_block1_work(&work);
    difficulty_to_target(1.0, work.target);

    uint32_t word0 = pack_target_word0(work.target);

    // Diff 1.0: target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
    // target[28]=0x00, target[29]=0x00, target[30]=0x00, target[31]=0x00
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, word0);
}

// Test: pack_target_word0 with difficulty 0.001 (easier)
void test_pack_target_word0_easy_diff(void)
{
    mining_work_t work;
    setup_block1_work(&work);
    difficulty_to_target(0.001, work.target);

    uint32_t word0 = pack_target_word0(work.target);

    // Diff 0.001 is easier (higher target). target[28] will have nonzero bits.
    // Just verify it's nonzero (easier than difficulty 1.0)
    TEST_ASSERT_NOT_EQUAL(0, word0);
}

// Test: pack_target_word0 with difficulty 100.0 (harder)
void test_pack_target_word0_hard_diff(void)
{
    mining_work_t work;
    setup_block1_work(&work);
    difficulty_to_target(100.0, work.target);

    uint32_t word0 = pack_target_word0(work.target);

    // Diff 100.0 is harder (lower target). target[28] should be 0x00.
    // target[29..31] will be smaller than diff 1.0
    uint32_t word0_diff1 = pack_target_word0(work.target);
    // For harder diff, target bytes [28-31] are zero
    TEST_ASSERT_EQUAL_HEX32(0, word0);
}

// Test: build_block2 produces correct padding structure
void test_build_block2_padding(void)
{
    mining_work_t work;
    setup_block1_work(&work);

    uint8_t block2[64];
    build_block2(block2, work.header);

    // Check: bytes 0-15 copied from header[64-79]
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_HEX8(work.header[64 + i], block2[i]);
    }

    // Check: byte 16 = 0x80 (SHA-256 padding marker)
    TEST_ASSERT_EQUAL_HEX8(0x80, block2[16]);

    // Check: bytes 17-61 are zero
    for (int i = 17; i < 62; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, block2[i]);
    }

    // Check: bytes 62-63 = 0x02 0x80 (message length encoding: 512 bits = 0x200)
    TEST_ASSERT_EQUAL_HEX8(0x02, block2[62]);
    TEST_ASSERT_EQUAL_HEX8(0x80, block2[63]);
}

// Test: package_result with no version rolling (ver_bits=0)
void test_package_result_no_version_rolling(void)
{
    mining_work_t work;
    setup_block1_work(&work);
    work.ntime = 0x12345678;
    strncpy(work.job_id, "job-abc", sizeof(work.job_id) - 1);
    work.job_id[sizeof(work.job_id) - 1] = '\0';
    strncpy(work.extranonce2_hex, "aabbccdd", sizeof(work.extranonce2_hex) - 1);
    work.extranonce2_hex[sizeof(work.extranonce2_hex) - 1] = '\0';

    mining_result_t result;
    package_result(&result, &work, 0xdeadbeef, 0);

    TEST_ASSERT_EQUAL_STRING("job-abc", result.job_id);
    TEST_ASSERT_EQUAL_STRING("aabbccdd", result.extranonce2_hex);
    TEST_ASSERT_EQUAL_STRING("12345678", result.ntime_hex);
    TEST_ASSERT_EQUAL_STRING("deadbeef", result.nonce_hex);
    // version_hex should be empty when ver_bits=0
    TEST_ASSERT_EQUAL_STRING("", result.version_hex);
}

// Test: package_result submits ver_bits directly, not full rolled version
void test_package_result_version_rolling_submits_ver_bits(void)
{
    mining_work_t work;
    setup_block1_work(&work);
    work.ntime = 0x12345678;
    strncpy(work.job_id, "job-abc", sizeof(work.job_id) - 1);
    work.job_id[sizeof(work.job_id) - 1] = '\0';
    strncpy(work.extranonce2_hex, "aabbccdd", sizeof(work.extranonce2_hex) - 1);
    work.extranonce2_hex[sizeof(work.extranonce2_hex) - 1] = '\0';

    mining_result_t result;
    // ver_bits=0x00006000: pool expects this, not 0x20006000 (full rolled)
    package_result(&result, &work, 0xdeadbeef, 0x00006000);

    TEST_ASSERT_EQUAL_STRING("00006000", result.version_hex);
}
