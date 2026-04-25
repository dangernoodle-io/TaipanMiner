#include "unity.h"
#include "ota_validator_state.h"

void test_ota_validator_init(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);
    TEST_ASSERT_FALSE(state.timer_armed);
}

void test_ota_validator_on_stratum_authorized_not_pending(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    ota_validator_step_t step = ota_validator_state_on_stratum_authorized(&state, false);

    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_NONE, step.kind);
    TEST_ASSERT_NULL(step.mark_reason);
    TEST_ASSERT_FALSE(state.timer_armed);
}

void test_ota_validator_on_stratum_authorized_pending_first_time(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    ota_validator_step_t step = ota_validator_state_on_stratum_authorized(&state, true);

    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_START_TIMER, step.kind);
    TEST_ASSERT_NULL(step.mark_reason);
    TEST_ASSERT_TRUE(state.timer_armed);
}

void test_ota_validator_on_stratum_authorized_pending_already_armed(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    // Arm the timer first
    ota_validator_step_t step1 = ota_validator_state_on_stratum_authorized(&state, true);
    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_START_TIMER, step1.kind);

    // Second call should be idempotent
    ota_validator_step_t step2 = ota_validator_state_on_stratum_authorized(&state, true);

    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_NONE, step2.kind);
    TEST_ASSERT_NULL(step2.mark_reason);
    TEST_ASSERT_TRUE(state.timer_armed);
}

void test_ota_validator_on_share_accepted_not_pending(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    ota_validator_step_t step = ota_validator_state_on_share_accepted(&state, false);

    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_NONE, step.kind);
    TEST_ASSERT_NULL(step.mark_reason);
    TEST_ASSERT_FALSE(state.timer_armed);
}

void test_ota_validator_on_share_accepted_pending_with_armed_timer(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    // Arm the timer
    ota_validator_state_on_stratum_authorized(&state, true);

    // Share accepted should stop timer and mark valid
    ota_validator_step_t step = ota_validator_state_on_share_accepted(&state, true);

    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_STOP_TIMER_AND_MARK_VALID, step.kind);
    TEST_ASSERT_EQUAL_STRING("first share", step.mark_reason);
    TEST_ASSERT_FALSE(state.timer_armed);
}

void test_ota_validator_on_share_accepted_pending_without_timer(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    ota_validator_step_t step = ota_validator_state_on_share_accepted(&state, true);

    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_MARK_VALID, step.kind);
    TEST_ASSERT_EQUAL_STRING("first share", step.mark_reason);
    TEST_ASSERT_FALSE(state.timer_armed);
}

void test_ota_validator_on_timer_fired_not_pending(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);
    state.timer_armed = true;

    ota_validator_step_t step = ota_validator_state_on_timer_fired(&state, false);

    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_NONE, step.kind);
    TEST_ASSERT_NULL(step.mark_reason);
    // Timer is still armed when not pending (defensive behavior)
    TEST_ASSERT_TRUE(state.timer_armed);
}

void test_ota_validator_on_timer_fired_pending(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    // Arm the timer
    ota_validator_state_on_stratum_authorized(&state, true);

    // Timer fires
    ota_validator_step_t step = ota_validator_state_on_timer_fired(&state, true);

    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_MARK_VALID, step.kind);
    TEST_ASSERT_EQUAL_STRING("sustained stratum", step.mark_reason);
    TEST_ASSERT_FALSE(state.timer_armed);
}

void test_ota_validator_happy_path_share_accepted(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    // Stratum authorizes
    ota_validator_step_t step1 = ota_validator_state_on_stratum_authorized(&state, true);
    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_START_TIMER, step1.kind);
    TEST_ASSERT_TRUE(state.timer_armed);

    // Share is accepted before timer fires
    ota_validator_step_t step2 = ota_validator_state_on_share_accepted(&state, true);
    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_STOP_TIMER_AND_MARK_VALID, step2.kind);
    TEST_ASSERT_EQUAL_STRING("first share", step2.mark_reason);
    TEST_ASSERT_FALSE(state.timer_armed);
}

void test_ota_validator_happy_path_timeout(void)
{
    ota_validator_state_t state;
    ota_validator_state_init(&state);

    // Stratum authorizes
    ota_validator_step_t step1 = ota_validator_state_on_stratum_authorized(&state, true);
    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_START_TIMER, step1.kind);
    TEST_ASSERT_TRUE(state.timer_armed);

    // Timer fires before share is accepted
    ota_validator_step_t step2 = ota_validator_state_on_timer_fired(&state, true);
    TEST_ASSERT_EQUAL_INT(OTA_VAL_ACTION_MARK_VALID, step2.kind);
    TEST_ASSERT_EQUAL_STRING("sustained stratum", step2.mark_reason);
    TEST_ASSERT_FALSE(state.timer_armed);
}
