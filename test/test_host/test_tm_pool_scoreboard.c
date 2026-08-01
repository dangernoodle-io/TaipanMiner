#include "unity.h"
#include "tm_pool_stats.h"
#include "bb_config.h"
#include "bb_storage.h"
#include "fake_nvs_backend.h"
#include "tm_pool_test_helpers.h"

#include <string.h>

// tm_pool_stats's field tables target backend="nvs" (the real ESP-IDF
// backend) -- host tests register the shared fake in-memory "nvs" vtable
// (see fake_nvs_backend.h) under that same name, exercising the SAME
// production field-table/addr code path the device build uses, with only
// the backend's storage swapped for a host-safe stand-in.

static void reset_all(void)
{
    bb_storage_test_reset();
    fake_nvs_reset();
    bb_storage_register_backend("nvs", &s_fake_nvs_vtable, NULL);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_init());
}

static void s_assert_sorted_desc(const tm_pool_scoreboard_t *sb)
{
    for (uint8_t i = 1; i < sb->count; i++) {
        TEST_ASSERT_TRUE(sb->entries[i - 1].diff >= sb->entries[i].diff);
    }
}

/* ---------------------------------------------------------------------------
 * tm_pool_scoreboard_maybe_insert -- pure logic
 * ---------------------------------------------------------------------------*/

void test_tm_pool_scoreboard_maybe_insert_empty_board_inserts(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    tm_pool_share_t share = tm_pool_test_share(10.0, 1);
    TEST_ASSERT_TRUE(tm_pool_scoreboard_maybe_insert(&sb, &share));
    TEST_ASSERT_EQUAL_UINT8(1, sb.count);
    TEST_ASSERT_EQUAL_DOUBLE(10.0, sb.entries[0].diff);
}

void test_tm_pool_scoreboard_maybe_insert_ascending_sequence_keeps_sorted_desc(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    for (int i = 1; i <= 5; i++) {
        tm_pool_share_t share = tm_pool_test_share((double)i, i);
        TEST_ASSERT_TRUE(tm_pool_scoreboard_maybe_insert(&sb, &share));
    }
    TEST_ASSERT_EQUAL_UINT8(5, sb.count);
    s_assert_sorted_desc(&sb);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, sb.entries[0].diff);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, sb.entries[4].diff);
}

void test_tm_pool_scoreboard_maybe_insert_descending_sequence_keeps_sorted_desc(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    for (int i = 5; i >= 1; i--) {
        tm_pool_share_t share = tm_pool_test_share((double)i, i);
        TEST_ASSERT_TRUE(tm_pool_scoreboard_maybe_insert(&sb, &share));
    }
    TEST_ASSERT_EQUAL_UINT8(5, sb.count);
    s_assert_sorted_desc(&sb);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, sb.entries[0].diff);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, sb.entries[4].diff);
}

void test_tm_pool_scoreboard_maybe_insert_random_sequence_keeps_sorted_desc(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    static const double diffs[] = { 7.0, 3.0, 42.0, 1.0, 19.0, 8.5, 100.0, 0.5, 55.0, 12.0, 2.0, 60.0 };
    for (size_t i = 0; i < sizeof(diffs) / sizeof(diffs[0]); i++) {
        tm_pool_share_t share = tm_pool_test_share(diffs[i], (int64_t)i);
        tm_pool_scoreboard_maybe_insert(&sb, &share);
    }
    TEST_ASSERT_EQUAL_UINT8(TM_POOL_SCOREBOARD_N, sb.count);
    s_assert_sorted_desc(&sb);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, sb.entries[0].diff); // the overall max
}

void test_tm_pool_scoreboard_maybe_insert_beats_min_evicts_min(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    for (int i = 1; i <= TM_POOL_SCOREBOARD_N; i++) {
        tm_pool_share_t share = tm_pool_test_share((double)i, i);
        TEST_ASSERT_TRUE(tm_pool_scoreboard_maybe_insert(&sb, &share));
    }
    TEST_ASSERT_EQUAL_UINT8(TM_POOL_SCOREBOARD_N, sb.count);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, sb.entries[TM_POOL_SCOREBOARD_N - 1].diff); // current min

    tm_pool_share_t better = tm_pool_test_share(1.5, 100); // beats the min (1.0)
    TEST_ASSERT_TRUE(tm_pool_scoreboard_maybe_insert(&sb, &better));
    TEST_ASSERT_EQUAL_UINT8(TM_POOL_SCOREBOARD_N, sb.count); // still capped
    s_assert_sorted_desc(&sb);
    // The old min (1.0) is gone -- the new min is now 1.5.
    TEST_ASSERT_EQUAL_DOUBLE(1.5, sb.entries[TM_POOL_SCOREBOARD_N - 1].diff);
}

