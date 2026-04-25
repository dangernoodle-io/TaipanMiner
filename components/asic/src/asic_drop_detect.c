#include "asic_drop_detect.h"

asic_drop_detect_step_t asic_drop_detect_evaluate(float ghs, float sanity_max,
                                                   uint64_t now_us,
                                                   uint64_t last_warn_us,
                                                   uint64_t cooldown_us)
{
    asic_drop_detect_step_t step = {
        .accept = false,
        .should_warn = false,
        .new_last_warn_us = last_warn_us,
    };

    if (ghs < sanity_max) {
        step.accept = true;
        return step;
    }

    // Sample fails sanity. Decide whether to log.
    if ((now_us - last_warn_us) >= cooldown_us) {
        step.should_warn = true;
        step.new_last_warn_us = now_us;
    }
    return step;
}
