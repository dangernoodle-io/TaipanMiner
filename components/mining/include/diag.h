#pragma once

// Shared log tag for opt-in instrumentation across components (mining, stratum,
// asic). Registered at boot in main.c with default level WARN so info-level
// probes are suppressed; bump via /api/log/level when investigating.
#define DIAG "diag"
