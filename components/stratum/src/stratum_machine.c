#include "stratum_machine.h"
#include <stdio.h>
#include <string.h>

// Build mining.configure request params (version rolling support).
// Output: [["version-rolling"],{"version-rolling.mask":"1fffe000","version-rolling.min-bit-count":13}]
// Returns: chars written (excluding null terminator) on success, -1 on truncation.
int stratum_machine_build_configure(char *buf, size_t n)
{
    if (!buf || !n) {
        return -1;
    }

    int result = snprintf(buf, n,
                         "[[\"version-rolling\"],"
                         "{\"version-rolling.mask\":\"1fffe000\","
                         "\"version-rolling.min-bit-count\":13}]");

    // Check for truncation
    if (result < 0 || (size_t)result >= n) {
        return -1;
    }

    return result;
}

// Build mining.subscribe request params.
// Output: ["TaipanMiner/0.1"]
// Returns: chars written (excluding null terminator) on success, -1 on truncation.
int stratum_machine_build_subscribe(char *buf, size_t n)
{
    if (!buf || !n) {
        return -1;
    }

    int result = snprintf(buf, n, "[\"TaipanMiner/0.1\"]");

    // Check for truncation
    if (result < 0 || (size_t)result >= n) {
        return -1;
    }

    return result;
}

// Build mining.authorize request params.
// Output: ["<wallet>.<worker>","<pass>"]
// Returns: chars written (excluding null terminator) on success, -1 on truncation.
int stratum_machine_build_authorize(char *buf, size_t n,
                                    const char *wallet, const char *worker,
                                    const char *pass)
{
    if (!buf || !n || !wallet || !worker || !pass) {
        return -1;
    }

    int result = snprintf(buf, n, "[\"%s.%s\",\"%s\"]",
                         wallet, worker, pass);

    // Check for truncation
    if (result < 0 || (size_t)result >= n) {
        return -1;
    }

    return result;
}

// Build mining.suggest_difficulty request params (used as app-level keepalive).
// Output: [<difficulty with %.4f format>]
// Returns: chars written (excluding null terminator) on success, -1 on truncation.
int stratum_machine_build_keepalive(char *buf, size_t n, double difficulty)
{
    if (!buf || !n) {
        return -1;
    }

    int result = snprintf(buf, n, "[%.4f]", difficulty);

    // Check for truncation
    if (result < 0 || (size_t)result >= n) {
        return -1;
    }

    return result;
}