void test_tm_pool_scoreboard_maybe_insert_below_full_board_min_rejected(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    for (int i = 1; i <= TM_POOL_SCOREBOARD_N; i++) {
        tm_pool_share_t share = tm_pool_test_share((double)i, i);
        tm_pool_scoreboard_maybe_insert(&sb, &share);
    }

    tm_pool_share_t worse = tm_pool_test_share(0.5, 100); // below the min (1.0)
    TEST_ASSERT_FALSE(tm_pool_scoreboard_maybe_insert(&sb, &worse));
    TEST_ASSERT_EQUAL_UINT8(TM_POOL_SCOREBOARD_N, sb.count);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, sb.entries[TM_POOL_SCOREBOARD_N - 1].diff); // unchanged
}

void test_tm_pool_scoreboard_maybe_insert_exact_n_boundary(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    for (int i = 1; i < TM_POOL_SCOREBOARD_N; i++) {
        tm_pool_share_t share = tm_pool_test_share((double)i, i);
        TEST_ASSERT_TRUE(tm_pool_scoreboard_maybe_insert(&sb, &share));
    }
    TEST_ASSERT_EQUAL_UINT8(TM_POOL_SCOREBOARD_N - 1, sb.count); // not yet full

    tm_pool_share_t last = tm_pool_test_share((double)TM_POOL_SCOREBOARD_N, TM_POOL_SCOREBOARD_N);
    TEST_ASSERT_TRUE(tm_pool_scoreboard_maybe_insert(&sb, &last)); // fills it exactly
    TEST_ASSERT_EQUAL_UINT8(TM_POOL_SCOREBOARD_N, sb.count);
    s_assert_sorted_desc(&sb);
}

void test_tm_pool_scoreboard_maybe_insert_hash_null_prefix_invalid(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    tm_pool_share_t share = tm_pool_test_share(10.0, 1);
    share.hash = NULL;
    tm_pool_scoreboard_maybe_insert(&sb, &share);
    TEST_ASSERT_FALSE(sb.entries[0].hash_prefix_valid);
}

void test_tm_pool_scoreboard_maybe_insert_hash_prefix_correct_msb_bytes(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    uint8_t hash[32];
    for (int i = 0; i < 32; i++) {
        hash[i] = (uint8_t)i;
    }

    tm_pool_share_t share = tm_pool_test_share(10.0, 1);
    share.hash = hash;
    tm_pool_scoreboard_maybe_insert(&sb, &share);

    TEST_ASSERT_TRUE(sb.entries[0].hash_prefix_valid);
    static const uint8_t expected[8] = { 24, 25, 26, 27, 28, 29, 30, 31 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, sb.entries[0].hash_prefix, 8);
}

void test_tm_pool_scoreboard_maybe_insert_job_id_truncated(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    // job_id[32] holds 31 chars + NUL -- a 40-char source truncates.
    static const char *long_job = "0123456789012345678901234567890123456789";
    tm_pool_share_t    share    = tm_pool_test_share(10.0, 1);
    share.job_id = long_job;
    tm_pool_scoreboard_maybe_insert(&sb, &share);

    TEST_ASSERT_EQUAL_UINT32(31, (uint32_t)strlen(sb.entries[0].job_id));
    TEST_ASSERT_EQUAL_INT('\0', sb.entries[0].job_id[31]);
}

void test_tm_pool_scoreboard_maybe_insert_extranonce2_len_clamped(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    uint8_t en2[16];
    for (int i = 0; i < 16; i++) {
        en2[i] = (uint8_t)(0x10 + i);
    }
    tm_pool_share_t share = tm_pool_test_share(10.0, 1);
    share.extranonce2     = en2;
    share.extranonce2_len = 16; // over the 8-byte cap
    tm_pool_scoreboard_maybe_insert(&sb, &share);

    TEST_ASSERT_EQUAL_UINT8(8, sb.entries[0].extranonce2_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(en2, sb.entries[0].extranonce2, 8);
}

void test_tm_pool_scoreboard_maybe_insert_null_extranonce2_forces_len_zero(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));

    // extranonce2 == NULL but extranonce2_len > 0 -- a caller bug (or
    // uninitialized field) that must not be taken at face value: the entry
    // must not claim bytes that were never actually supplied.
    tm_pool_share_t share = tm_pool_test_share(10.0, 1);
    share.extranonce2     = NULL;
    share.extranonce2_len = 5;
    tm_pool_scoreboard_maybe_insert(&sb, &share);

    TEST_ASSERT_EQUAL_UINT8(0, sb.entries[0].extranonce2_len);
}

