#include "stratum_health.h"
#include "bb_transport_health.h"

static bb_transport_handle_t s_stratum_th = BB_TRANSPORT_HANDLE_INVALID;

// Single-writer: only called from stratum_task (Core 0). No lock needed;
// bb_transport_health's own ops are internally locked.
void stratum_health_report(bool up)
{
    if (s_stratum_th == BB_TRANSPORT_HANDLE_INVALID) {
        bb_transport_health_register("stratum", BB_TRANSPORT_AUTHORITATIVE, &s_stratum_th);
    }
    bb_transport_health_report(s_stratum_th, up);  // up=true clears failing; false sets it
}

#ifdef STRATUM_HEALTH_TESTING
void stratum_health_reset_for_test(void)
{
    s_stratum_th = BB_TRANSPORT_HANDLE_INVALID;
}
#endif
