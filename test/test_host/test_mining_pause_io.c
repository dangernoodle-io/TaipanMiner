#include "unity.h"
#include "mining.h"
#include "mining_pause_io.h"
#include "mining_pause_state.h"

// ---------------------------------------------------------------------------
// Mock fixture
// ---------------------------------------------------------------------------

typedef struct {
    int      mutex_take_calls, mutex_give_calls;
    int      ack_take_calls,   ack_give_calls;
    int      done_take_calls,  done_give_calls;
    uint32_t last_mutex_timeout, last_ack_timeout, last_done_timeout;
    bool     mutex_take_returns;
    bool     ack_take_returns;
    bool     done_take_returns;
    int      ack_take_invocation; // running count of ack_take calls (for N-th-call override)
} mock_sync_t;

static mock_sync_t s_mock;

static bool mock_mutex_take(uint32_t timeout_ms)
{
    s_mock.mutex_take_calls++;
    s_mock.last_mutex_timeout = timeout_ms;
    return s_mock.mutex_take_returns;
}

static void mock_mutex_give(void) { s_mock.mutex_give_calls++; }

static bool mock_ack_take(uint32_t timeout_ms)
{
    s_mock.ack_take_calls++;
    s_mock.ack_take_invocation++;
    s_mock.last_ack_timeout = timeout_ms;
    return s_mock.ack_take_returns;
}

static void mock_ack_give(void) { s_mock.ack_give_calls++; }

static bool mock_done_take(uint32_t timeout_ms)
{
    s_mock.done_take_calls++;
    s_mock.last_done_timeout = timeout_ms;
    return s_mock.done_take_returns;
}

static void mock_done_give(void) { s_mock.done_give_calls++; }

static const mining_pause_sync_ops_t s_mock_ops = {
    .mutex_take = mock_mutex_take,
    .mutex_give = mock_mutex_give,
    .ack_take   = mock_ack_take,
    .ack_give   = mock_ack_give,
    .done_take  = mock_done_take,
    .done_give  = mock_done_give,
};

