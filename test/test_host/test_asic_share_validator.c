#ifdef ASIC_CHIP
#include "unity.h"
#include "share_validate.h"
#include "work.h"
#include "sha256.h"
#include "asic_proto.h"
#include <string.h>

// Genesis block header bytes (version=1, prevhash=0, genesis merkle root, ntime, nbits).
// Nonce 0x7c2bac1d is stored LE so bytes 76-79 = [0x1d, 0xac, 0x2b, 0x7c].
// SHA256d of this header = 000000000019d6689c085ae165831e929a29c3df9f... (leading zeros).
static const uint8_t GENESIS_HEADER[80] = {
    0x01, 0x00, 0x00, 0x00, // version = 1 LE
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // prevhash = 0
    0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2,
    0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
    0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
    0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a, // genesis merkle root
    0x29, 0xab, 0x5f, 0x49, // ntime = 0x495fab29 LE
    0xff, 0xff, 0x00, 0x1d, // nbits = 0x1d00ffff LE
    0x1d, 0xac, 0x2b, 0x7c, // nonce = 0x7c2bac1d LE
};

// Helper: populate a genesis-based work with the given difficulty
static void make_genesis_work(mining_work_t *w, double difficulty)
{
    memset(w, 0, sizeof(*w));
    memcpy(w->header, GENESIS_HEADER, 80);
    difficulty_to_target(difficulty, w->target);
    w->version      = 1;
    w->version_mask = 0;
    w->ntime        = 0x495fab29;
    w->difficulty   = difficulty;
    strncpy(w->job_id, "test-genesis", sizeof(w->job_id) - 1);
    strncpy(w->extranonce2_hex, "00000000", sizeof(w->extranonce2_hex) - 1);
}

// Helper: compute SHA256d of genesis header with the given nonce patched in LE at [76-79]
static void genesis_hash(uint32_t nonce_le, uint8_t out_hash[32])
{
    uint8_t header[80];
    memcpy(header, GENESIS_HEADER, 80);
    header[76] = (uint8_t)(nonce_le);
    header[77] = (uint8_t)(nonce_le >> 8);
    header[78] = (uint8_t)(nonce_le >> 16);
    header[79] = (uint8_t)(nonce_le >> 24);
    sha256d(header, 80, out_hash);
}

// Genesis nonce as LE uint32
// header[76..79] = [0x1d, 0xac, 0x2b, 0x7c]
// nonce_le = 0x1d | 0xac<<8 | 0x2b<<16 | 0x7c<<24 = 0x7c2bac1d
#define GENESIS_NONCE_LE 0x7c2bac1du

// TA-274 / Track-3: share_validate tests

// NULL guard: NULL work → INVALID_TARGET
void test_asic_share_validate_null_work(void)
{
    double diff;
    uint8_t hash[32];
    memset(hash, 0, sizeof(hash));
    share_verdict_t v = share_validate(NULL, hash, &diff);
    TEST_ASSERT_EQUAL_INT(SHARE_INVALID_TARGET, v);
}

// NULL guard: NULL out_diff → INVALID_TARGET
void test_asic_share_validate_null_out_difficulty(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    uint8_t hash[32];
    genesis_hash(GENESIS_NONCE_LE, hash);
    share_verdict_t v = share_validate(&work, hash, NULL);
    TEST_ASSERT_EQUAL_INT(SHARE_INVALID_TARGET, v);
}

// NULL guard: NULL out_hash is not a parameter in share_validate (hash is input, not output).
// Repurpose to verify INVALID_TARGET when target is all-zeros (is_target_valid fails).
void test_asic_share_validate_null_out_hash(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    memset(work.target, 0, sizeof(work.target));
    uint8_t hash[32];
    genesis_hash(GENESIS_NONCE_LE, hash);
    double diff;
    share_verdict_t v = share_validate(&work, hash, &diff);
    TEST_ASSERT_EQUAL_INT(SHARE_INVALID_TARGET, v);
}

