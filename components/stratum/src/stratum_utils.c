#include "stratum_utils.h"
#include <stdio.h>
#include <string.h>

int format_submit_params(char *buf, size_t buf_size,
                         const char *wallet_addr, const char *worker_name,
                         const char *job_id, const char *extranonce2_hex,
                         const char *ntime_hex, const char *nonce_hex,
                         const char *version_hex)
{
    if (!buf || !buf_size || !wallet_addr || !worker_name || !job_id ||
        !extranonce2_hex || !ntime_hex || !nonce_hex) {
        return -1;
    }

    int result;
    if (version_hex && version_hex[0] != '\0') {
        // 6-field format with version
        result = snprintf(buf, buf_size,
                         "[\"%s.%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]",
                         wallet_addr, worker_name,
                         job_id, extranonce2_hex,
                         ntime_hex, nonce_hex, version_hex);
    } else {
        // 5-field format without version
        result = snprintf(buf, buf_size,
                         "[\"%s.%s\",\"%s\",\"%s\",\"%s\",\"%s\"]",
                         wallet_addr, worker_name,
                         job_id, extranonce2_hex,
                         ntime_hex, nonce_hex);
    }

    // Check for truncation: snprintf returns the number of characters that
    // would have been written if the buffer was large enough (excluding null)
    if (result < 0 || (size_t)result >= buf_size) {
        return -1;
    }

    return result;
}

int format_stratum_request(char *buf, size_t buf_size,
                           int id, const char *method, const char *params_json)
{
    if (!buf || !buf_size || !method || !params_json) {
        return -1;
    }

    int result = snprintf(buf, buf_size,
                         "{\"id\":%d,\"method\":\"%s\",\"params\":%s}\n",
                         id, method, params_json);

    if (result < 0 || (size_t)result >= buf_size) {
        return -1;
    }

    return result;
}

int stratum_parse_error_code(bb_json_t error_item)
{
    /* Array form first: [code, "message", "data"]. */
    bb_json_t arr0 = bb_json_arr_get_item(error_item, 0);
    if (bb_json_item_is_number(arr0)) {
        return (int)bb_json_item_get_double(arr0);
    }
    /* Object form: {"code": N, ...}. */
    double n;
    if (bb_json_obj_get_number(error_item, "code", &n)) {
        return (int)n;
    }
    return -1;
}

size_t hex_to_bytes(const char *hex, uint8_t *out, size_t max_out)
{
    if (!hex || !out) {
        return 0;
    }

    size_t count = 0;
    size_t hex_len = strlen(hex);

    for (size_t i = 0; i + 1 < hex_len && count < max_out; i += 2) {
        char high = hex[i];
        char low = hex[i + 1];

        uint8_t high_nibble = 0;
        uint8_t low_nibble = 0;

        if (high >= '0' && high <= '9') {
            high_nibble = high - '0';
        } else if (high >= 'a' && high <= 'f') {
            high_nibble = 10 + (high - 'a');
        } else if (high >= 'A' && high <= 'F') {
            high_nibble = 10 + (high - 'A');
        }

        if (low >= '0' && low <= '9') {
            low_nibble = low - '0';
        } else if (low >= 'a' && low <= 'f') {
            low_nibble = 10 + (low - 'a');
        } else if (low >= 'A' && low <= 'F') {
            low_nibble = 10 + (low - 'A');
        }

        out[count] = (high_nibble << 4) | low_nibble;
        count++;
    }

    return count;
}

void bytes_to_hex(const uint8_t *data, size_t len, char *hex)
{
    if (!data || !hex) {
        return;
    }

    const char hex_chars[] = "0123456789abcdef";

    for (size_t i = 0; i < len; i++) {
        hex[2 * i] = hex_chars[(data[i] >> 4) & 0xF];
        hex[2 * i + 1] = hex_chars[data[i] & 0xF];
    }

    hex[2 * len] = '\0';
}

void decode_stratum_prevhash(const char *hex, uint8_t prevhash[32])
{
    // Stratum sends prevhash as 64 hex chars = 32 bytes, organized as 8
    // groups of 4 bytes (8 hex chars each); each group has its bytes reversed.
    uint8_t raw[32];
    hex_to_bytes(hex, raw, 32);

    for (int i = 0; i < 8; i++) {
        int group_start = i * 4;
        prevhash[group_start]     = raw[group_start + 3];
        prevhash[group_start + 1] = raw[group_start + 2];
        prevhash[group_start + 2] = raw[group_start + 1];
        prevhash[group_start + 3] = raw[group_start];
    }
}
