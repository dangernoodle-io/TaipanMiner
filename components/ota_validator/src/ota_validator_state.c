#include "ota_validator_state.h"
#include <stddef.h>

void ota_validator_state_init(ota_validator_state_t *s) {
    s->timer_armed = false;
}

ota_validator_step_t ota_validator_state_on_stratum_authorized(ota_validator_state_t *s, bool is_pending) {
    ota_validator_step_t step = { OTA_VAL_ACTION_NONE, NULL };
    if (!is_pending) return step;
    if (s->timer_armed) return step;
    s->timer_armed = true;
    step.kind = OTA_VAL_ACTION_START_TIMER;
    return step;
}

ota_validator_step_t ota_validator_state_on_share_accepted(ota_validator_state_t *s, bool is_pending) {
    ota_validator_step_t step = { OTA_VAL_ACTION_NONE, NULL };
    if (!is_pending) return step;
    if (s->timer_armed) {
        s->timer_armed = false;
        step.kind = OTA_VAL_ACTION_STOP_TIMER_AND_MARK_VALID;
    } else {
        step.kind = OTA_VAL_ACTION_MARK_VALID;
    }
    step.mark_reason = "first share";
    return step;
}

ota_validator_step_t ota_validator_state_on_timer_fired(ota_validator_state_t *s, bool is_pending) {
    ota_validator_step_t step = { OTA_VAL_ACTION_NONE, NULL };
    if (!is_pending) return step;
    s->timer_armed = false;
    step.kind = OTA_VAL_ACTION_MARK_VALID;
    step.mark_reason = "sustained stratum";
    return step;
}
