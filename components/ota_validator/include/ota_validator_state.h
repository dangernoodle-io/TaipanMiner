#pragma once

#include <stdbool.h>

typedef enum {
    OTA_VAL_ACTION_NONE = 0,
    OTA_VAL_ACTION_START_TIMER,
    OTA_VAL_ACTION_STOP_TIMER_AND_MARK_VALID,
    OTA_VAL_ACTION_MARK_VALID,
} ota_validator_action_t;

typedef struct {
    ota_validator_action_t kind;
    const char *mark_reason;   // valid iff kind marks valid; otherwise NULL
} ota_validator_step_t;

typedef struct {
    bool timer_armed;
} ota_validator_state_t;

void ota_validator_state_init(ota_validator_state_t *s);

// Returns the action the outer adapter should execute. Pure function.
// is_pending: result of bb_ota_is_pending() at call time.
ota_validator_step_t ota_validator_state_on_stratum_authorized(ota_validator_state_t *s, bool is_pending);
ota_validator_step_t ota_validator_state_on_share_accepted(ota_validator_state_t *s, bool is_pending);
ota_validator_step_t ota_validator_state_on_timer_fired(ota_validator_state_t *s, bool is_pending);
