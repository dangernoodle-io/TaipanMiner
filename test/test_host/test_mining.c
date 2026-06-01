#include "unity.h"
#include "mining.h"
#ifdef ASIC_CHIP
#include "board.h"
#endif
#include "work.h"
#include "sha256.h"
#include "share_validate.h"
#include "bb_byte_order.h"
#include <stdio.h>
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

// --- expected_ghs tests (TA-339) ---

void test_mining_get_expected_ghs_null_out(void)
{
    TEST_ASSERT_FALSE(mining_get_expected_ghs(500.0f, NULL));
}

#ifdef ASIC_CHIP
void test_mining_get_expected_ghs_asic_freq_set(void)
{
    double ghs = -1.0;
    // bitaxe-601 (BM1370): 894 small_cores, 1 chip → 500 * 894 * 1 / 1000 = 447.0
    bool ok = mining_get_expected_ghs(500.0f, &ghs);
    TEST_ASSERT_TRUE(ok);
    double expected = 500.0 * (double)BOARD_SMALL_CORES * (double)BOARD_ASIC_COUNT / 1000.0;
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, expected, ghs);
}

void test_mining_get_expected_ghs_asic_freq_zero(void)
{
    double ghs = -1.0;
    TEST_ASSERT_FALSE(mining_get_expected_ghs(0.0f, &ghs));
}

void test_mining_get_expected_ghs_asic_freq_negative(void)
{
    double ghs = -1.0;
    TEST_ASSERT_FALSE(mining_get_expected_ghs(-1.0f, &ghs));
}
#else
void test_mining_get_expected_ghs_non_asic_with_microbench(void)
{
    mining_set_sha_microbench(1.631, 306.56);
    double ghs = -1.0;
    bool result = mining_get_expected_ghs(0.0f, &ghs);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_FLOAT_WITHIN(1e-8, 0.00030656, ghs);
}

void test_mining_get_expected_ghs_non_asic_no_microbench(void)
{
    double ghs = -1.0;
    bool result = mining_get_expected_ghs(0.0f, &ghs);
    TEST_ASSERT_FALSE(result);
}
#endif

// --- pool-effective hashrate tests (TA-344) ---

void test_mining_compute_pool_effective_hps_empty(void)
{
    double result = mining_compute_pool_effective_hps(0.0, 100.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, result);
}

void test_mining_compute_pool_effective_hps_uptime_too_short(void)
{
    double result = mining_compute_pool_effective_hps(1024.0, 0.5);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, result);
}

void test_mining_compute_pool_effective_hps_typical(void)
{
    // 1024 diff * 2^32 / 60 seconds = 1024 * 4294967296 / 60 = ~73243789312 H/s = ~73 GH/s
    double result = mining_compute_pool_effective_hps(1024.0, 60.0);
    double expected = 1024.0 * 4294967296.0 / 60.0;
    TEST_ASSERT_DOUBLE_WITHIN(expected * 1e-6, expected, result);
}

void test_mining_compute_pool_effective_hps_diff1_share(void)
{
    // 1 diff * 2^32 / 60 seconds = 4294967296 / 60 = ~71582787 H/s = ~71.6 MH/s
    double result = mining_compute_pool_effective_hps(1.0, 60.0);
    double expected = 1.0 * 4294967296.0 / 60.0;
    TEST_ASSERT_DOUBLE_WITHIN(expected * 1e-6, expected, result);
}

void test_mining_compute_pool_effective_hps_divide_by_zero_guard(void)
{
    // Zero uptime should return 0.0 (guard against division by zero)
    double result = mining_compute_pool_effective_hps(100.0, 0.0);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, result);
}

void test_mining_get_pool_effective_hashrate_host_stub(void)
{
    // Host stub returns 0.0 — live FreeRTOS path is ESP-only. Covers the stub.
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, mining_get_pool_effective_hashrate());
}

// TA-363: rolling-window accessor host stubs all return 0.0; ESP-only live path
// is exercised in firmware. Direct stub call covers the host build.
void test_mining_get_pool_effective_rolling_host_stubs(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, mining_get_pool_effective_1m());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, mining_get_pool_effective_10m());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, mining_get_pool_effective_1h());
}

// --- TA-396: byte-order regression tests ---

