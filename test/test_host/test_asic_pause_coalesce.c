#include "unity.h"
#include "asic_pause_coalesce.h"

void test_asic_pause_idle_when_not_pending_and_not_quiesced(void) {
    bool q = false;
    TEST_ASSERT_EQUAL(ASIC_PAUSE_ACTION_NONE, asic_pause_coalesce_next(false, &q));
    TEST_ASSERT_FALSE(q);
}

void test_asic_pause_first_pause_quiesces_and_acks(void) {
    bool q = false;
    TEST_ASSERT_EQUAL(ASIC_PAUSE_ACTION_QUIESCE_AND_ACK, asic_pause_coalesce_next(true, &q));
    TEST_ASSERT_TRUE(q);
}

void test_asic_pause_second_pause_only_acks(void) {
    bool q = true;  // already quiesced from prior pause
    TEST_ASSERT_EQUAL(ASIC_PAUSE_ACTION_ACK_ONLY, asic_pause_coalesce_next(true, &q));
    TEST_ASSERT_TRUE(q);  // unchanged
}

void test_asic_pause_resume_when_clear_and_quiesced(void) {
    bool q = true;
    TEST_ASSERT_EQUAL(ASIC_PAUSE_ACTION_RESUME, asic_pause_coalesce_next(false, &q));
    TEST_ASSERT_FALSE(q);
}

void test_asic_pause_full_check_install_resume_sequence(void) {
    // simulates bb_ota_pull's check + install + resume flow:
    // pending=true (check-phase pause) → quiesce+ack
    // pending=true (install-phase pause arrives during ramp window before resume could fire) → ack only
    // pending=false (after install completes + resume) → resume
    bool q = false;
    TEST_ASSERT_EQUAL(ASIC_PAUSE_ACTION_QUIESCE_AND_ACK, asic_pause_coalesce_next(true, &q));
    TEST_ASSERT_EQUAL(ASIC_PAUSE_ACTION_ACK_ONLY, asic_pause_coalesce_next(true, &q));
    TEST_ASSERT_TRUE(q);  // chip stayed quiesced through the whole sequence
    TEST_ASSERT_EQUAL(ASIC_PAUSE_ACTION_RESUME, asic_pause_coalesce_next(false, &q));
    TEST_ASSERT_FALSE(q);
}
