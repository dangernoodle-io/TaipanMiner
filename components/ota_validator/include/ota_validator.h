#pragma once

#include <stdbool.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"

// Start OTA validation state machine.
// Call once from app_main after checking OTA state.
// If firmware is not in PENDING_VERIFY state, sets pending flag to false immediately.
void ota_validator_start(void);

// Notify validator that stratum has authorized.
// Starts the 10-minute safety-net timer if not already pending valid.
void ota_validator_on_stratum_authorized(void);

// Notify validator that a share was accepted.
// Marks firmware valid immediately with reason "first share".
void ota_validator_on_share_accepted(void);

// Manual mark-valid via HTTP API.
// Returns ESP_ERR_INVALID_STATE if not pending (already marked valid or boot not in PENDING_VERIFY).
// Returns ESP_OK on success.
esp_err_t ota_validator_mark_valid_manual(void);

// Check if firmware is still pending validation.
// Returns false if already marked valid or not pending at boot.
bool ota_validator_is_pending(void);

#endif
