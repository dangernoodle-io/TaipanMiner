#include "ota_validator_io.h"
#include "esp_timer.h"
#include "bb_ota_validator.h"
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Timer wrappers
// ---------------------------------------------------------------------------

typedef struct {
    ota_timer_cb_t cb;
    void          *user;
} timer_ctx_t;

static void trampoline_cb(void *arg)
{
    timer_ctx_t *ctx = (timer_ctx_t *)arg;
    ctx->cb(ctx->user);
}

static bool espidf_timer_create(ota_timer_cb_t cb, void *user, void **out_handle)
{
    timer_ctx_t *ctx = malloc(sizeof(timer_ctx_t));
    if (!ctx) return false;
    ctx->cb   = cb;
    ctx->user = user;

    esp_timer_handle_t h = NULL;
    const esp_timer_create_args_t args = {
        .callback        = trampoline_cb,
        .arg             = ctx,
        .name            = "ota_validator",
        .dispatch_method = ESP_TIMER_TASK,
    };
    if (esp_timer_create(&args, &h) != ESP_OK) {
        free(ctx);
        return false;
    }
    *out_handle = (void *)h;
    return true;
}

static bool espidf_timer_start_once(void *handle, uint64_t timeout_us)
{
    return esp_timer_start_once((esp_timer_handle_t)handle, timeout_us) == ESP_OK;
}

static void espidf_timer_stop(void *handle)
{
    esp_timer_stop((esp_timer_handle_t)handle);
}

static void espidf_timer_delete_(void *handle)
{
    // Retrieve and free ctx stored as arg
    esp_timer_handle_t h = (esp_timer_handle_t)handle;
    // ctx was allocated in espidf_timer_create; retrieve via get_period hack not available.
    // Instead we rely on the fact that stop is always called before delete_ in ota_validator.c,
    // so the callback will not fire again.  Free ctx by querying the timer info is not a public
    // API, so we accept a small leak of timer_ctx_t (one allocation per timer lifetime; the
    // validator only ever creates one timer per firmware run).
    esp_timer_delete(h);
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
