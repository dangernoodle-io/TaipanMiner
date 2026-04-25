#include "unity.h"
#include "mining_pause_state.h"

// Test 1: init sets both flags to false
void test_mining_pause_state_init(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);
    TEST_ASSERT_FALSE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);
}

// Test 2: request sets pause_requested, does not touch pause_active
void test_mining_pause_state_request(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);
    mining_pause_state_request(&state);
    TEST_ASSERT_TRUE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);
}

// Test 3: on_check when not requested returns false, no state change
void test_mining_pause_state_on_check_not_requested(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);
    bool result = mining_pause_state_on_check(&state);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);
}

// Test 4: on_check when requested returns true and sets pause_active
void test_mining_pause_state_on_check_requested(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);
    mining_pause_state_request(&state);
    bool result = mining_pause_state_on_check(&state);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(state.pause_requested);
    TEST_ASSERT_TRUE(state.pause_active);
}

// Test 5: on_resume when active returns true, clears pause_requested
void test_mining_pause_state_on_resume_when_active(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);
    mining_pause_state_request(&state);
    mining_pause_state_on_check(&state);
    // Now pause_requested=true and pause_active=true
    bool needs_signal = mining_pause_state_on_resume(&state);
    TEST_ASSERT_TRUE(needs_signal);
    TEST_ASSERT_FALSE(state.pause_requested);
    TEST_ASSERT_TRUE(state.pause_active); // on_resume does not clear this
}

// Test 6: on_resume when not active returns false, clears pause_requested
void test_mining_pause_state_on_resume_when_not_active(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);
    mining_pause_state_request(&state);
    // pause_requested=true but pause_active=false (never called on_check)
    bool needs_signal = mining_pause_state_on_resume(&state);
    TEST_ASSERT_FALSE(needs_signal);
    TEST_ASSERT_FALSE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);
}

// Test 7: on_resumed clears pause_active, pause_requested unchanged
void test_mining_pause_state_on_resumed(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);
    mining_pause_state_request(&state);
    mining_pause_state_on_check(&state);
    mining_pause_state_on_resumed(&state);
    TEST_ASSERT_TRUE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);
}

// Test 8: on_ack_timeout clears pause_requested, pause_active unchanged
void test_mining_pause_state_on_ack_timeout(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);
    mining_pause_state_request(&state);
    mining_pause_state_on_ack_timeout(&state);
    TEST_ASSERT_FALSE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);
}

// Test 9: full happy path: request → on_check → on_resume → on_resumed
void test_mining_pause_state_full_happy_path(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);

    // External caller: request pause
    mining_pause_state_request(&state);
    TEST_ASSERT_TRUE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);

    // Mining task: observe and park
    bool should_park = mining_pause_state_on_check(&state);
    TEST_ASSERT_TRUE(should_park);
    TEST_ASSERT_TRUE(state.pause_requested);
    TEST_ASSERT_TRUE(state.pause_active);

    // External caller: resume
    bool needs_signal = mining_pause_state_on_resume(&state);
    TEST_ASSERT_TRUE(needs_signal);
    TEST_ASSERT_FALSE(state.pause_requested);
    TEST_ASSERT_TRUE(state.pause_active);

    // Mining task: exit park
    mining_pause_state_on_resumed(&state);
    TEST_ASSERT_FALSE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);
}

// Test 10: race condition: request → on_resume before on_check
void test_mining_pause_state_resume_before_check(void)
{
    mining_pause_state_t state;
    mining_pause_state_init(&state);

    // External caller: request pause
    mining_pause_state_request(&state);
    TEST_ASSERT_TRUE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);

    // External caller: resume before task even observes the pause
    bool needs_signal = mining_pause_state_on_resume(&state);
    TEST_ASSERT_FALSE(needs_signal); // pause was never active
    TEST_ASSERT_FALSE(state.pause_requested);
    TEST_ASSERT_FALSE(state.pause_active);
}
