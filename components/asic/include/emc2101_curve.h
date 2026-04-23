#pragma once

// Temperature → fan duty curve (host-testable, no ESP-IDF dependency).
// Interim tune to match AxeOS behavior at 60°C until TA-141 (setpoint
// control) lands. Returns duty percent [0, 100]:
//   temp <= 50°C  → 30%
//   50–60°C       → linear 30→40%  (slope 1.0)
//   60–75°C       → linear 40→70%  (slope 2.0)
//   75–85°C       → linear 70→100% (slope 3.0)
//   temp >= 85°C  → 100%
static inline int emc2101_duty_for_temp_c(float temp_c)
{
    if (temp_c <= 50.0f) return 30;
    if (temp_c >= 85.0f) return 100;
    if (temp_c < 60.0f) {
        return (int)(30.0f + (temp_c - 50.0f) * 1.0f);
    }
    if (temp_c < 75.0f) {
        return (int)(40.0f + (temp_c - 60.0f) * 2.0f);
    }
    return (int)(70.0f + (temp_c - 75.0f) * 3.0f);
}
