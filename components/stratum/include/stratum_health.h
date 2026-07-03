#pragma once
#include <stdbool.h>

// Reports the stratum connection's up/down state into breadboard's
// bb_transport_health SSOT (AUTHORITATIVE class). Lazily registers the
// "stratum" slot on first call.
//
// Single-writer: only called from stratum_task (Core 0). No lock needed here
// — bb_transport_health's own ops are internally locked.
void stratum_health_report(bool up);

#ifdef STRATUM_HEALTH_TESTING
void stratum_health_reset_for_test(void);
#endif