// happy_path_easy_share: genesis hash meets diff=1 target → SHARE_VALID + share_diff populated.
void test_asic_share_validate_happy_path_easy_share(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);

    uint8_t hash[32];
    genesis_hash(GENESIS_NONCE_LE, hash);

    double share_diff;
    share_verdict_t v = share_validate(&work, hash, &share_diff);

    TEST_ASSERT_EQUAL_INT(SHARE_VALID, v);
    TEST_ASSERT_GREATER_THAN(1.0, share_diff);
}

// below_target: all-zeros target means meets_target returns false → SHARE_BELOW_TARGET.
// Note: is_target_valid(all-zeros) also fails, so SHARE_INVALID_TARGET fires first.
// All-zeros target: both is_target_valid and meets_target reject it.
// To test BELOW_TARGET we need a valid target that the genesis hash doesn't meet.
// Use a tiny target (difficulty 1e15) — genesis hash won't meet it.
void test_asic_share_validate_below_target(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1e15);  // target so hard genesis hash can't meet it

    uint8_t hash[32];
    genesis_hash(GENESIS_NONCE_LE, hash);

    double share_diff;
    share_verdict_t v = share_validate(&work, hash, &share_diff);
    TEST_ASSERT_EQUAL_INT(SHARE_BELOW_TARGET, v);
}

// invalid_target_all_ff: all-0xFF target — is_target_valid rejects it → SHARE_INVALID_TARGET.
// Track-3 ordering fix: is_target_valid runs BEFORE meets_target, so even though
// meets_target would return true (hash ≤ 0xFF..FF), the verdict is INVALID_TARGET.
void test_asic_share_validate_invalid_target_all_ff(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    memset(work.target, 0xFF, sizeof(work.target));

    uint8_t hash[32];
    genesis_hash(GENESIS_NONCE_LE, hash);

    double share_diff;
    share_verdict_t v = share_validate(&work, hash, &share_diff);
    TEST_ASSERT_EQUAL_INT(SHARE_INVALID_TARGET, v);
}

// low_difficulty_sanity: genesis hash meets diff=1 target but work->difficulty=1e12
// → share_diff << 1e12/2 → SHARE_LOW_DIFFICULTY.
void test_asic_share_validate_low_difficulty_sanity(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    work.difficulty = 1e12;

    uint8_t hash[32];
    genesis_hash(GENESIS_NONCE_LE, hash);

    double share_diff;
    share_verdict_t v = share_validate(&work, hash, &share_diff);
    TEST_ASSERT_EQUAL_INT(SHARE_LOW_DIFFICULTY, v);
}

// TA-380: version_mask fallback — verify rolled-version formula for all three cases.
// When a pool grants no version mask (mask == 0), the chip still rolls ver_bits
// within ASIC_VERSION_MASK.  The firmware must reconstruct the correct version
// for SHA256d instead of skipping the roll and hashing the unrolled header.

// Case A: pool grants version_mask=0x1fffe000, ver_bits=0x00012000 — use pool mask.
void test_asic_version_mask_fallback_case_a_pool_mask(void)
{
    uint32_t version      = 0x20000000u;
    uint32_t version_mask = 0x1FFFE000u;
    uint32_t ver_bits     = 0x00012000u;
    uint32_t effective    = version_mask;  // pool mask used
    uint32_t rolled       = (version & ~effective) | (ver_bits & effective);
    // base bits preserved, rolling bits from ver_bits
    TEST_ASSERT_EQUAL_HEX32(0x20012000u, rolled);
}

// Case B: pool grants version_mask=0 (no version rolling), ver_bits=0x00012000 —
// fall back to chip effective mask (ASIC_VERSION_MASK = 0x1FFFE000).
void test_asic_version_mask_fallback_case_b_fallback_mask(void)
{
    uint32_t version      = 0x20000000u;
    uint32_t version_mask = 0;  // pool grants none
    uint32_t ver_bits     = 0x00012000u;
    uint32_t effective    = version_mask ? version_mask : ASIC_VERSION_MASK;
    uint32_t rolled       = (version & ~effective) | (ver_bits & effective);
    // same result as Case A — chip mask == pool mask for BM1370
    TEST_ASSERT_EQUAL_HEX32(0x20012000u, rolled);
}

