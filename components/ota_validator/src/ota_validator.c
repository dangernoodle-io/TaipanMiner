#include "ota_validator.h"
#include "ota_validator_io.h"
#include "ota_validator_state.h"
#include "bb_log.h"

static const char *TAG = "ota_validator";

#define OTA_VALIDATOR_TIMEOUT_US (15 * 60 * 1000000)  // 15 minutes

static const ota_timer_ops_t      *s_timer_ops  = NULL;
static const ota_mark_valid_ops_t *s_mark_ops   = NULL;

static ota_validator_state_t s_state;
static void                 *s_timer_handle = NULL;

void ota_validator_init(const ota_timer_ops_t *t, const ota_mark_valid_ops_t *m)
{
    s_timer_ops = t;
    s_mark_ops  = m;
    ota_validator_state_init(&s_state);
}

static void timer_fired_cb(void *user)
{
    (void)user;
    if (!s_mark_ops) return;
    ota_validator_step_t step = ota_validator_state_on_timer_fired(&s_state, s_mark_ops->is_pending());
    if (step.kind == OTA_VAL_ACTION_MARK_VALID) {
        s_mark_ops->mark_valid(step.mark_reason);
    }
    // Timer already fired; do not delete here.
}

void ota_validator_on_stratum_authorized(void)
{
    if (!s_timer_ops || !s_mark_ops) return;

    ota_validator_step_t step = ota_validator_state_on_stratum_authorized(&s_state, s_mark_ops->is_pending());

    if (step.kind == OTA_VAL_ACTION_START_TIMER) {
        if (s_timer_ops->create(timer_fired_cb, NULL, &s_timer_handle)) {
            s_timer_ops->start_once(s_timer_handle, OTA_VALIDATOR_TIMEOUT_US);
            bb_log_i(TAG, "15-minute mark-valid timer started");
        }
    }
}

void ota_validator_on_share_accepted(void)
{
    if (!s_timer_ops || !s_mark_ops) return;

    ota_validator_step_t step = ota_validator_state_on_share_accepted(&s_state, s_mark_ops->is_pending());

    if (step.kind == OTA_VAL_ACTION_STOP_TIMER_AND_MARK_VALID) {
        if (s_timer_handle) {
            s_timer_ops->stop(s_timer_handle);
            s_timer_ops->delete_(s_timer_handle);
            s_timer_handle = NULL;
        }
        s_mark_ops->mark_valid(step.mark_reason);
    } else if (step.kind == OTA_VAL_ACTION_MARK_VALID) {
        s_mark_ops->mark_valid(step.mark_reason);
    }
}
