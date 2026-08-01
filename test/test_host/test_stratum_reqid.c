#include "unity.h"
#include "stratum_reqid.h"
#include <stdio.h>
#include <string.h>

void test_stratum_reqid_register_then_take(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    stratum_reqid_register(&t, 5, STRATUM_REQID_SUBSCRIBE);
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBSCRIBE, stratum_reqid_take(&t, 5, NULL));
}

void test_stratum_reqid_take_consumes(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    stratum_reqid_register(&t, 7, STRATUM_REQID_AUTHORIZE);
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_AUTHORIZE, stratum_reqid_take(&t, 7, NULL));
    // Second take of the same id -- already consumed.
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 7, NULL));
}

void test_stratum_reqid_unregistered_id_returns_none(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 42, NULL));
}

// The bug this module fixes (TA-560 acceptance criteria): a keepalive ack
// that arrives out of order relative to a NEWER keepalive send must still
// be classified as a keepalive, never mistaken for a plain submit response
// (which would otherwise inflate the accepted/rejected share counters).
void test_stratum_reqid_keepalive_survives_a_newer_overwrite(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    stratum_reqid_register(&t, 10, STRATUM_REQID_KEEPALIVE);   // first keepalive, id=10
    stratum_reqid_register(&t, 11, STRATUM_REQID_SUBMIT);      // a share submit lands in between
    stratum_reqid_register(&t, 12, STRATUM_REQID_KEEPALIVE);   // a second keepalive, id=12, overwrites nothing (distinct id)

    // The delayed ack for the FIRST keepalive (id=10) arrives last -- must
    // still resolve to KEEPALIVE, not fall through to "submit response".
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_KEEPALIVE, stratum_reqid_take(&t, 10, NULL));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBMIT, stratum_reqid_take(&t, 11, NULL));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_KEEPALIVE, stratum_reqid_take(&t, 12, NULL));
}

void test_stratum_reqid_table_full_evicts_oldest(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    for (int i = 0; i < STRATUM_REQID_MAX_INFLIGHT; i++) {
        stratum_reqid_register(&t, i, STRATUM_REQID_SUBMIT);
    }
    // One more registration when full evicts id=0 (oldest).
    stratum_reqid_register(&t, STRATUM_REQID_MAX_INFLIGHT, STRATUM_REQID_KEEPALIVE);

    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 0, NULL));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBMIT, stratum_reqid_take(&t, 1, NULL));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_KEEPALIVE, stratum_reqid_take(&t, STRATUM_REQID_MAX_INFLIGHT, NULL));
}

void test_stratum_reqid_reset_clears_table(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);
    stratum_reqid_register(&t, 1, STRATUM_REQID_CONFIGURE);
    stratum_reqid_reset(&t);
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 1, NULL));
}

// ---------------------------------------------------------------------------
// stratum_reqid_register_submit(): the SUBMIT id and its full share payload
// are captured/evicted TOGETHER, in one slot -- see stratum_reqid.h's own
// doc comment (TA-571 firmware review finding #2, the two-table desync bug
// this replaces).
// ---------------------------------------------------------------------------

static stratum_accepted_share_t make_share(int seed)
{
    stratum_accepted_share_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.job_id, sizeof(s.job_id), "job-%d", seed);
    snprintf(s.extranonce2_hex, sizeof(s.extranonce2_hex), "%08x", seed);
    s.extranonce2_len = (uint8_t)strlen(s.extranonce2_hex);
    snprintf(s.ntime_hex, sizeof(s.ntime_hex), "%08x", seed + 1);
    snprintf(s.nonce_hex, sizeof(s.nonce_hex), "%08x", seed + 2);
    s.version_rolled = (seed % 2) == 0;
    s.version_bits = s.version_rolled ? (uint32_t)seed : 0;
    s.diff = 1024.0 + seed;
    for (int i = 0; i < 8; i++) s.hash_prefix[i] = (uint8_t)(seed + i);
    return s;
}

void test_stratum_reqid_register_submit_then_take_returns_exact_record(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    stratum_accepted_share_t in = make_share(1);
    stratum_reqid_register_submit(&t, 42, &in);

    stratum_accepted_share_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBMIT, stratum_reqid_take(&t, 42, &out));
    TEST_ASSERT_EQUAL_MEMORY(&in, &out, sizeof(in));
}

void test_stratum_reqid_take_consumes_submit_slot(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);
    stratum_accepted_share_t in = make_share(1);
    stratum_reqid_register_submit(&t, 7, &in);

    stratum_accepted_share_t out;
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBMIT, stratum_reqid_take(&t, 7, &out));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 7, &out));  // slot reclaimed
}

// A non-SUBMIT take() call must never touch share_out.
void test_stratum_reqid_take_non_submit_leaves_share_out_untouched(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);
    stratum_reqid_register(&t, 3, STRATUM_REQID_KEEPALIVE);

    stratum_accepted_share_t out;
    memset(&out, 0xAB, sizeof(out));  // sentinel pattern
    stratum_accepted_share_t sentinel = out;

    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_KEEPALIVE, stratum_reqid_take(&t, 3, &out));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &out, sizeof(out));
}

// The desync this fixes: a SUBMIT id registered first, then evicted by
// enough churn (other kinds registered afterward) to push it out of the
// fixed-cap table -- with a SINGLE table there is no possibility of the id
// being "gone" while its record lingers elsewhere; both vanish together.
void test_stratum_reqid_submit_evicted_by_churn_takes_id_and_record_together(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    stratum_accepted_share_t submit_rec = make_share(1);
    stratum_reqid_register_submit(&t, 100, &submit_rec);  // slot 0

    // Fill the remaining 7 slots, then one more -- 8 total registrations
    // after the submit evicts it (FIFO, oldest-first).
    for (int i = 0; i < STRATUM_REQID_MAX_INFLIGHT; i++) {
        stratum_reqid_register(&t, 200 + i, STRATUM_REQID_KEEPALIVE);
    }

    stratum_accepted_share_t out;
    memset(&out, 0, sizeof(out));
    // Evicted: falls back to NONE, exactly like any other evicted id --
    // no orphaned record anywhere (there is nowhere else for one to live).
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 100, &out));
}

// A submit id that survives churn WITHIN capacity (registered first, then
// enough other ids to exactly fill the table without exceeding it) must
// still resolve correctly and carry its exact record.
void test_stratum_reqid_submit_survives_churn_under_capacity(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    stratum_accepted_share_t submit_rec = make_share(9);
    stratum_reqid_register_submit(&t, 4, &submit_rec);  // slot 0 of 8

    // 7 more registrations -- table now exactly at capacity (8), submit
    // NOT evicted.
    for (int i = 0; i < STRATUM_REQID_MAX_INFLIGHT - 1; i++) {
        stratum_reqid_register(&t, 10 + i, STRATUM_REQID_KEEPALIVE);
    }

    stratum_accepted_share_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBMIT, stratum_reqid_take(&t, 4, &out));
    TEST_ASSERT_EQUAL_MEMORY(&submit_rec, &out, sizeof(submit_rec));
}