static void fixture_init(void)
{
    s_mock = (mock_sync_t){
        .mutex_take_returns = true,
        .ack_take_returns   = true,
        .done_take_returns  = true,
    };
    mining_pause_init(&s_mock_ops);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// After init, mining_pause_pending() must return false.
void test_init_stores_ops_and_inits_state(void)
{
    fixture_init();
    TEST_ASSERT_FALSE(mining_pause_pending());
}

// Happy-path pause: mutex taken with 30000ms, ack taken with 15000ms.
// Mutex is NOT released on success (caller holds it until resume).
void test_pause_happy_path(void)
{
    fixture_init();
    bool result = mining_pause();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(1, s_mock.mutex_take_calls);
    TEST_ASSERT_EQUAL_UINT32(30000, s_mock.last_mutex_timeout);
    TEST_ASSERT_EQUAL_INT(1, s_mock.ack_take_calls);
    TEST_ASSERT_EQUAL_UINT32(15000, s_mock.last_ack_timeout);
    TEST_ASSERT_EQUAL_INT(0, s_mock.mutex_give_calls); // still held
}

// Mutex timeout: returns false, no ack/done calls, no give.
void test_pause_mutex_timeout(void)
{
    fixture_init();
    s_mock.mutex_take_returns = false;
    bool result = mining_pause();
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(1, s_mock.mutex_take_calls);
    TEST_ASSERT_EQUAL_INT(0, s_mock.ack_take_calls);
    TEST_ASSERT_EQUAL_INT(0, s_mock.done_take_calls);
    TEST_ASSERT_EQUAL_INT(0, s_mock.mutex_give_calls);
}

// Ack timeout: returns false, mutex released once.
void test_pause_ack_timeout(void)
{
    fixture_init();
    s_mock.ack_take_returns = false;
    bool result = mining_pause();
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(1, s_mock.mutex_take_calls);
    TEST_ASSERT_EQUAL_INT(1, s_mock.ack_take_calls);
    TEST_ASSERT_EQUAL_INT(1, s_mock.mutex_give_calls);
    // State is cleared by on_ack_timeout — subsequent pending query returns false.
    TEST_ASSERT_FALSE(mining_pause_pending());
}

// Resume after successful pause: done_give called (active), then mutex released.
void test_resume_happy_path(void)
{
    fixture_init();
    mining_pause();                 // puts state into pause_requested + ack received
    // Simulate the mining task calling on_check to set pause_active:
    // We need pause_active=true for resume to send done_give.
    // mining_pause_check() sets active and waits on done — but that would block.
    // Instead call resume directly; since pause_active is still false at this point
    // (on_check not called), resume returns needs_signal=false.
    // To test needs_signal=true path, we first call mining_pause_check which
    // sets pause_active and then waits. That would block.
    //
    // The cleanest approach: call mining_resume() right after mining_pause() without
    // an intervening on_check. State: requested=true, active=false.
    // on_resume returns false → no done_give. mutex_give should still be called.
    mining_resume();
    TEST_ASSERT_EQUAL_INT(0, s_mock.done_give_calls);  // active was false
    TEST_ASSERT_EQUAL_INT(1, s_mock.mutex_give_calls);
}

// Resume when pause_active=true (done_give must be called).
// We drive active directly by calling mining_pause_check with a done that returns
// immediately. But mining_pause_check blocks on done_take — so we need done_take
// to return true right away (default). The sequence is:
//   mining_pause()          → mutex_take, request, ack_take
//   mining_pause_check()    → on_check sets active, ack_give, done_take(300000) → true, on_resumed
//   mining_resume()         → on_resume (active already false after on_resumed) → no done_give
//
// Actually: after on_resumed(), pause_active=false. So mining_resume would see
// active=false and not call done_give. The "active" path in resume is called when
// the CHECK is still parked (done_take hasn't returned yet). That's a real concurrency
// scenario not reachable in a single-threaded test directly.
//
// We test the observable: resume after pause_check completes normally → 0 done_give,
// 1 mutex_give (mutex was held by mining_pause).
void test_resume_after_pause_check_normal(void)
{
    fixture_init();
    mining_pause();             // mutex held
    mining_pause_check();       // ack_give, done_take → true, on_resumed
    // Now call resume: pause_requested=false (cleared by on_check? No — on_check
    // doesn't clear it. on_resume clears it). pause_active=false (cleared by on_resumed).
    // on_resume: active=false → needs_signal=false, clears requested.
    int done_before = s_mock.done_give_calls;
    mining_resume();
    TEST_ASSERT_EQUAL_INT(done_before, s_mock.done_give_calls); // no extra done_give
    TEST_ASSERT_EQUAL_INT(1, s_mock.mutex_give_calls);
}

// Resume without an active pause (pause requested but task never checked).
// on_resume should return false (no done_give), mutex_give called.
void test_resume_without_active_no_done_signal(void)
{
    fixture_init();
    // Manually set requested state without going through pause() (which takes mutex).
    // Instead call pause() to get the right state, then resume.
    // After mining_pause(): requested=true, mutex held, ack consumed.
    mining_pause();
    // Don't call mining_pause_check() — task never observed the pause.
    // pause_active=false.
    mining_resume();
    TEST_ASSERT_EQUAL_INT(0, s_mock.done_give_calls);   // active was false
    TEST_ASSERT_EQUAL_INT(1, s_mock.mutex_give_calls);
}

// mining_pause_check without prior request: returns false, no semaphore traffic.
void test_pause_check_no_request_returns_false(void)
{
    fixture_init();
    bool result = mining_pause_check();
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(0, s_mock.ack_give_calls);
    TEST_ASSERT_EQUAL_INT(0, s_mock.done_take_calls);
}

// Full check cycle: pause → check(done_take=true) → on_resumed clears active.
void test_pause_check_request_normal_resume(void)
{
    fixture_init();
    mining_pause();             // sets requested, takes ack
    bool checked = mining_pause_check();
    TEST_ASSERT_TRUE(checked);
    TEST_ASSERT_EQUAL_INT(1, s_mock.ack_give_calls);
    TEST_ASSERT_EQUAL_INT(1, s_mock.done_take_calls);
    TEST_ASSERT_EQUAL_UINT32(300000, s_mock.last_done_timeout);
    // on_resumed() clears pause_active but not pause_requested — caller must call
    // mining_resume() to clear the request and release the mutex.
    TEST_ASSERT_TRUE(mining_pause_pending());
}

// TA-277 regression: done_take timeout clears BOTH flags so next on_check does not re-pause.
void test_pause_check_done_timeout_TA277(void)
{
    fixture_init();
    s_mock.done_take_returns = false;
    mining_pause();
    bool checked = mining_pause_check();
    TEST_ASSERT_TRUE(checked);
    TEST_ASSERT_EQUAL_INT(1, s_mock.done_take_calls);
    // Both flags cleared by on_done_timeout — next check must not re-pause.
    TEST_ASSERT_FALSE(mining_pause_pending());
    bool re_check = mining_pause_check();
    TEST_ASSERT_FALSE(re_check);
    // No additional ack_give for the second check.
    TEST_ASSERT_EQUAL_INT(1, s_mock.ack_give_calls);
}

// Concurrent pause serialized via mutex: second call returns false when mutex unavailable.
void test_concurrent_pause_serialized_via_mutex(void)
{
    fixture_init();
    // First call succeeds; second call fails on mutex_take.
    mining_pause();                          // call 1 — succeeds, mutex_take_calls=1
    s_mock.mutex_take_returns = false;       // simulate mutex held by first caller
    bool second = mining_pause();
    TEST_ASSERT_FALSE(second);
    TEST_ASSERT_EQUAL_INT(2, s_mock.mutex_take_calls);
    TEST_ASSERT_EQUAL_INT(0, s_mock.mutex_give_calls); // no give for either (first still holds)
}
