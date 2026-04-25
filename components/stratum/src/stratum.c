#include "stratum.h"
#include "stratum_backoff.h"
#include "stratum_utils.h"
#include "bb_nv.h"
#include "taipan_config.h"
#include "mining.h"
#include "work.h"
#include "sha256.h"
#include "board.h"
#include "ota_validator.h"
#include "bb_wifi.h"
#include "bb_log.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/tcp.h>

#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bb_json.h"

static const char *TAG = "stratum";

// Stratum state
static int s_sock = -1;
static int s_msg_id = 1;
static char s_extranonce1_hex[32];
static uint8_t s_extranonce1[MAX_EXTRANONCE1_SIZE];
static size_t s_extranonce1_len = 0;
static int s_extranonce2_size = 4;  // bytes
static double s_difficulty = 512.0;  // sane default for ASIC mining; pool overrides via set_difficulty
static stratum_job_t s_job;
static int s_subscribe_id = 0;
static int s_authorize_id = 0;
static uint32_t s_version_mask = 0;
static int s_configure_id = 0;
static int s_keepalive_id = 0;
static const char *s_wallet_addr;
static const char *s_worker_name;
static uint32_t s_extranonce2 = 0;     // rolling extranonce2 counter
static uint32_t s_work_seq = 0;        // work sequence counter
static TickType_t s_last_job_tick = 0; // last job dispatch tick
static TickType_t s_last_pool_job_tick = 0; // last mining.notify from pool (not extranonce2 roll)
static TickType_t s_last_share_tick = 0; // last share submission tick (30-min watchdog)
static TickType_t s_last_tx_tick = 0;    // last stratum TX (for app-level keepalive)
static TickType_t s_session_start_tick = 0;
static stratum_backoff_t s_backoff = {
    .delay_ms = STRATUM_BACKOFF_INITIAL_MS,
    .fail_count = 0,
};
static volatile bool s_stratum_connected = false;
static volatile bool s_reconnect_requested = false;
static stratum_wifi_kick_cb_t s_wifi_kick_cb = NULL;

// Line buffer for reading from socket
static char s_linebuf[4096];
static int s_linebuf_len = 0;
static int s_rcvtimeo_ms = -1;

// Send a string to the socket
static int stratum_send(const char *msg)
{
    int len = strlen(msg);
    int total = 0;
    while (total < len) {
        int sent = send(s_sock, msg + total, len - total, 0);
        if (sent < 0) {
            bb_log_e(TAG, "send error: %d", errno);
            return -1;
        }
        total += sent;
    }
    s_last_tx_tick = xTaskGetTickCount();
    bb_log_d(TAG, ">> %s", msg);
    return 0;
}

bool stratum_is_connected(void)
{
    return s_stratum_connected;
}

void stratum_request_reconnect(void)
{
    s_reconnect_requested = true;
}

void stratum_set_wifi_kick_cb(stratum_wifi_kick_cb_t cb)
{
    s_wifi_kick_cb = cb;
}

uint32_t stratum_get_reconnect_delay_ms(void)
{
    return s_backoff.delay_ms;
}

int stratum_get_connect_fail_count(void)
{
    return s_backoff.fail_count;
}

// Send a JSON-RPC request. Returns assigned message id, or -1 on error.
static int stratum_request(const char *method, const char *params_json)
{
    char buf[512];
    int id = s_msg_id++;
    if (format_stratum_request(buf, sizeof(buf), id, method, params_json) < 0) {
        return -1;
    }
    if (stratum_send(buf) != 0) {
        return -1;
    }
    return id;
}

