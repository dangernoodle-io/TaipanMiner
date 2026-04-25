#pragma once

#include <stddef.h>

// Window sizes for progressive-blend averaging.
// Caller's buffers must be exactly these sizes.
#define ASIC_METRIC_AVG_1M_SIZE   12   // 12 * 5s poll = 60s
#define ASIC_METRIC_AVG_10M_SIZE  10
#define ASIC_METRIC_AVG_1H_SIZE   6
#define ASIC_METRIC_AVG_DIV_10M   ASIC_METRIC_AVG_1M_SIZE
#define ASIC_METRIC_AVG_DIV_1H    (ASIC_METRIC_AVG_10M_SIZE * ASIC_METRIC_AVG_DIV_10M)

// NaN-safe mean: ignores NaN entries. Returns 0.0f if all entries are NaN.
float asic_metric_avg_nan_safe(const float *buf, size_t n);

// One step of the progressive-blend rolling-window update.
// poll_count: monotonic counter incremented once per poll cycle (caller manages it).
// sample: new value to fold in.
// buf_1m / buf_10m / buf_1h: ring buffers sized per the constants above.
// prev_10m / prev_1h: previous-window blend state (caller-owned, init to NAN).
// out_1m / out_10m / out_1h: receive the recomputed averages.
void asic_metric_avg_update(unsigned long poll_count,
                            float sample,
                            float *buf_1m, float *buf_10m, float *buf_1h,
                            float *prev_10m, float *prev_1h,
                            float *out_1m, float *out_10m, float *out_1h);
