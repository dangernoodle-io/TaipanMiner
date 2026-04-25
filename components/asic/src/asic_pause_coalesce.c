#include "asic_pause_coalesce.h"

asic_pause_action_t asic_pause_coalesce_next(bool pause_pending, bool *quiesced)
{
    if (pause_pending) {
        if (!*quiesced) {
            *quiesced = true;
            return ASIC_PAUSE_ACTION_QUIESCE_AND_ACK;
        }
        return ASIC_PAUSE_ACTION_ACK_ONLY;
    }
    if (*quiesced) {
        *quiesced = false;
        return ASIC_PAUSE_ACTION_RESUME;
    }
    return ASIC_PAUSE_ACTION_NONE;
}