// Read one newline-terminated line from socket. Returns line length, 0 for no data yet, -1 on error.
static int stratum_readline(char *out, int max_len, int timeout_ms)
{
    // Check if we already have a complete line in buffer
    for (int i = 0; i < s_linebuf_len; i++) {
        if (s_linebuf[i] == '\n') {
            int line_len = i;  // exclude newline
            if (line_len >= max_len) line_len = max_len - 1;
            memcpy(out, s_linebuf, line_len);
            out[line_len] = '\0';
            // Shift buffer
            int remaining = s_linebuf_len - (i + 1);
            if (remaining > 0) {
                memmove(s_linebuf, s_linebuf + i + 1, remaining);
            }
            s_linebuf_len = remaining;
            return line_len;
        }
    }

    // Read more data from socket
    if (timeout_ms != s_rcvtimeo_ms) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        s_rcvtimeo_ms = timeout_ms;
    }

    int space = sizeof(s_linebuf) - s_linebuf_len - 1;
    if (space <= 0) {
        bb_log_w(TAG, "line buffer overflow, discarding");
        s_linebuf_len = 0;
        return 0;
    }

    int n = recv(s_sock, s_linebuf + s_linebuf_len, space, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // timeout, no data
        }
        bb_log_e(TAG, "recv error: %d", errno);
        return -1;
    }
    if (n == 0) {
        bb_log_w(TAG, "connection closed");
        return -1;
    }

    s_linebuf_len += n;

    // Try again to find a line
    for (int i = 0; i < s_linebuf_len; i++) {
        if (s_linebuf[i] == '\n') {
            int line_len = i;
            if (line_len >= max_len) line_len = max_len - 1;
            memcpy(out, s_linebuf, line_len);
            out[line_len] = '\0';
            int remaining = s_linebuf_len - (i + 1);
            if (remaining > 0) {
                memmove(s_linebuf, s_linebuf + i + 1, remaining);
            }
            s_linebuf_len = remaining;
            return line_len;
        }
    }

    return 0;  // no complete line yet
}

// Connect TCP socket to pool
static int stratum_connect(const char *host, uint16_t port)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    // Gate: skip DNS lookup if WiFi station has no IP yet
    if (!bb_wifi_has_ip()) {
        static int64_t s_last_no_ip_log_us = 0;
        int64_t now_us = esp_timer_get_time();
        if (now_us - s_last_no_ip_log_us > 10000000) {  // 10s rate limit
            bb_log_w(TAG, "no IP yet, deferring DNS lookup");
            s_last_no_ip_log_us = now_us;
        }
        return -1;
    }

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0 || res == NULL) {
        bb_log_e(TAG, "DNS lookup failed: %d", err);
        if (res) freeaddrinfo(res);
        return -1;
    }

    s_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s_sock < 0) {
        bb_log_e(TAG, "socket failed: %d", errno);
        freeaddrinfo(res);
        return -1;
    }

    // Connect timeout — prevent blocking for 75+ seconds on unreachable hosts
    struct timeval conn_tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &conn_tv, sizeof(conn_tv));

    if (connect(s_sock, res->ai_addr, res->ai_addrlen) != 0) {
        bb_log_e(TAG, "connect failed: %d", errno);
        close(s_sock);
        s_sock = -1;
        freeaddrinfo(res);
        return -1;
    }

    // TCP keepalive — detect dead pool connections
    int ka = 1, idle = 60, intvl = 10, cnt = 3;
    setsockopt(s_sock, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));
    setsockopt(s_sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(s_sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(s_sock, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));

    // Send timeout — prevent indefinite blocking on half-open connections
    struct timeval snd_tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &snd_tv, sizeof(snd_tv));

    // Disable Nagle — send shares immediately
    int nodelay = 1;
    setsockopt(s_sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    freeaddrinfo(res);
    s_linebuf_len = 0;
    s_msg_id = 1;
    s_rcvtimeo_ms = -1;

    bb_log_i(TAG, "connected to %s:%d", host, port);
    return 0;
}

// Handle mining.configure response
static void handle_configure_result(bb_json_t result)
{
    bb_json_t vr = bb_json_obj_get_item(result, "version-rolling");
    if (bb_json_item_is_true(vr)) {
        bb_json_t mask_j = bb_json_obj_get_item(result, "version-rolling.mask");
        if (mask_j && bb_json_item_is_string(mask_j)) {
            s_version_mask = (uint32_t)strtoul(bb_json_item_get_string(mask_j), NULL, 16);
            bb_log_i(TAG, "version rolling enabled, mask=0x%08" PRIx32, s_version_mask);
        }
    }
}

