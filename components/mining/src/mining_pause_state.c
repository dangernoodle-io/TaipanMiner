#include "mining_pause_state.h"

void mining_pause_state_init(mining_pause_state_t *s)
{
    s->pause_requested = false;
    s->pause_active = false;
}

void mining_pause_state_request(mining_pause_state_t *s)
{
    s->pause_requested = true;
}

bool mining_pause_state_on_check(mining_pause_state_t *s)
{
    if (!s->pause_requested) return false;
    s->pause_active = true;
    return true;
}

bool mining_pause_state_on_resume(mining_pause_state_t *s)
{
    bool was_active = s->pause_active;
    s->pause_requested = false;
    return was_active;
}

void mining_pause_state_on_resumed(mining_pause_state_t *s)
{
    s->pause_active = false;
}

void mining_pause_state_on_ack_timeout(mining_pause_state_t *s)
{
    s->pause_requested = false;
}
