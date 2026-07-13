#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bb_json.h"

// Format mining.submit params into buf. Returns number of chars written (excluding null), or -1 on truncation.
// When version_hex is NULL or empty string -> 5-field format (no version). Otherwise 6-field format.
int format_submit_params(char *buf, size_t buf_size,
                         const char *wallet_addr, const char *worker_name,
                         const char *job_id, const char *extranonce2_hex,
                         const char *ntime_hex, const char *nonce_hex,
                         const char *version_hex);

// Format a JSON-RPC request into buf. Returns chars written (excl null), or -1 on truncation.
int format_stratum_request(char *buf, size_t buf_size,
                           int id, const char *method, const char *params_json);

// Parse stratum error code from a JSON-RPC error item: handles both the
// [code, "message", "data"] array form and the {"code": N, ...} object form.
// Returns the error code, or -1 if absent/unparseable.
int stratum_parse_error_code(bb_json_t error_item);

// ---------------------------------------------------------------------------
// Hex codec helpers -- relocated from the pre-rebuild mining/work.h (mining
// doesn't exist in the v2 floor yet; these are pure byte<->hex helpers the
// protocol parser needs regardless of which component eventually owns
// coinbase/header building).
// ---------------------------------------------------------------------------

// Convert a hex string to raw bytes (up to max_out bytes). Returns count of
// bytes written. Tolerant of a trailing odd nibble (dropped) and NULL input.
size_t hex_to_bytes(const char *hex, uint8_t *out, size_t max_out);

// Convert len raw bytes to a lowercase NUL-terminated hex string. `hex` must
// have room for 2*len+1 bytes.
void bytes_to_hex(const uint8_t *data, size_t len, char *hex);

// Decode a stratum-format prevhash (64 hex chars = 8 groups of 4
// byte-reversed bytes) into 32 raw bytes.
void decode_stratum_prevhash(const char *hex, uint8_t prevhash[32]);
