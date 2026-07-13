#include "unity.h"
#include "stratum_reqid.h"

void test_stratum_reqid_register_then_take(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    stratum_reqid_register(&t, 5, STRATUM_REQID_SUBSCRIBE);
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBSCRIBE, stratum_reqid_take(&t, 5));
}

void test_stratum_reqid_take_consumes(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);

    stratum_reqid_register(&t, 7, STRATUM_REQID_AUTHORIZE);
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_AUTHORIZE, stratum_reqid_take(&t, 7));
    // Second take of the same id -- already consumed.
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 7));
}

void test_stratum_reqid_unregistered_id_returns_none(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 42));
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
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_KEEPALIVE, stratum_reqid_take(&t, 10));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBMIT, stratum_reqid_take(&t, 11));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_KEEPALIVE, stratum_reqid_take(&t, 12));
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

    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 0));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_SUBMIT, stratum_reqid_take(&t, 1));
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_KEEPALIVE, stratum_reqid_take(&t, STRATUM_REQID_MAX_INFLIGHT));
}

void test_stratum_reqid_reset_clears_table(void)
{
    stratum_reqid_table_t t;
    stratum_reqid_reset(&t);
    stratum_reqid_register(&t, 1, STRATUM_REQID_CONFIGURE);
    stratum_reqid_reset(&t);
    TEST_ASSERT_EQUAL_INT(STRATUM_REQID_NONE, stratum_reqid_take(&t, 1));
}