// Build mining work from current job
static bool build_work(mining_work_t *work)
{
    // Build extranonce2 from rolling counter (LE byte order)
    uint8_t extranonce2[MAX_EXTRANONCE2_SIZE];
    memset(extranonce2, 0, sizeof(extranonce2));
    for (size_t i = 0; i < s_extranonce2_size && i < sizeof(uint32_t); i++) {
        extranonce2[i] = (uint8_t)(s_extranonce2 >> (i * 8));
    }

    // Store extranonce2 hex in work
    char en2_hex[17];
    bytes_to_hex(extranonce2, s_extranonce2_size, en2_hex);
    strncpy(work->extranonce2_hex, en2_hex, sizeof(work->extranonce2_hex) - 1);
    work->extranonce2_hex[sizeof(work->extranonce2_hex) - 1] = '\0';

    // Build coinbase hash
    uint8_t coinbase_hash[32];
    build_coinbase_hash(s_job.coinb1, s_job.coinb1_len,
                        s_extranonce1, s_extranonce1_len,
                        extranonce2, s_extranonce2_size,
                        s_job.coinb2, s_job.coinb2_len,
                        coinbase_hash);

    char cb_hex[65];
    bytes_to_hex(coinbase_hash, 32, cb_hex);
    bb_log_d(TAG, "coinbase_hash: %s", cb_hex);

    // Build merkle root
    uint8_t merkle_root[32];
    build_merkle_root(coinbase_hash, s_job.merkle_branches, s_job.merkle_count, merkle_root);

    char mr_hex[65];
    bytes_to_hex(merkle_root, 32, mr_hex);
    bb_log_d(TAG, "merkle_root: %s", mr_hex);

    // Serialize header
    serialize_header(s_job.version, s_job.prevhash, merkle_root,
                     s_job.ntime, s_job.nbits, 0, work->header);

    // Set target from current difficulty
    difficulty_to_target(s_difficulty, work->target);
    work->difficulty = s_difficulty;

    if (!is_target_valid(work->target)) {
        bb_log_e(TAG, "build_work: invalid target for diff=%.6f, dropping work", s_difficulty);
        return false;
    }

    bb_log_d(TAG, "build_work: diff=%.6f target=%02x%02x%02x%02x %02x%02x%02x%02x",
             s_difficulty,
             work->target[31], work->target[30], work->target[29], work->target[28],
             work->target[27], work->target[26], work->target[25], work->target[24]);

    work->version = s_job.version;
    work->version_mask = s_version_mask;
    work->ntime = s_job.ntime;
    strncpy(work->job_id, s_job.job_id, sizeof(work->job_id) - 1);
    work->job_id[sizeof(work->job_id) - 1] = '\0';
    work->work_seq = ++s_work_seq;
    return true;
}

