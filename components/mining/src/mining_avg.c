#include "mining_avg.h"

#include <math.h>

// NaN-safe check: a NaN value is not equal to itself
static inline int is_not_nan(float x)
{
    return x == x;
}

// NaN-safe mean: ignores NaN entries. Returns 0.0f if all entries are NaN.
float mining_avg_nan_safe(const float *buf, size_t n)
{
    float sum = 0.0f;
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        if (is_not_nan(buf[i])) {
            sum += buf[i];
            count++;
        }
    }
    return count > 0 ? sum / (float)count : 0.0f;
}

// Progressive-blend averaging — matches ESP-Miner's update_hashrate_averages.
void mining_avg_update(unsigned long poll_count,
                            float sample,
                            float *buf_1m, float *buf_10m, float *buf_1h,
                            float *prev_10m, float *prev_1h,
                            float *out_1m, float *out_10m, float *out_1h)
{
    buf_1m[poll_count % MINING_AVG_1M_SIZE] = sample;
    *out_1m = mining_avg_nan_safe(buf_1m, MINING_AVG_1M_SIZE);

    int blend_10m = poll_count % MINING_AVG_1M_SIZE;
    if (blend_10m == 0) {
        *prev_10m = buf_10m[(poll_count / MINING_AVG_DIV_10M) % MINING_AVG_10M_SIZE];
    }
    float v_10m = *out_1m;
    if (is_not_nan(*prev_10m)) {
        float f = (blend_10m + 1.0f) / (float)MINING_AVG_1M_SIZE;
        v_10m = f * v_10m + (1.0f - f) * (*prev_10m);
    }
    buf_10m[(poll_count / MINING_AVG_DIV_10M) % MINING_AVG_10M_SIZE] = v_10m;
    *out_10m = mining_avg_nan_safe(buf_10m, MINING_AVG_10M_SIZE);

    int blend_1h = poll_count % MINING_AVG_DIV_1H;
    if (blend_1h == 0) {
        *prev_1h = buf_1h[(poll_count / MINING_AVG_DIV_1H) % MINING_AVG_1H_SIZE];
    }
    float v_1h = *out_10m;
    if (is_not_nan(*prev_1h)) {
        float f = (blend_1h + 1.0f) / (float)MINING_AVG_DIV_1H;
        v_1h = f * v_1h + (1.0f - f) * (*prev_1h);
    }
    buf_1h[(poll_count / MINING_AVG_DIV_1H) % MINING_AVG_1H_SIZE] = v_1h;
    *out_1h = mining_avg_nan_safe(buf_1h, MINING_AVG_1H_SIZE);
}

// Pool-effective rolling sampler. Computes delta from diff sum, applies
// rate conversion formula, then feeds through the standard smoother.
void mining_pool_eff_tick(double sum, double period_s, double *prev_sum,
                          unsigned long poll_count,
                          float *buf_1m, float *buf_10m, float *buf_1h,
                          float *prev_10m, float *prev_1h,
                          float *out_1m, float *out_10m, float *out_1h)
{
    double delta = sum - *prev_sum;
    if (delta < 0.0) delta = 0.0;  /* pool reconnect resets sum */
    *prev_sum = sum;
    float sample = (float)(delta * 4294967296.0 / period_s);
    mining_avg_update(poll_count, sample,
                      buf_1m, buf_10m, buf_1h,
                      prev_10m, prev_1h,
                      out_1m, out_10m, out_1h);
}