void test_tm_pool_scoreboard_maybe_insert_null_sb_returns_false(void)
{
    tm_pool_share_t share = tm_pool_test_share(10.0, 1);
    TEST_ASSERT_FALSE(tm_pool_scoreboard_maybe_insert(NULL, &share));
}

void test_tm_pool_scoreboard_maybe_insert_null_share_returns_false(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));
    TEST_ASSERT_FALSE(tm_pool_scoreboard_maybe_insert(&sb, NULL));
    TEST_ASSERT_EQUAL_UINT8(0, sb.count);
}

/* ---------------------------------------------------------------------------
 * Full record round-trip -- tm_pool_stats_record_share() drives BOTH the
 * lifetime record and the scoreboard together.
 * ---------------------------------------------------------------------------*/

void test_tm_pool_stats_record_share_full_round_trip_scoreboard(void)
{
    reset_all();

    uint8_t hash[32];
    for (int i = 0; i < 32; i++) {
        hash[i] = (uint8_t)i;
    }
    uint8_t en2[3] = { 0xAA, 0xBB, 0xCC };

    tm_pool_share_t share;
    memset(&share, 0, sizeof(share));
    share.diff           = 500.0;
    share.submitted_ts   = 1700000200;
    share.ntime          = 0x5F5E100;
    share.nonce          = 0xDEADBEEF;
    share.version_bits   = 0x20000000;
    share.version_rolled = true;
    share.hash           = hash;
    share.extranonce2    = en2;
    share.extranonce2_len = 3;
    share.job_id         = "job-abc";

    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(2, &share));
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(2));

    // Force slot 2 out of the cache, then read the scoreboard back from
    // storage.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(0, 1));

    tm_pool_scoreboard_t sb;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_scoreboard_load(2, &sb));
    TEST_ASSERT_EQUAL_UINT8(1, sb.count);

    const tm_pool_scoreboard_entry_t *e = &sb.entries[0];
    TEST_ASSERT_EQUAL_DOUBLE(500.0, e->diff);
    TEST_ASSERT_EQUAL_INT64(1700000200, e->submitted_ts);
    TEST_ASSERT_EQUAL_UINT32(0x5F5E100, e->ntime);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, e->nonce);
    TEST_ASSERT_EQUAL_UINT32(0x20000000, e->version_bits);
    TEST_ASSERT_TRUE(e->version_rolled);
    TEST_ASSERT_TRUE(e->hash_prefix_valid);
    static const uint8_t expected_prefix[8] = { 24, 25, 26, 27, 28, 29, 30, 31 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_prefix, e->hash_prefix, 8);
    TEST_ASSERT_EQUAL_UINT8(3, e->extranonce2_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(en2, e->extranonce2, 3);
    TEST_ASSERT_EQUAL_STRING("job-abc", e->job_id);
}

/* ---------------------------------------------------------------------------
 * Active-slot cache + persist-immediately discipline
 * ---------------------------------------------------------------------------*/

void test_tm_pool_stats_record_share_scoreboard_change_persists_immediately(void)
{
    reset_all();
    tm_pool_share_t share = tm_pool_test_share(30.0, 1700000001);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(0, &share));

    // No explicit flush() call -- the scoreboard blob must already be on
    // storage (probed with a fresh field descriptor, bypassing the cache).
    static const bb_config_field_t probe0_score = {
        .id      = "pool0.score.probe",
        .type    = BB_CONFIG_BLOB,
        .addr    = { .backend = "nvs", .ns_or_dir = "tm_pool", .key = "pool0_score" },
        .max_len = sizeof(tm_pool_scoreboard_t),
    };
    tm_pool_scoreboard_t raw;
    size_t                len = 0;
    TEST_ASSERT_EQUAL(BB_OK, bb_config_get_blob(&probe0_score, &raw, sizeof(raw), &len));
    TEST_ASSERT_EQUAL_UINT8(1, raw.count);
}