// Handle mining.notify
static void handle_notify(bb_json_t params)
{
    bb_json_t arr = params;
    if (!bb_json_item_is_array(arr) || bb_json_arr_size(arr) < 9) {
        bb_log_w(TAG, "invalid notify params");
        return;
    }

    // Parse job fields
    bb_json_t job_id_j = bb_json_arr_get_item(arr, 0);
    bb_json_t prevhash_j = bb_json_arr_get_item(arr, 1);
    bb_json_t coinb1_j = bb_json_arr_get_item(arr, 2);
    bb_json_t coinb2_j = bb_json_arr_get_item(arr, 3);
    bb_json_t merkle_j = bb_json_arr_get_item(arr, 4);
    bb_json_t version_j = bb_json_arr_get_item(arr, 5);
    bb_json_t nbits_j = bb_json_arr_get_item(arr, 6);
    bb_json_t ntime_j = bb_json_arr_get_item(arr, 7);
    bb_json_t clean_j = bb_json_arr_get_item(arr, 8);

    if (!job_id_j || !prevhash_j || !coinb1_j || !coinb2_j ||
        !merkle_j || !version_j || !nbits_j || !ntime_j) {
        bb_log_w(TAG, "missing notify fields");
        return;
    }

    // Check that all string fields are actually strings
    if (!bb_json_item_is_string(job_id_j) || !bb_json_item_is_string(prevhash_j) ||
        !bb_json_item_is_string(coinb1_j) || !bb_json_item_is_string(coinb2_j) ||
        !bb_json_item_is_string(version_j) || !bb_json_item_is_string(nbits_j) || !bb_json_item_is_string(ntime_j)) {
        bb_log_w(TAG, "notify fields have wrong type");
        return;
    }

    // Copy job_id
    strncpy(s_job.job_id, bb_json_item_get_string(job_id_j), sizeof(s_job.job_id) - 1);
    s_job.job_id[sizeof(s_job.job_id) - 1] = '\0';

    // Decode prevhash (stratum format: 8 groups of 4 bytes, each reversed)
    decode_stratum_prevhash(bb_json_item_get_string(prevhash_j), s_job.prevhash);

    // Decode coinb1
    s_job.coinb1_len = hex_to_bytes(bb_json_item_get_string(coinb1_j), s_job.coinb1, MAX_COINB1_SIZE);

    // Decode coinb2
    s_job.coinb2_len = hex_to_bytes(bb_json_item_get_string(coinb2_j), s_job.coinb2, MAX_COINB2_SIZE);

    // Decode merkle branches
    s_job.merkle_count = 0;
    int branch_count = bb_json_arr_size(merkle_j);
    for (int i = 0; i < branch_count && i < MAX_MERKLE_BRANCHES; i++) {
        bb_json_t branch = bb_json_arr_get_item(merkle_j, i);
        if (branch && bb_json_item_is_string(branch)) {
            hex_to_bytes(bb_json_item_get_string(branch), s_job.merkle_branches[i], 32);
            s_job.merkle_count++;
        }
    }

    // Parse version, nbits, ntime (hex strings → uint32)
    s_job.version = (uint32_t)strtoul(bb_json_item_get_string(version_j), NULL, 16);
    s_job.nbits = (uint32_t)strtoul(bb_json_item_get_string(nbits_j), NULL, 16);
    s_job.ntime = (uint32_t)strtoul(bb_json_item_get_string(ntime_j), NULL, 16);
    s_job.clean_jobs = clean_j ? bb_json_item_is_true(clean_j) : false;

    bb_log_i(TAG, "notify: job=%s clean=%d ver=%s ntime=%s nbits=%s",
             s_job.job_id, s_job.clean_jobs,
             bb_json_item_get_string(version_j), bb_json_item_get_string(ntime_j), bb_json_item_get_string(nbits_j));

    s_extranonce2 = 0;
    mining_work_t work;
    if (!build_work(&work)) return;

    // Debug: dump first 80 bytes of header as hex
    char hdr_hex[161];
    bytes_to_hex(work.header, 80, hdr_hex);
    bb_log_d(TAG, "header: %s", hdr_hex);
    work.clean = s_job.clean_jobs;

    xQueueOverwrite(work_queue, &work);
    s_last_job_tick = xTaskGetTickCount();
    s_last_pool_job_tick = s_last_job_tick;
    stratum_backoff_reset(&s_backoff);  // reset on successful job receipt
}

// Handle mining.set_difficulty
static void handle_set_difficulty(bb_json_t params)
{
    if (!bb_json_item_is_array(params) || bb_json_arr_size(params) < 1) {
        return;
    }
    bb_json_t diff = bb_json_arr_get_item(params, 0);
    if (bb_json_item_is_number(diff)) {
        s_difficulty = bb_json_item_get_double(diff);
        bb_log_i(TAG, "difficulty set to %.4f", s_difficulty);

        // Re-dispatch work with updated target — mark clean to invalidate
        // stale job table entries that carry the old (easier) target
        if (s_job.job_id[0] != '\0') {
            mining_work_t work;
            if (!build_work(&work)) return;
            work.clean = true;
            xQueueOverwrite(work_queue, &work);
        }
    }
}

// Handle subscribe response
static int handle_subscribe_result(bb_json_t result)
{
    if (!bb_json_item_is_array(result) || bb_json_arr_size(result) < 3) {
        bb_log_e(TAG, "invalid subscribe result");
        return -1;
    }

    // result[1] = extranonce1 (hex string)
    bb_json_t en1 = bb_json_arr_get_item(result, 1);
    if (!en1 || !bb_json_item_is_string(en1)) {
        bb_log_e(TAG, "no extranonce1");
        return -1;
    }
    strncpy(s_extranonce1_hex, bb_json_item_get_string(en1), sizeof(s_extranonce1_hex) - 1);
    s_extranonce1_hex[sizeof(s_extranonce1_hex) - 1] = '\0';
    s_extranonce1_len = hex_to_bytes(s_extranonce1_hex, s_extranonce1, MAX_EXTRANONCE1_SIZE);

    // result[2] = extranonce2_size
    bb_json_t en2sz = bb_json_arr_get_item(result, 2);
    if (bb_json_item_is_number(en2sz)) {
        s_extranonce2_size = bb_json_item_get_int(en2sz);
    }

    bb_log_i(TAG, "subscribed: en1=%s en2_size=%d", s_extranonce1_hex, s_extranonce2_size);
    return 0;
}