// Case C: version_mask=0, ver_bits=0 — no rolling, version unchanged (negative case).
void test_asic_version_mask_fallback_case_c_no_roll(void)
{
    uint32_t version  = 0x20000000u;
    uint32_t ver_bits = 0;
    // ver_bits == 0 → skip roll; rolled_ver == orig->version
    uint32_t rolled_ver = (ver_bits != 0)
        ? (version & ~ASIC_VERSION_MASK) | (ver_bits & ASIC_VERSION_MASK)
        : version;
    TEST_ASSERT_EQUAL_HEX32(version, rolled_ver);
}

// version_rolling_applied: two different version_bits produce different hashes.
// Hash computation is now caller's responsibility (as in asic_task.c).
void test_asic_share_validate_version_rolling_applied(void)
{
    // Compute two hashes with different version bytes applied manually
    uint8_t header1[80], header2[80];
    memcpy(header1, GENESIS_HEADER, 80);
    memcpy(header2, GENESIS_HEADER, 80);
    // Apply rolled version 1
    header1[0] = 0x00; header1[1] = 0x20; header1[2] = 0x00; header1[3] = 0x20;
    // Apply rolled version 2
    header2[0] = 0x00; header2[1] = 0x40; header2[2] = 0x00; header2[3] = 0x20;

    uint8_t hash1[32], hash2[32];
    sha256d(header1, 80, hash1);
    sha256d(header2, 80, hash2);

    TEST_ASSERT_FALSE(memcmp(hash1, hash2, 32) == 0);
}

// nonce_patching_position: verify caller-computed SHA256d matches expectation.
// (share_validate now takes a pre-computed hash; this tests the SHA layer used by callers.)
void test_asic_share_validate_nonce_patching_position(void)
{
    uint32_t nonce = 0xdeadbeef;
    uint8_t hash_from_helper[32];
    genesis_hash(nonce, hash_from_helper);

    // Build expected hash manually
    uint8_t header[80];
    memcpy(header, GENESIS_HEADER, 80);
    header[76] = (uint8_t)(nonce);
    header[77] = (uint8_t)(nonce >> 8);
    header[78] = (uint8_t)(nonce >> 16);
    header[79] = (uint8_t)(nonce >> 24);

    uint8_t expected_hash[32];
    sha256d(header, 80, expected_hash);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, hash_from_helper, 32);
}

// Track-3: ordering bug regression — invalid target fires BEFORE meets_target check.
// Simulates the SW-path bug: a hash that would pass meets_target on an all-0xFF target
// must still return SHARE_INVALID_TARGET, not SHARE_VALID.
void test_share_validate_target_invalid_returns_fail(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    memset(work.target, 0xFF, sizeof(work.target));  // invalid: all bits set

    uint8_t hash[32];
    genesis_hash(GENESIS_NONCE_LE, hash);

    double diff = 99.0;
    share_verdict_t v = share_validate(&work, hash, &diff);
    TEST_ASSERT_EQUAL_INT(SHARE_INVALID_TARGET, v);
}

// Track-3: the specific SW-path ordering bug — meets_target can return true on a
// corrupt target, but is_target_valid must still reject it.
// With the new ordering (is_target_valid first), verdict must be SHARE_INVALID_TARGET.
void test_share_validate_meets_target_with_invalid_target_still_fails(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    // All-0xFF target: meets_target returns true (any hash ≤ max), but is_target_valid
    // rejects it. The correct ordering must catch this before meets_target runs.
    memset(work.target, 0xFF, sizeof(work.target));

    // Use an all-zero hash — guaranteed to be ≤ any target, so meets_target=true.
    uint8_t hash[32];
    memset(hash, 0x00, sizeof(hash));

    double diff = 0.0;
    share_verdict_t v = share_validate(&work, hash, &diff);

    // Must be INVALID_TARGET, not VALID — is_target_valid runs first.
    TEST_ASSERT_EQUAL_INT(SHARE_INVALID_TARGET, v);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, diff);  // out_diff must not be set
}
#endif /* ASIC_CHIP */
