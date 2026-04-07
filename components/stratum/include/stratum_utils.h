#pragma once

#include <stddef.h>

// Format mining.submit params into buf. Returns number of chars written (excluding null), or -1 on truncation.
// When version_hex is NULL or empty string → 5-field format (no version). Otherwise 6-field format.
int format_submit_params(char *buf, size_t buf_size,
                         const char *wallet_addr, const char *worker_name,
                         const char *job_id, const char *extranonce2_hex,
                         const char *ntime_hex, const char *nonce_hex,
                         const char *version_hex);

// Format a JSON-RPC request into buf. Returns chars written (excl null), or -1 on truncation.
int format_stratum_request(char *buf, size_t buf_size,
                           int id, const char *method, const char *params_json);