// Test: pack_target_word0 returns the TRUE most-significant word in correct byte order.
// The old buggy pack used target[28] as the HIGH byte (byte-reversed).
// E.g. for target[31..28] = {0x00,0x00,0x00,0x63} the old code returned 0x63000000,
// the correct code returns 0x00000063.
void test_pack_target_word0_exact_byte_order(void)
{
    uint8_t target[32];

    // Case 1: single-byte in target[28], zeros elsewhere
    // True MSB word = (target[31]<<24)|(target[30]<<16)|(target[29]<<8)|target[28]
    //               = 0x00000063
    // Old buggy result: (target[28]<<24)|... = 0x63000000
    memset(target, 0, 32);
    target[31] = 0x00;
    target[30] = 0x00;
    target[29] = 0x00;
    target[28] = 0x63;
    TEST_ASSERT_EQUAL_HEX32(0x00000063U, pack_target_word0(target));

    // Case 2: multi-byte — old reversed pack returns 0x78563412
    memset(target, 0, 32);
    target[31] = 0x12;
    target[30] = 0x34;
    target[29] = 0x56;
    target[28] = 0x78;
    TEST_ASSERT_EQUAL_HEX32(0x12345678U, pack_target_word0(target));
}

// Test: share_reverify agrees with SW sha256d for block #1 winning nonce.
//
// Uses setup_block1_work (version_mask=0, ver_bits=0) so the no-roll path is
// exercised. Computes the real hash via sha256d and checks:
//   (a) genuine (work, ver_bits=0, nonce, real_hash) → true
//   (b) real_hash with one byte flipped → false (simulates DPORT corruption)
void test_share_reverify_block1_nonce(void)
{
    mining_work_t work;
    setup_block1_work(&work);

    const uint32_t winning_nonce = 0x9962e301;

    // Reconstruct the real hash via SW sha256d (same path share_reverify uses).
    uint8_t hdr[80];
    memcpy(hdr, work.header, 80);
    set_header_nonce(hdr, winning_nonce);
    uint8_t real_hash[32];
    sha256d(hdr, 80, real_hash);

    // (a) Genuine hash must pass reverify.
    TEST_ASSERT_TRUE(share_reverify(&work, 0, winning_nonce, real_hash));

    // (b) Flip one byte to simulate a DPORT partial-corruption that slipped
    //     past the target compare — reverify must reject it.
    uint8_t corrupt_hash[32];
    memcpy(corrupt_hash, real_hash, 32);
    corrupt_hash[0] ^= 0xFF;
    TEST_ASSERT_FALSE(share_reverify(&work, 0, winning_nonce, corrupt_hash));
}

// Test: share_reverify with version rolling (mask != 0) covers the roll branch.
//
// Uses the block #1 work but adds a version mask.  The rolled version is
// computed identically to share_reverify's internal logic and burned into a
// fresh header before sha256d so the expected hash matches.
void test_share_reverify_version_rolling(void)
{
    mining_work_t work;
    setup_block1_work(&work);

    // Realistic BIP 320 mask; use ver_bits that differ from version in the
    // masked bits so the roll actually changes the header bytes.
    work.version_mask = 0x1FFFE000;
    work.version      = 0x20000000;

    const uint32_t ver_bits     = 0x00006000;   // within mask
    const uint32_t winning_nonce = 0x9962e301;  // block #1 nonce (arbitrary, just needs a hash)

    // Compute the rolled version exactly as share_reverify does.
    uint32_t rolled = (work.version & ~work.version_mask) | (ver_bits & work.version_mask);

    // Build the header with the rolled version and the nonce.
    uint8_t hdr[80];
    memcpy(hdr, work.header, 80);
    bb_store_le32(hdr, rolled);
    set_header_nonce(hdr, winning_nonce);

    // Compute expected hash via sha256d.
    uint8_t real_hash[32];
    sha256d(hdr, 80, real_hash);

    // (a) Genuine hash with correct ver_bits must pass.
    TEST_ASSERT_TRUE(share_reverify(&work, ver_bits, winning_nonce, real_hash));

    // (b) Wrong ver_bits (different rolled version) → hash mismatch → false.
    TEST_ASSERT_FALSE(share_reverify(&work, 0x00002000, winning_nonce, real_hash));
}

// --- mining_efficiency_jth tests ---

