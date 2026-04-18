#pragma once

// Temperature → fan duty curve (host-testable, no ESP-IDF dependency).
// Returns duty percent [0, 100]:
//   temp <= 40°C  → 30%
//   40–60°C       → linear 30→60%
//   60–75°C       → linear 60→100%
//   temp >= 75°C  → 100%
static inline int emc2101_duty_for_temp_c(float temp_c)
{
    if (temp_c <= 40.0f) return 30;
    if (temp_c >= 75.0f) return 100;
    if (temp_c < 60.0f) {
        // 40–60°C: 30→60%, slope 1.5%/°C
        return (int)(30.0f + (temp_c - 40.0f) * 1.5f);
    }
    // 60–75°C: 60→100%, slope ~2.667%/°C
    return (int)(60.0f + (temp_c - 60.0f) * (40.0f / 15.0f));
}