void test_tm_pool_scoreboard_flushed_on_slot_switch(void)
{
    reset_all();
    tm_pool_share_t share = tm_pool_test_share(30.0, 1700000001);
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_share(0, &share));
    // The insert is already persisted immediately by record_share -- switch
    // the active slot away from 0 and confirm the scoreboard survives on
    // storage (the active-slot cache no longer holds it).
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_record_hashes(1, 1));

    tm_pool_scoreboard_t sb;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_scoreboard_load(0, &sb));
    TEST_ASSERT_EQUAL_UINT8(1, sb.count);
    TEST_ASSERT_EQUAL_DOUBLE(30.0, sb.entries[0].diff);
}

void test_tm_pool_stats_record_share_score_flush_failure_leaves_dirty_for_retry(void)
{
    reset_all();
    fake_nvs_backend_fail_set_key("pool0_score");

    tm_pool_share_t share = tm_pool_test_share(30.0, 1700000001);
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_stats_record_share(0, &share));

    // Nothing landed on storage (the write failed).
    static const bb_config_field_t probe0_score = {
        .id      = "pool0.score.probe",
        .type    = BB_CONFIG_BLOB,
        .addr    = { .backend = "nvs", .ns_or_dir = "tm_pool", .key = "pool0_score" },
        .max_len = sizeof(tm_pool_scoreboard_t),
    };
    tm_pool_scoreboard_t raw;
    size_t                len = 0;
    TEST_ASSERT_EQUAL(BB_ERR_NOT_FOUND, bb_config_get_blob(&probe0_score, &raw, sizeof(raw), &len));

    // But the insert already landed in the in-RAM cache -- a later flush
    // retry recovers it (fault was one-shot, already fired above).
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(0));
    TEST_ASSERT_EQUAL(BB_OK, bb_config_get_blob(&probe0_score, &raw, sizeof(raw), &len));
    TEST_ASSERT_EQUAL_UINT8(1, raw.count);
}

void test_tm_pool_stats_switch_score_flush_failure_propagates_and_preserves_dirty_slot(void)
{
    reset_all();
    // Arm two consecutive set() failures on pool0_score: one for the
    // immediate persist inside record_share, one for the retry attempted by
    // the slot-switch flush below -- proving the switch itself surfaces the
    // failure and does not silently drop the pending scoreboard change.
    fake_nvs_backend_fail_set_key_n("pool0_score", 2);

    tm_pool_share_t share = tm_pool_test_share(30.0, 1700000001);
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_stats_record_share(0, &share));

    // Switching to slot 1 must attempt to flush slot 0's still-dirty
    // scoreboard first; that attempt is also armed to fail.
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_stats_record_hashes(1, 1));

    // Slot 1 was never activated -- its hash never landed anywhere.
    tm_pool_lifetime_stat_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_load(1, &out));
    TEST_ASSERT_EQUAL_UINT64(0, out.hashes);

    // Slot 0's scoreboard insert is still intact in the cache -- a later
    // flush retry (fault is fully spent now) recovers it.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(0));
    tm_pool_scoreboard_t sb;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_scoreboard_load(0, &sb));
    TEST_ASSERT_EQUAL_UINT8(1, sb.count);
}

