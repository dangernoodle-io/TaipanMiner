#include "unity.h"
#include "mining.h"
#include "work.h"
#include "sha256.h"
#include <string.h>

// Test pinning: both S3 hot-loop copies must produce identical SHA-256d
// outputs for identical (work, nonce) inputs. The function-pointer dispatch
// (hw_hash_nonce, mining.c lines 482-498) and the inlined copy
// (mine_nonce_range, mining.c lines 576-592) implement the same algorithm.
//
// Host tests use the SW backend path, which is the reference that both
// hardware paths must stay in sync with. This test verifies mine_nonce_range
// executes the mining loop correctly; any divergence in the two copies
// on real hardware (device tests) would show up as missed/false positives.

static void setup_test_work(mining_work_t *work)
{
    memset(work, 0, sizeof(*work));
    const char *prevhash_hex = "0a8ce26f72b3f1b646a2a6c14ff763ae65831e939c085ae10019d66800000000";
    const char *coinb1_hex = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000";

    uint8_t prevhash[32];
    decode_stratum_prevhash(prevhash_hex, prevhash);

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
    strncpy(work->job_id, "test-loop-pin", sizeof(work->job_id) - 1);
    work->job_id[sizeof(work->job_id) - 1] = '\0';
    strncpy(work->extranonce2_hex, "00000000", sizeof(work->extranonce2_hex) - 1);
    work->extranonce2_hex[sizeof(work->extranonce2_hex) - 1] = '\0';
    work->work_seq = 1;
}

void test_mining_hotloop_finds_known_share(void)
{
    // Test mine_nonce_range with SW backend: verify it finds a known-good share.
    // Block #1 at nonce 0x9962e301 meets difficulty 1.0 target.
    // This verifies the mining loop executes correctly end-to-end.

    mining_work_t work;
    setup_test_work(&work);

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
    TEST_ASSERT_EQUAL_STRING("9962e301", result.nonce_hex);
}

void test_mining_hotloop_rejects_non_matching_nonce(void)
{
    // Test mine_nonce_range correctly rejects a nonce that doesn't meet target.
    // Use an arbitrary nonce that won't produce a valid hash for difficulty 1.

    mining_work_t work;
    setup_test_work(&work);

    sw_backend_ctx_t ctx;
    hash_backend_t backend;
    sw_backend_setup(&backend, &ctx);

    mine_params_t params = {
        .nonce_start = 0x00000001,
        .nonce_end = 0x00000001,
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
}
