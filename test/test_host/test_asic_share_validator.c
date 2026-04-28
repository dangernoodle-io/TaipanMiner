#include "unity.h"
#include "asic_share_validator.h"
#include "work.h"
#include "sha256.h"
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
    0x1d, 0xac, 0x2b, 0x7c, // nonce = 0x7c2bac1d LE → nonce_le = 0x7c2bac1d
};

// Genesis nonce as LE uint32 (matches nonce_le convention)
// header[76..79] = [0x1d, 0xac, 0x2b, 0x7c]
// nonce_le = 0x1d | 0xac<<8 | 0x2b<<16 | 0x7c<<24 = 0x7c2bac1d
#define GENESIS_NONCE_LE 0x7c2bac1du

// Helper: populate a genesis-based work with the given difficulty
static void make_genesis_work(mining_work_t *w, double difficulty)
{
    memset(w, 0, sizeof(*w));
    memcpy(w->header, GENESIS_HEADER, 80);
    // Set nonce field to zero so validator can apply it from the nonce argument
    w->header[76] = 0;
    w->header[77] = 0;
    w->header[78] = 0;
    w->header[79] = 0;
    difficulty_to_target(difficulty, w->target);
    w->version      = 1;
    w->version_mask = 0;
    w->ntime        = 0x495fab29;
    w->difficulty   = difficulty;
    strncpy(w->job_id, "test-genesis", sizeof(w->job_id) - 1);
    strncpy(w->extranonce2_hex, "00000000", sizeof(w->extranonce2_hex) - 1);
}

// TA-274: asic_share_validator tests

// NULL guard: NULL work → INVALID_TARGET
void test_asic_share_validate_null_work(void)
{
    double diff;
    uint8_t hash[32];
    asic_share_verdict_t v = asic_share_validate(NULL, 0, 0, &diff, hash);
    TEST_ASSERT_EQUAL_INT(ASIC_SHARE_INVALID_TARGET, v);
}

// NULL guard: NULL out_share_difficulty → INVALID_TARGET
void test_asic_share_validate_null_out_difficulty(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    uint8_t hash[32];
    asic_share_verdict_t v = asic_share_validate(&work, 0, 0, NULL, hash);
    TEST_ASSERT_EQUAL_INT(ASIC_SHARE_INVALID_TARGET, v);
}

// NULL guard: NULL out_hash → INVALID_TARGET
void test_asic_share_validate_null_out_hash(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    double diff;
    asic_share_verdict_t v = asic_share_validate(&work, 0, 0, &diff, NULL);
    TEST_ASSERT_EQUAL_INT(ASIC_SHARE_INVALID_TARGET, v);
}

// happy_path_easy_share: genesis header + genesis nonce meets diff=1 target.
// Genesis hash has many leading zeros and will easily satisfy diff=1.
// We set work.difficulty=1.0 and verify ASIC_SHARE_OK and share_diff populated.
void test_asic_share_validate_happy_path_easy_share(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);

    double share_diff;
    uint8_t hash[32];
    asic_share_verdict_t v = asic_share_validate(&work, GENESIS_NONCE_LE, 0, &share_diff, hash);

    TEST_ASSERT_EQUAL_INT(ASIC_SHARE_OK, v);
    TEST_ASSERT_GREATER_THAN(1.0, share_diff);
}

// below_target: all-zeros target means meets_target returns false for any real hash → BELOW_TARGET.
// (The all-zeros path fires BELOW_TARGET before INVALID_TARGET because meets_target is checked first.)
void test_asic_share_validate_below_target(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    memset(work.target, 0, sizeof(work.target));

    double share_diff;
    uint8_t hash[32];
    asic_share_verdict_t v = asic_share_validate(&work, GENESIS_NONCE_LE, 0, &share_diff, hash);
    TEST_ASSERT_EQUAL_INT(ASIC_SHARE_BELOW_TARGET, v);
}

// invalid_target: all-0xFF target — meets_target always returns true (hash ≤ 0xFF..FF)
// but is_target_valid rejects it → INVALID_TARGET
void test_asic_share_validate_invalid_target_all_ff(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    memset(work.target, 0xFF, sizeof(work.target));

    double share_diff;
    uint8_t hash[32];
    asic_share_verdict_t v = asic_share_validate(&work, GENESIS_NONCE_LE, 0, &share_diff, hash);
    TEST_ASSERT_EQUAL_INT(ASIC_SHARE_INVALID_TARGET, v);
}

// low_difficulty_sanity: genesis nonce meets diff=1 target (share_diff >> 1),
// but work->difficulty is set to 1e12 so share_diff < 1e12/2 → LOW_DIFFICULTY
void test_asic_share_validate_low_difficulty_sanity(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    work.difficulty = 1e12;  // requires share_diff >= 5e11; genesis hash gives ~1e10 at best

    double share_diff;
    uint8_t hash[32];
    asic_share_verdict_t v = asic_share_validate(&work, GENESIS_NONCE_LE, 0, &share_diff, hash);
    TEST_ASSERT_EQUAL_INT(ASIC_SHARE_LOW_DIFFICULTY, v);
}

// version_rolling_applied: same nonce, two calls with different version_bits produce different hashes
void test_asic_share_validate_version_rolling_applied(void)
{
    mining_work_t work;
    make_genesis_work(&work, 1.0);
    work.version      = 0x20000000;
    work.version_mask = 0x1FFFE000;
    work.difficulty   = 1.0;

    double diff1, diff2;
    uint8_t hash1[32], hash2[32];

    // These may or may not be ASIC_SHARE_OK — we only care about hash difference
    asic_share_validate(&work, GENESIS_NONCE_LE, 0x00002000, &diff1, hash1);
    asic_share_validate(&work, GENESIS_NONCE_LE, 0x00004000, &diff2, hash2);

    TEST_ASSERT_FALSE(memcmp(hash1, hash2, 32) == 0);
}

// nonce_patching_position: build header with bytes 76-79 = 0; pass nonce_le = 0xdeadbeef;
// verify out_hash == sha256d([header with LE nonce at 76-79])
void test_asic_share_validate_nonce_patching_position(void)
{
    mining_work_t work;
    memset(&work, 0, sizeof(work));
    // Use genesis header body (without nonce) to get a real but predictable input
    memcpy(work.header, GENESIS_HEADER, 80);
    work.header[76] = 0;
    work.header[77] = 0;
    work.header[78] = 0;
    work.header[79] = 0;
    // Use all-0xFF target so meets_target passes (will then fire INVALID_TARGET,
    // but hash is populated before verdict is checked by validator)
    memset(work.target, 0xFF, sizeof(work.target));
    work.difficulty   = 1.0;
    work.version_mask = 0;

    uint32_t nonce = 0xdeadbeef;

    double share_diff;
    uint8_t hash_from_validator[32];
    asic_share_validate(&work, nonce, 0, &share_diff, hash_from_validator);

    // Build expected hash manually
    uint8_t header[80];
    memcpy(header, GENESIS_HEADER, 80);
    header[76] = (uint8_t)(nonce);
    header[77] = (uint8_t)(nonce >> 8);
    header[78] = (uint8_t)(nonce >> 16);
    header[79] = (uint8_t)(nonce >> 24);

    uint8_t expected_hash[32];
    sha256d(header, 80, expected_hash);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, hash_from_validator, 32);
}