// Combined case: a share that BOTH improves best_diff AND enters the
// scoreboard, where the LIFETIME flush faults. The two flushes must be
// truly independent -- the scoreboard flush is still attempted (and
// succeeds) even though the lifetime flush failed first.
void test_tm_pool_stats_record_share_lifetime_flush_fault_does_not_block_scoreboard_flush(void)
{
    reset_all();
    fake_nvs_backend_fail_set_key("pool0_stat");

    tm_pool_share_t share = tm_pool_test_share(30.0, 1700000001); // first share -- improves best_diff AND enters the board
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_stats_record_share(0, &share));

    // Lifetime flush failed (fault fired against pool0_stat) -- nothing
    // landed there.
    static const bb_config_field_t probe0_stat = {
        .id      = "pool0.stat.probe",
        .type    = BB_CONFIG_BLOB,
        .addr    = { .backend = "nvs", .ns_or_dir = "tm_pool", .key = "pool0_stat" },
        .max_len = sizeof(tm_pool_lifetime_stat_t),
    };
    tm_pool_lifetime_stat_t raw_stat;
    size_t                  len = 0;
    TEST_ASSERT_EQUAL(BB_ERR_NOT_FOUND, bb_config_get_blob(&probe0_stat, &raw_stat, sizeof(raw_stat), &len));

    // But the scoreboard flush was attempted independently (no fault armed
    // against pool0_score) and DID succeed -- proving the lifetime fault
    // did not short-circuit it.
    static const bb_config_field_t probe0_score = {
        .id      = "pool0.score.probe",
        .type    = BB_CONFIG_BLOB,
        .addr    = { .backend = "nvs", .ns_or_dir = "tm_pool", .key = "pool0_score" },
        .max_len = sizeof(tm_pool_scoreboard_t),
    };
    tm_pool_scoreboard_t raw_score;
    TEST_ASSERT_EQUAL(BB_OK, bb_config_get_blob(&probe0_score, &raw_score, sizeof(raw_score), &len));
    TEST_ASSERT_EQUAL_UINT8(1, raw_score.count);

    // The lifetime record's dirty flag survived its failed flush -- a
    // later retry recovers best_diff too.
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_stats_flush(0));
    TEST_ASSERT_EQUAL(BB_OK, bb_config_get_blob(&probe0_stat, &raw_stat, sizeof(raw_stat), &len));
    TEST_ASSERT_EQUAL_DOUBLE(30.0, raw_stat.best_diff);
}

/* ---------------------------------------------------------------------------
 * Sanitizer
 * ---------------------------------------------------------------------------*/

static double s_bits_to_double(uint64_t bits)
{
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

void test_tm_pool_scoreboard_sanitize_drops_corrupt_entry_and_recompacts(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.count = 3;
    sb.entries[0].diff = 100.0;
    strcpy(sb.entries[0].job_id, "a");
    sb.entries[1].diff = s_bits_to_double(UINT64_C(0x7FF8000000000000)); // NaN -- corrupt
    strcpy(sb.entries[1].job_id, "b");
    sb.entries[2].diff = 50.0;
    strcpy(sb.entries[2].job_id, "c");

    tm_pool_scoreboard_sanitize(&sb);

    TEST_ASSERT_EQUAL_UINT8(2, sb.count);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, sb.entries[0].diff);
    TEST_ASSERT_EQUAL_STRING("a", sb.entries[0].job_id);
    // Entry 2 (job "c") shifted down into slot 1, closing the gap left by
    // the dropped NaN entry -- the surviving entries are NOT zeroed/reset.
    TEST_ASSERT_EQUAL_DOUBLE(50.0, sb.entries[1].diff);
    TEST_ASSERT_EQUAL_STRING("c", sb.entries[1].job_id);
}

void test_tm_pool_scoreboard_sanitize_clamps_count_to_max(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.count = 200; // corrupt -- beyond TM_POOL_SCOREBOARD_N
    for (int i = 0; i < TM_POOL_SCOREBOARD_N; i++) {
        sb.entries[i].diff = (double)(TM_POOL_SCOREBOARD_N - i);
    }

    tm_pool_scoreboard_sanitize(&sb);
    TEST_ASSERT_EQUAL_UINT8(TM_POOL_SCOREBOARD_N, sb.count);
}

void test_tm_pool_scoreboard_sanitize_clamps_extranonce2_len(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.count                     = 1;
    sb.entries[0].diff           = 10.0;
    sb.entries[0].extranonce2_len = 200; // corrupt

    tm_pool_scoreboard_sanitize(&sb);
    TEST_ASSERT_EQUAL_UINT8(1, sb.count); // entry itself is still valid
    TEST_ASSERT_EQUAL_UINT8(8, sb.entries[0].extranonce2_len);
}

void test_tm_pool_scoreboard_sanitize_null_is_a_noop(void)
{
    tm_pool_scoreboard_sanitize(NULL); // must not crash
}

void test_tm_pool_scoreboard_sanitize_rejects_inf_diff(void)
{
    tm_pool_scoreboard_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.count           = 1;
    sb.entries[0].diff = s_bits_to_double(UINT64_C(0x7FF0000000000000)); // +inf

    tm_pool_scoreboard_sanitize(&sb);
    TEST_ASSERT_EQUAL_UINT8(0, sb.count);
}