// Submit a share
static int submit_share(mining_result_t *result)
{
    char params[256];
    if (format_submit_params(params, sizeof(params),
                            s_wallet_addr, s_worker_name,
                            result->job_id, result->extranonce2_hex,
                            result->ntime_hex, result->nonce_hex,
                            result->version_hex) < 0) {
        return -1;
    }

    bb_log_i(TAG, "submit: %s", params);
    int rc = stratum_request("mining.submit", params);
    if (rc >= 0) {
        s_last_share_tick = xTaskGetTickCount();
    }
    return rc < 0 ? -1 : 0;
}

// Process one JSON message from pool
static void process_message(const char *line)
{
    bb_json_t json = bb_json_parse(line, 0);
    if (!json) {
        bb_log_w(TAG, "invalid JSON");
        return;
    }

    bb_log_d(TAG, "<< %s", line);

    bb_json_t method = bb_json_obj_get_item(json, "method");
    bb_json_t id_item = bb_json_obj_get_item(json, "id");
    bb_json_t result_item = bb_json_obj_get_item(json, "result");
    bb_json_t params = bb_json_obj_get_item(json, "params");
    bb_json_t error_item = bb_json_obj_get_item(json, "error");

    if (method && bb_json_item_is_string(method)) {
        // Server notification
        const char *method_str = bb_json_item_get_string(method);
        if (strcmp(method_str, "mining.notify") == 0) {
            handle_notify(params);
        } else if (strcmp(method_str, "mining.set_difficulty") == 0) {
            handle_set_difficulty(params);
        } else {
            bb_log_d(TAG, "unhandled method: %s", method_str);
        }
    } else if (id_item && bb_json_item_is_number(id_item)) {
        // Response to our request
        int id = bb_json_item_get_int(id_item);
        if (id == s_subscribe_id) {
            // Subscribe response
            if (result_item) {
                handle_subscribe_result(result_item);
            }
        } else if (id == s_authorize_id) {
            // Authorize response
            if (result_item && bb_json_item_is_true(result_item)) {
                bb_log_i(TAG, "authorized");
                ota_validator_on_stratum_authorized();
            } else {
                bb_log_e(TAG, "authorization failed");
                if (error_item && !bb_json_item_is_null(error_item)) {
                    char *err_str = bb_json_item_serialize(error_item);
                    if (err_str) {
                        bb_log_e(TAG, "error: %s", err_str);
                        bb_json_free_str(err_str);
                    }
                }
            }
        } else if (id == s_configure_id) {
            // Configure response (BIP 320 version rolling) — non-fatal
            if (result_item && bb_json_item_is_object(result_item)) {
                handle_configure_result(result_item);
            } else {
                bb_log_w(TAG, "pool does not support mining.configure, version rolling disabled");
            }
        } else if (s_keepalive_id != 0 && id == s_keepalive_id) {
            bb_log_d(TAG, "keepalive ack id=%d", id);
        } else {
            // Submit response or other
            if (error_item && !bb_json_item_is_null(error_item)) {
                char *err_str = bb_json_item_serialize(error_item);
                if (err_str) {
                    bb_log_e(TAG, "share rejected: %s", err_str);
                    bb_json_free_str(err_str);
                }
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    mining_stats.session.rejected++;
                    xSemaphoreGive(mining_stats.mutex);
                }
            } else if (result_item && bb_json_item_is_true(result_item)) {
                bb_log_i(TAG, "share accepted");
                ota_validator_on_share_accepted();
                int64_t now_us = esp_timer_get_time();
                mining_lifetime_t lt_snap = {0};
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    mining_stats.hw_shares++;
                    mining_stats.session.shares++;
                    mining_stats.session.last_share_us = now_us;
                    mining_stats.lifetime.total_shares++;
#ifdef ASIC_CHIP
                    mining_stats.asic_shares++;
#endif
                    lt_snap = mining_stats.lifetime;
                    xSemaphoreGive(mining_stats.mutex);
                }
                mining_stats_save_lifetime(&lt_snap);
            }
        }
    }

    bb_json_free(json);
}

