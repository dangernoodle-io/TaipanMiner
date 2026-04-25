#pragma once

#include <stdbool.h>
#include <stdint.h>

// Result of evaluating a single GHS sample against the sanity cap +
// per-(chip, metric) warning cooldown. Pure: no side effects; caller
// applies the decision.
typedef struct {
    bool     accept;            // true → sample passes sanity, store it
    bool     should_warn;       // true → log a warn (cooldown elapsed)
    uint64_t new_last_warn_us;  // value to write back into the warn-timestamp slot
} asic_drop_detect_step_t;

// Decide what to do with a computed GHS sample.
// - ghs < sanity_max → accept; warn fields unused.
// - ghs >= sanity_max → reject; warn fires iff (now - last_warn) >= cooldown.
asic_drop_detect_step_t asic_drop_detect_evaluate(float ghs, float sanity_max,
                                                   uint64_t now_us,
                                                   uint64_t last_warn_us,
                                                   uint64_t cooldown_us);
