#pragma once

#include <stdbool.h>

typedef enum {
    ASIC_PAUSE_ACTION_NONE = 0,
    ASIC_PAUSE_ACTION_QUIESCE_AND_ACK,   // chip_quiesce(); mining_pause_check();
    ASIC_PAUSE_ACTION_ACK_ONLY,          // mining_pause_check(); (already quiesced)
    ASIC_PAUSE_ACTION_RESUME,            // chip_resume();
} asic_pause_action_t;

// Decide the next action and update the *quiesced flag in-place.
// Pure: no side effects.
asic_pause_action_t asic_pause_coalesce_next(bool pause_pending, bool *quiesced);