void stratum_task(void *arg)
{
    static char line[4096];

    bb_log_i(TAG, "stratum task started");

    for (;;) {
        // Read config
        const char *pool_host = taipan_config_pool_host();
        uint16_t pool_port = taipan_config_pool_port();
        s_wallet_addr = taipan_config_wallet_addr();
        s_worker_name = taipan_config_worker_name();
        const char *pool_pass = taipan_config_pool_pass();

        bb_log_i(TAG, "connecting to %s:%u wallet=%s worker=%s",
                 pool_host, pool_port, s_wallet_addr, s_worker_name);

        // Connect
        if (stratum_connect(pool_host, pool_port) != 0) {
            stratum_backoff_step_t step = stratum_backoff_on_fail(&s_backoff);
            bb_log_w(TAG, "connect failed (%d/%d), reconnecting in %" PRIu32 "ms",
                     s_backoff.fail_count, STRATUM_BACKOFF_KICK_THRESHOLD, step.sleep_ms);
            if (step.outcome == STRATUM_BACKOFF_OUTCOME_KICK && s_wifi_kick_cb) {
                bb_log_w(TAG, "forcing WiFi reassociation after %d consecutive failures",
                         STRATUM_BACKOFF_KICK_THRESHOLD);
                s_wifi_kick_cb();
            }
            vTaskDelay(pdMS_TO_TICKS(step.sleep_ms));
            continue;
        }
        stratum_backoff_reset(&s_backoff);

        // Configure (version rolling) — non-fatal if pool doesn't support it
        s_configure_id = stratum_request("mining.configure",
            "[[\"version-rolling\"],"
            "{\"version-rolling.mask\":\"1fffe000\","
            "\"version-rolling.min-bit-count\":13}]");
        if (s_configure_id < 0) {
            bb_log_w(TAG, "configure send failed, skipping version rolling");
            s_configure_id = 0;
        }

        // Subscribe
        s_subscribe_id = stratum_request("mining.subscribe", "[\"TaipanMiner/0.1\"]");
        if (s_subscribe_id < 0) {
            goto reconnect;
        }

        // Wait for subscribe response
        for (int i = 0; i < 50; i++) {  // 5s timeout
            int n = stratum_readline(line, sizeof(line), 100);
            if (n > 0) {
                process_message(line);
                if (s_extranonce1_len > 0) break;
            } else if (n < 0) {
                goto reconnect;
            }
        }

        if (s_extranonce1_len == 0) {
            bb_log_e(TAG, "subscribe timeout");
            goto reconnect;
        }

        // Authorize
        {
            char auth_params[256];
            int n = snprintf(auth_params, sizeof(auth_params),
                     "[\"%s.%s\",\"%s\"]",
                     s_wallet_addr, s_worker_name,
                     pool_pass);
            if (n < 0 || n >= (int)sizeof(auth_params)) {
                bb_log_e(TAG, "auth params truncated");
                goto reconnect;
            }
            s_authorize_id = stratum_request("mining.authorize", auth_params);
            if (s_authorize_id < 0) {
                goto reconnect;
            }
        }

        // Wait for authorize response, set_difficulty, and initial notify
        bool job_received = false;
        for (int i = 0; i < 50; i++) {  // 5s timeout
            int n = stratum_readline(line, sizeof(line), 100);
            if (n > 0) {
                process_message(line);
                if (s_job.job_id[0] != '\0') {
                    job_received = true;
                }
            } else if (n < 0) {
                goto reconnect;
            }
        }
        if (!job_received) {
            bb_log_w(TAG, "no job received during handshake, continuing to main loop");
        }

        s_stratum_connected = true;
        s_last_job_tick = xTaskGetTickCount();
        s_last_pool_job_tick = s_last_job_tick;
        s_last_tx_tick = s_last_job_tick;
        s_session_start_tick = xTaskGetTickCount();

        // Main loop: read messages and submit shares
        for (;;) {
            // While mining is paused (e.g. OTA on Core 0), don't drain the
            // stratum socket — it contends with esp_https_ota for Core 0 and
            // TLS buffer bandwidth on the tdongle, causing WDT resets mid-OTA.
            // TCP keepalive (60s idle) holds the session; pool messages queue
            // up in the socket buffer and get processed on resume.
            if (mining_pause_pending()) {
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            // Try to read a message (100ms timeout to check results)
            int n = stratum_readline(line, sizeof(line), 100);
            if (n > 0) {
                process_message(line);
            } else if (n < 0) {
                break;  // connection error
            }

            // Check for mining results to submit
            mining_result_t result;
            while (xQueueReceive(result_queue, &result, 0) == pdTRUE) {
                if (submit_share(&result) != 0) {
                    goto reconnect;
                }
            }

            // External reconnect request (e.g. IP loss)
            if (s_reconnect_requested) {
                s_reconnect_requested = false;
                bb_log_w(TAG, "reconnect requested externally");
                break;
            }

            // Reconnect if no new pool job received for 5 minutes
            if (s_last_pool_job_tick != 0 &&
                (xTaskGetTickCount() - s_last_pool_job_tick) >= pdMS_TO_TICKS(300000)) {
                bb_log_w(TAG, "no new job for 5 minutes, reconnecting");
                break;
            }

            // Reconnect if no share submitted in 30 minutes
            {
                TickType_t share_ref = s_last_share_tick ? s_last_share_tick : s_session_start_tick;
                if (share_ref != 0 &&
                    (xTaskGetTickCount() - share_ref) >= pdMS_TO_TICKS(1800000)) {
                    bb_log_w(TAG, "no share submitted in 30 minutes, reconnecting");
                    break;
                }
            }

            // Periodic job dispatch — feed miner fresh nonce space (e.g. ASIC)
            if (g_miner_config.extranonce2_roll) {
                TickType_t now = xTaskGetTickCount();
                if (s_job.job_id[0] != '\0' &&
                    (now - s_last_job_tick) >= pdMS_TO_TICKS(g_miner_config.roll_interval_ms)) {
                    s_extranonce2++;
                    mining_work_t work;
                    if (!build_work(&work)) continue;
                    work.clean = false;
                    xQueueOverwrite(work_queue, &work);
                    s_last_job_tick = now;
                }
            }

            // App-level keepalive — keep NAT table alive on routers that ignore TCP keepalives
            {
                TickType_t now = xTaskGetTickCount();
                if (s_last_tx_tick != 0 &&
                    (now - s_last_tx_tick) >= pdMS_TO_TICKS(90000)) {
                    bb_log_d(TAG, "sending keepalive ping");
                    char params[32];
                    snprintf(params, sizeof(params), "[%.4f]", s_difficulty);
                    int kid = stratum_request("mining.suggest_difficulty", params);
                    if (kid < 0) {
                        bb_log_w(TAG, "keepalive send failed, reconnecting");
                        break;
                    }
                    s_keepalive_id = kid;
                }
            }
        }

reconnect:
        s_stratum_connected = false;
        xQueueReset(work_queue);
        if (s_sock >= 0) {
            close(s_sock);
            s_sock = -1;
        }
        s_extranonce1_len = 0;
        s_subscribe_id = 0;
        s_authorize_id = 0;
        s_configure_id = 0;
        s_keepalive_id = 0;
        s_version_mask = 0;
        memset(&s_job, 0, sizeof(s_job));
        s_last_share_tick = 0;
        s_last_pool_job_tick = 0;
        s_last_tx_tick = 0;
        // Drain stale shares from previous session
        {
            mining_result_t stale;
            while (xQueueReceive(result_queue, &stale, 0) == pdTRUE) {}
        }
        stratum_backoff_step_t step = stratum_backoff_on_fail(&s_backoff);
        bb_log_w(TAG, "reconnecting in %" PRIu32 "ms", step.sleep_ms);
        if (step.outcome == STRATUM_BACKOFF_OUTCOME_KICK && s_wifi_kick_cb) {
            bb_log_w(TAG, "forcing WiFi reassociation after %d consecutive failures",
                     STRATUM_BACKOFF_KICK_THRESHOLD);
            s_wifi_kick_cb();
        }
        vTaskDelay(pdMS_TO_TICKS(step.sleep_ms));
    }
}
