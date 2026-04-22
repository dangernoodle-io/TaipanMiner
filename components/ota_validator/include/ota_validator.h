#pragma once

// Notify validator that stratum has authorized.
// Starts the 15-minute safety-net timer.
void ota_validator_on_stratum_authorized(void);

// Notify validator that a share was accepted.
// Marks firmware valid immediately with reason "first share".
void ota_validator_on_share_accepted(void);