// Known case: bitaxe-601 live sample (pcore_mw=17273, hashrate=1071 GH/s → ~16.13 J/TH).
// With the old vcore×icore derivation (dropping BOARD_POWER_OFFSET_MW=5000 mW),
// expected would have read ~11.46 J/TH — ~30% too optimistic.
void test_mining_efficiency_jth_known_case(void)
{
    double result = mining_efficiency_jth(17273.0, 1071.0);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 17273.0 / 1071.0, result);
}

void test_mining_efficiency_jth_zero_hashrate(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -1.0, mining_efficiency_jth(17273.0, 0.0));
}

void test_mining_efficiency_jth_negative_hashrate(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -1.0, mining_efficiency_jth(17273.0, -1.0));
}

void test_mining_efficiency_jth_zero_power(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -1.0, mining_efficiency_jth(0.0, 1071.0));
}

void test_mining_efficiency_jth_negative_power(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -1.0, mining_efficiency_jth(-1.0, 1071.0));
}

// Offset-consistency: actual and expected now share the pcore_mw basis (canonical
// value including BOARD_POWER_OFFSET_MW). Two calls with the same power_mw and
// different hashrates must scale inversely (J/TH ∝ 1/GH/s).
void test_mining_efficiency_jth_inverse_hashrate_scaling(void)
{
    double e1 = mining_efficiency_jth(17273.0, 1000.0);
    double e2 = mining_efficiency_jth(17273.0, 2000.0);
    // doubling GH/s halves J/TH
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, e1 / 2.0, e2);
}

// Test: sw_hash_nonce early-reject observable directly (not through mine_nonce_range).
//
// Calibrated vectors (verified empirically by running the binary):
//   - nonce 0x9962e301 (genesis block): bswap32(state[7]) = 0x00000000 (many leading zeros)
//   - nonce 0x00000000 (arbitrary):     bswap32(state[7]) = 0x1bf57b5a (nonzero)
//
// Case A (HASH_CHECK): genesis nonce with loose target (diff 0.001).
//   target_word0 from difficulty_to_target(0.001) is large (easy target).
//   bswap32(state[7])=0 <= target_word0 → HASH_CHECK.
//
// Case B (HASH_MISS): nonce 0 whose MSB word is 0x1bf57b5a.
//   Hand-set target so target_word0 = 0x1bf57b59 (one below the hash's MSB word).
//   0x1bf57b5a > 0x1bf57b59 → HASH_MISS.
//   With the OLD no-op filter (byte-reversed pack), target_word0 would be
//   byte-reversed and the comparison broken — returning HASH_CHECK wrongly.
void test_sw_hash_nonce_rejects_over_target(void)
{
    mining_work_t work;
    setup_block1_work(&work);

    sw_backend_ctx_t ctx;
    hash_backend_t backend;
    sw_backend_setup(&backend, &ctx);

    uint8_t block2[64];
    build_block2(block2, work.header);

    // --- Case A: loose target, genesis nonce -> HASH_CHECK ---
    difficulty_to_target(0.001, work.target);
    sw_prepare_job(&backend, &work, block2);
    uint8_t hash_out[32];
    hash_result_t result_a = sw_hash_nonce(&backend, 0x9962e301, hash_out);
    TEST_ASSERT_EQUAL_INT(HASH_CHECK, result_a);

    // --- Case B: tight target, nonce 0 -> HASH_MISS ---
    // nonce 0's true MSB word is 0x1bf57b5a (calibrated).
    // Set target's MSB word to 0x1bf57b59 (strictly below hash MSB word).
    // Correct pack: target[31..28] = {0x1b, 0xf5, 0x7b, 0x59}
    //   -> target_word0 = 0x1bf57b59
    // Old buggy pack: target[28..31] as high bytes -> byte-reversed, filter broken.
    memset(work.target, 0, 32);
    work.target[31] = 0x1b;
    work.target[30] = 0xf5;
    work.target[29] = 0x7b;
    work.target[28] = 0x59; // one below 0x1bf57b5a
    sw_prepare_job(&backend, &work, block2);
    hash_result_t result_b = sw_hash_nonce(&backend, 0x00000000, hash_out);
    TEST_ASSERT_EQUAL_INT(HASH_MISS, result_b);
}
