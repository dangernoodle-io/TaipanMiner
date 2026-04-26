#include "stratum_utils.h"
#include "bb_json.h"
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

    // Check for truncation
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
