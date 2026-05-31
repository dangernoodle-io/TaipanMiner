#include "ota_validator_io.h"
#include "bb_timer.h"
#include "bb_ota_validator.h"

// ---------------------------------------------------------------------------
// Timer wrappers — one-shot timer via bb_timer (esp_timer leak removed).
// bb_timer_oneshot stores the cb+arg internally, so the ota_timer_cb_t is
// passed straight through. The prior esp_timer path needed a trampoline +
// a heap timer_ctx_t it could never free (esp_timer has no get-arg API), so
// it deliberately leaked one allocation per timer; that's gone now.
// ---------------------------------------------------------------------------

static bool espidf_timer_create(ota_timer_cb_t cb, void *user, void **out_handle)
{
    bb_oneshot_timer_t t = NULL;
    if (bb_timer_oneshot_create(cb, user, "ota_validator", &t) != BB_OK) {
        return false;
    }
    *out_handle = (void *)t;
    return true;
}

static bool espidf_timer_start_once(void *handle, uint64_t timeout_us)
{
    return bb_timer_oneshot_start((bb_oneshot_timer_t)handle, timeout_us) == BB_OK;
}

static void espidf_timer_stop(void *handle)
{
    bb_timer_oneshot_stop((bb_oneshot_timer_t)handle);
}

static void espidf_timer_delete_(void *handle)
{
    bb_timer_oneshot_delete((bb_oneshot_timer_t)handle);
}

// ---------------------------------------------------------------------------
// Mark-valid wrappers
// ---------------------------------------------------------------------------

static bool espidf_is_pending(void)
{
    return bb_ota_is_pending();
}

static void espidf_mark_valid(const char *reason)
{
    bb_ota_mark_valid(reason);
}

// ---------------------------------------------------------------------------
// Default op tables
// ---------------------------------------------------------------------------

const ota_timer_ops_t g_ota_timer_ops_default = {
    .create      = espidf_timer_create,
    .start_once  = espidf_timer_start_once,
    .stop        = espidf_timer_stop,
    .delete_     = espidf_timer_delete_,
};

const ota_mark_valid_ops_t g_ota_mark_valid_ops_default = {
    .is_pending  = espidf_is_pending,
    .mark_valid  = espidf_mark_valid,
};
