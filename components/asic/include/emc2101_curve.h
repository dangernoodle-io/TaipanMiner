#pragma once

// Temperature → fan duty curve (host-testable, no ESP-IDF dependency).
// Returns duty percent [0, 100]:
//   temp <= 45°C  → 30%
//   45–70°C       → linear 30→55%
//   70–85°C       → linear 55→100%
//   temp >= 85°C  → 100%
static inline int emc2101_duty_for_temp_c(float temp_c)
{
    if (temp_c <= 45.0f) return 30;
    if (temp_c >= 85.0f) return 100;
    if (temp_c < 70.0f) {
        // 45–70°C: 30→55%, slope 1.0%/°C
        return (int)(30.0f + (temp_c - 45.0f) * 1.0f);
    }
    // 70–85°C: 55→100%, slope 3.0%/°C
    return (int)(55.0f + (temp_c - 70.0f) * 3.0f);
}