void test_tm_pool_stats_load_sanitizes_corrupt_stored_scoreboard(void)
{
    reset_all();

    tm_pool_scoreboard_t corrupt;
    memset(&corrupt, 0, sizeof(corrupt));
    corrupt.count           = 2;
    corrupt.entries[0].diff = 10.0;
    corrupt.entries[1].diff = s_bits_to_double(UINT64_C(0x7FF8000000000000)); // NaN

    static const bb_config_field_t probe0_score = {
        .id      = "pool0.score.probe",
        .type    = BB_CONFIG_BLOB,
        .addr    = { .backend = "nvs", .ns_or_dir = "tm_pool", .key = "pool0_score" },
        .max_len = sizeof(tm_pool_scoreboard_t),
    };
    TEST_ASSERT_EQUAL(BB_OK, bb_config_set_blob(&probe0_score, &corrupt, sizeof(corrupt)));

    tm_pool_scoreboard_t out;
    TEST_ASSERT_EQUAL(BB_OK, tm_pool_scoreboard_load(0, &out));
    TEST_ASSERT_EQUAL_UINT8(1, out.count); // NaN entry dropped, sane one kept
    TEST_ASSERT_EQUAL_DOUBLE(10.0, out.entries[0].diff);
}

/* ---------------------------------------------------------------------------
 * Display helper -- tm_pool_scoreboard_hash_to_hex
 * ---------------------------------------------------------------------------*/

void test_tm_pool_scoreboard_hash_to_hex_known_prefix_reverses_to_big_endian(void)
{
    tm_pool_scoreboard_entry_t e;
    memset(&e, 0, sizeof(e));
    e.hash_prefix_valid = true;
    static const uint8_t prefix[8] = { 24, 25, 26, 27, 28, 29, 30, 31 }; // internal order
    memcpy(e.hash_prefix, prefix, sizeof(prefix));

    char out[32];
    int  n = tm_pool_scoreboard_hash_to_hex(&e, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(16, n);
    // Reversed (byte[7] first) then hex-encoded: 31,30,29,...,24.
    TEST_ASSERT_EQUAL_STRING("1f1e1d1c1b1a1918", out);
}

void test_tm_pool_scoreboard_hash_to_hex_invalid_prefix_rejected(void)
{
    tm_pool_scoreboard_entry_t e;
    memset(&e, 0, sizeof(e));
    e.hash_prefix_valid = false;

    char out[32];
    TEST_ASSERT_EQUAL_INT(-1, tm_pool_scoreboard_hash_to_hex(&e, out, sizeof(out)));
}

void test_tm_pool_scoreboard_hash_to_hex_short_buffer_rejected(void)
{
    tm_pool_scoreboard_entry_t e;
    memset(&e, 0, sizeof(e));
    e.hash_prefix_valid = true;

    char out[16]; // needs 17 (16 hex chars + NUL)
    TEST_ASSERT_EQUAL_INT(-1, tm_pool_scoreboard_hash_to_hex(&e, out, sizeof(out)));
}

void test_tm_pool_scoreboard_hash_to_hex_null_entry_rejected(void)
{
    char out[32];
    TEST_ASSERT_EQUAL_INT(-1, tm_pool_scoreboard_hash_to_hex(NULL, out, sizeof(out)));
}

void test_tm_pool_scoreboard_hash_to_hex_null_out_rejected(void)
{
    tm_pool_scoreboard_entry_t e;
    memset(&e, 0, sizeof(e));
    e.hash_prefix_valid = true;
    TEST_ASSERT_EQUAL_INT(-1, tm_pool_scoreboard_hash_to_hex(&e, NULL, 32));
}

/* ---------------------------------------------------------------------------
 * Fault injection -- scoreboard BLOB load path
 * ---------------------------------------------------------------------------*/

void test_tm_pool_scoreboard_load_propagates_genuine_backend_fault(void)
{
    reset_all();
    fake_nvs_backend_fail_key("pool0_score");

    tm_pool_scoreboard_t out;
    TEST_ASSERT_EQUAL(BB_ERR_TIMEOUT, tm_pool_scoreboard_load(0, &out));
}

/* ---------------------------------------------------------------------------
 * Bounds / NULL rejection
 * ---------------------------------------------------------------------------*/

void test_tm_pool_scoreboard_load_rejects_out_of_range_idx(void)
{
    reset_all();
    tm_pool_scoreboard_t out;
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_scoreboard_load(TM_POOL_MAX, &out));
}

void test_tm_pool_scoreboard_load_rejects_null_out(void)
{
    reset_all();
    TEST_ASSERT_EQUAL(BB_ERR_INVALID_ARG, tm_pool_scoreboard_load(0, NULL));
}
