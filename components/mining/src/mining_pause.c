#include "mining.h"
#include "mining_pause_io.h"
#include "mining_pause_state.h"
#include "bb_log.h"
#include <inttypes.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "mining";

static const mining_pause_sync_ops_t *s_ops = NULL;
static mining_pause_state_t s_pause_state;

void mining_pause_init(const mining_pause_sync_ops_t *ops)
{
    s_ops = ops;
    mining_pause_state_init(&s_pause_state);
}

bool mining_pause(void)
{
    if (!s_ops->mutex_take(30000)) {
        bb_log_w(TAG, "mining pause mutex timeout — another caller holds pause");
        return false;
    }
    mining_pause_state_request(&s_pause_state);
    // 15s covers the worst-case chip_resume freq ramp (~10s on BM1370 at
    // 650 MHz). Fix #1 (coalesce) should prevent most timeouts, but this is
    // belt-and-suspenders for races where the pause arrives just inside the
    // ramp window.
    if (!s_ops->ack_take(15000)) {
        bb_log_w(TAG, "mining pause acknowledge timeout, resetting state");
        mining_pause_state_on_ack_timeout(&s_pause_state);
        s_ops->mutex_give();
        return false;
    }
    return true;
}

void mining_resume(void)
{
    bool needs_signal = mining_pause_state_on_resume(&s_pause_state);
    if (needs_signal) s_ops->done_give();
    s_ops->mutex_give();
}

bool mining_pause_pending(void)
{
    return s_pause_state.pause_requested;
}

bool mining_pause_check(void)
{
    if (!mining_pause_state_on_check(&s_pause_state)) return false;
    bb_log_i(TAG, "mining paused for maintenance");
    s_ops->ack_give();
    // 5 min covers OTA pull worst case (firmware download over weak WiFi).
    // The prior 30s budget timed out mid-download on slow links, and because
    // on_resumed() only cleared pause_active the task re-paused on the next
    // Tier-1 hop and cycled until OTA finally called mining_resume(). See
    // TA-277. on_done_timeout clears both flags as belt-and-suspenders so a
    // future watchdog event does not regress to cycling.
    if (!s_ops->done_take(300000)) {
        bb_log_e(TAG, "mining resume timeout, resuming anyway");
        mining_pause_state_on_done_timeout(&s_pause_state);
    } else {
        mining_pause_state_on_resumed(&s_pause_state);
    }
#ifdef ESP_PLATFORM
    bb_log_i(TAG, "mining resumed (stack high water: %" PRIu32 ")",
             (uint32_t)uxTaskGetStackHighWaterMark(NULL));
#else
    bb_log_i(TAG, "mining resumed (stack high water: %" PRIu32 ")", (uint32_t)0);
#endif
    return true;
}
