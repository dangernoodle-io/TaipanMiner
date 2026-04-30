#include "stratum.h"
#include "stratum_backoff.h"
#include "stratum_watchdogs.h"
#include "stratum_utils.h"
#include "stratum_machine.h"
#include "bb_nv.h"
#include "taipan_config.h"
#include "mining.h"
#include "diag.h"
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
static stratum_state_t s_state = {
    .next_msg_id = 1,
    .extranonce2_size = 4,
    .difficulty = 512.0,
};
static const char *s_wallet_addr;
static const char *s_worker_name;
static TickType_t s_last_job_tick = 0; // last job dispatch tick
static TickType_t s_last_pool_job_tick = 0; // last mining.notify from pool (not extranonce2 roll)
static TickType_t s_last_share_tick = 0; // last share submission tick (30-min watchdog)
static int64_t s_last_submit_us = 0;     // diag: esp_timer timestamp of last mining.submit send
static TickType_t s_last_tx_tick = 0;    // last stratum TX (for app-level keepalive)
static TickType_t s_session_start_tick = 0;
static stratum_backoff_t s_backoff = {
    .delay_ms = STRATUM_BACKOFF_INITIAL_MS,
    .fail_count = 0,
};
static volatile bool s_stratum_connected = false;
static volatile bool s_reconnect_requested = false;
static stratum_wifi_kick_cb_t s_wifi_kick_cb = NULL;

// Pool RTT tracking (TA-118)
#define STRATUM_INFLIGHT_MAX 32
static int64_t s_inflight_send_us[STRATUM_INFLIGHT_MAX] = {0};
static double s_pool_rtt_ms_ema = 0.0;
static bool s_pool_rtt_initialized = false;

// Pool failover state (TA-202)
static volatile int s_active_pool_idx = TAIPAN_POOL_PRIMARY;
static int s_consecutive_fail_count = 0;
#define STRATUM_FAILOVER_THRESHOLD 3

/* TA-306: extranonce.subscribe runtime status (per-session, reset on reconnect). */
static volatile stratum_extranonce_sub_status_t s_extranonce_sub_status = STRATUM_EXNX_SUB_OFF;

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

bb_err_t stratum_request_switch_pool(int idx)
{
    if (idx < 0 || idx >= TAIPAN_POOL_COUNT) {
        return BB_ERR_INVALID_ARG;
    }
    if (!taipan_config_pool_configured(idx)) {
        return BB_ERR_INVALID_ARG;
    }
    if (idx == s_active_pool_idx) {
        return BB_OK;  // no-op
    }
    s_active_pool_idx = idx;
    s_consecutive_fail_count = 0;
    s_reconnect_requested = true;
    return BB_OK;
}

uint32_t stratum_get_reconnect_delay_ms(void)
{
    return s_backoff.delay_ms;
}

int stratum_get_connect_fail_count(void)
{
    return s_backoff.fail_count;
}

uint32_t stratum_get_session_start_ms(void)
{
    if (!s_stratum_connected) return 0;
    return pdTICKS_TO_MS(s_session_start_tick);
}

double stratum_get_difficulty(void)
{
    return s_state.difficulty;
}

int stratum_get_pool_rtt_ms(void)
{
    return s_pool_rtt_initialized ? (int)s_pool_rtt_ms_ema : -1;
}

int stratum_get_active_pool_idx(void)
{
    return s_stratum_connected ? s_active_pool_idx : -1;
}

stratum_extranonce_sub_status_t stratum_get_extranonce_subscribe_status(void)
{
    return s_extranonce_sub_status;
}

bool stratum_get_session_snapshot(stratum_session_snapshot_t *out)
{
    if (!s_stratum_connected || !out) return false;
    out->extranonce1      = s_state.extranonce1;
    out->extranonce1_len  = s_state.extranonce1_len;
    out->extranonce2_size = s_state.extranonce2_size;
    out->version_mask     = s_state.version_mask;
    return true;
}

bool stratum_get_job_snapshot(const stratum_job_t **out)
{
    if (!s_stratum_connected || !out) return false;
    if (s_state.job.job_id[0] == '\0') return false;
    *out = &s_state.job;
    return true;
}

// Send a JSON-RPC request. Returns assigned message id, or -1 on error.
static int stratum_request(const char *method, const char *params_json)
{
    char buf[512];
    int id = s_state.next_msg_id++;
    if (format_stratum_request(buf, sizeof(buf), id, method, params_json) < 0) {
        return -1;
    }
    if (stratum_send(buf) != 0) {
        return -1;
    }
    // Record send time for RTT measurement (TA-118)
    int slot = id % STRATUM_INFLIGHT_MAX;
    s_inflight_send_us[slot] = esp_timer_get_time();
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
    s_state.next_msg_id = 1;
    s_rcvtimeo_ms = -1;

    // Reset RTT tracking on new connection (TA-118)
    memset(s_inflight_send_us, 0, sizeof(s_inflight_send_us));
    s_pool_rtt_initialized = false;

    bb_log_i(TAG, "connected to %s:%d", host, port);
    return 0;
}

// Handle mining.configure response
static void handle_configure_result(bb_json_t result)
{
    if (stratum_machine_handle_configure_result(&s_state, result)) {
        bb_log_i(TAG, "version rolling enabled, mask=0x%08" PRIx32, s_state.version_mask);
    }
}

// Build mining work from current job
static bool build_work(mining_work_t *work)
{
    if (!stratum_machine_build_work(&s_state, work)) {
        bb_log_e(TAG, "build_work: invalid target for diff=%.6f, dropping work", s_state.difficulty);
        return false;
    }

    bb_log_d(TAG, "build_work: diff=%.6f target=%02x%02x%02x%02x %02x%02x%02x%02x",
             s_state.difficulty,
             work->target[31], work->target[30], work->target[29], work->target[28],
             work->target[27], work->target[26], work->target[25], work->target[24]);
    return true;
}

// Handle mining.notify
static void handle_notify(bb_json_t params)
{
    if (!stratum_machine_handle_notify(&s_state, params)) {
        bb_log_w(TAG, "invalid notify params");
        return;
    }

    bb_log_i(TAG, "notify: job=%s clean=%d ver=%08" PRIx32 " ntime=%08" PRIx32 " nbits=%08" PRIx32,
             s_state.job.job_id, s_state.job.clean_jobs,
             s_state.job.version, s_state.job.ntime, s_state.job.nbits);

    mining_work_t work;
    if (!build_work(&work)) return;

    // Debug: dump first 80 bytes of header as hex
    char hdr_hex[161];
    bytes_to_hex(work.header, 80, hdr_hex);
    bb_log_d(TAG, "header: %s", hdr_hex);
    work.clean = s_state.job.clean_jobs;

    xQueueOverwrite(work_queue, &work);
    s_last_job_tick = xTaskGetTickCount();
    s_last_pool_job_tick = s_last_job_tick;
    stratum_backoff_reset(&s_backoff);  // reset on successful job receipt
}

// Handle mining.set_extranonce (TA-306). Pool can roll extranonce1 mid-session
// when we've subscribed via mining.extranonce.subscribe. Updates s_state in
// place; the next work cycle picks up the new values via the existing
// extranonce2_roll path. In-flight ASIC work prepared with the old
// extranonce1 will be rejected as "stale" by the pool — acceptable because
// the work loop runs every 500ms.
static void handle_set_extranonce(bb_json_t params)
{
    if (!stratum_machine_handle_set_extranonce(&s_state, params)) {
        bb_log_w(TAG, "invalid set_extranonce params");
        return;
    }
    bb_log_i(TAG, "set_extranonce: en1=%s en2_size=%d",
             s_state.extranonce1_hex, s_state.extranonce2_size);
}

// Handle mining.set_difficulty
static void handle_set_difficulty(bb_json_t params)
{
    if (!stratum_machine_handle_set_difficulty(&s_state, params)) {
        bb_log_w(TAG, "invalid set_difficulty params");
        return;
    }

    bb_log_i(TAG, "difficulty set to %.4f", s_state.difficulty);

    // Re-dispatch work with updated target — mark clean to invalidate
    // stale job table entries that carry the old (easier) target
    if (s_state.job.job_id[0] != '\0') {
        mining_work_t work;
        if (!build_work(&work)) return;
        work.clean = true;
        xQueueOverwrite(work_queue, &work);
    }
}

// Handle subscribe response
static int handle_subscribe_result(bb_json_t result)
{
    if (!stratum_machine_handle_subscribe_result(&s_state, result)) {
        bb_log_e(TAG, "invalid subscribe result");
        return -1;
    }

    bb_log_i(TAG, "subscribed: en1=%s en2_size=%d", s_state.extranonce1_hex, s_state.extranonce2_size);
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
    s_last_submit_us = esp_timer_get_time();
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
        if (strcmp(method_str, "mining.set_extranonce") == 0) {
            handle_set_extranonce(params);
        } else if (strcmp(method_str, "mining.notify") == 0) {
            handle_notify(params);
        } else if (strcmp(method_str, "mining.set_difficulty") == 0) {
            handle_set_difficulty(params);
        } else {
            bb_log_d(TAG, "unhandled method: %s", method_str);
        }
    } else if (id_item && bb_json_item_is_number(id_item)) {
        // Response to our request
        int id = bb_json_item_get_int(id_item);

        // Measure RTT for any response (TA-118)
        {
            int slot = id % STRATUM_INFLIGHT_MAX;
            int64_t send_us = s_inflight_send_us[slot];
            if (send_us > 0) {
                int64_t rtt_us = esp_timer_get_time() - send_us;
                if (rtt_us >= 0 && rtt_us < 30 * 1000 * 1000) {  // sanity: 0–30s
                    double rtt_ms = (double)rtt_us / 1000.0;
                    if (!s_pool_rtt_initialized) {
                        s_pool_rtt_ms_ema = rtt_ms;
                        s_pool_rtt_initialized = true;
                    } else {
                        // EMA: 7/8 old + 1/8 new
                        s_pool_rtt_ms_ema = (s_pool_rtt_ms_ema * 7.0 + rtt_ms) / 8.0;
                    }
                }
                // Clear slot to avoid stale re-use on collision
                s_inflight_send_us[slot] = 0;
            }
        }

        if (id == s_state.subscribe_id) {
            // Subscribe response
            if (result_item) {
                handle_subscribe_result(result_item);
            }
        } else if (id == s_state.authorize_id) {
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
        } else if (id == s_state.configure_id) {
            // Configure response (BIP 320 version rolling) — non-fatal
            if (result_item && bb_json_item_is_object(result_item)) {
                handle_configure_result(result_item);
            } else {
                bb_log_w(TAG, "pool does not support mining.configure, version rolling disabled");
            }
        } else if (s_state.extranonce_subscribe_id != 0 && id == s_state.extranonce_subscribe_id) {
            s_state.extranonce_subscribe_id = 0;
            if (result_item && bb_json_item_is_true(result_item)) {
                s_extranonce_sub_status = STRATUM_EXNX_SUB_ACTIVE;
                bb_log_i(TAG, "extranonce.subscribe: active");
            } else {
                s_extranonce_sub_status = STRATUM_EXNX_SUB_REJECTED;
                bb_log_w(TAG, "extranonce.subscribe: rejected");
            }
        } else if (s_state.keepalive_id != 0 && id == s_state.keepalive_id) {
            bb_log_d(TAG, "keepalive ack id=%d", id);
        } else {
            // Submit response or other
            if (error_item && !bb_json_item_is_null(error_item)) {
                char *err_str = bb_json_item_serialize(error_item);
                if (err_str) {
                    bb_log_e(TAG, "share rejected: %s", err_str);
                    bb_json_free_str(err_str);
                }
                int code = stratum_parse_error_code(error_item);
                stratum_reject_kind_t kind = stratum_machine_classify_reject(code);
                if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    mining_stats.session.rejected++;
                    switch (kind) {
                        case STRATUM_REJECT_JOB_NOT_FOUND:  mining_stats.session.rejected_job_not_found++;  break;
                        case STRATUM_REJECT_DUPLICATE:      mining_stats.session.rejected_duplicate++;      break;
                        case STRATUM_REJECT_LOW_DIFFICULTY: mining_stats.session.rejected_low_difficulty++; break;
                        case STRATUM_REJECT_STALE_PREVHASH: mining_stats.session.rejected_stale_prevhash++; break;
                        case STRATUM_REJECT_OTHER:
                        default:
                            mining_stats.session.rejected_other++;
                            mining_stats.session.rejected_other_last_code = code;
                            break;
                    }
                    xSemaphoreGive(mining_stats.mutex);
                }
            } else if (result_item && bb_json_item_is_true(result_item)) {
                bb_log_i(TAG, "share accepted");
                if (s_last_submit_us) {
                    bb_log_i(DIAG, "share ack: %lldms",
                             (esp_timer_get_time() - s_last_submit_us) / 1000);
                }
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
    char line[4096];

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
        {
            char cfg_params[256];
            int n = stratum_machine_build_configure(cfg_params, sizeof(cfg_params));
            if (n < 0) {
                bb_log_e(TAG, "configure params truncated");
                goto reconnect;
            }
            s_state.configure_id = stratum_request("mining.configure", cfg_params);
            if (s_state.configure_id < 0) {
                bb_log_w(TAG, "configure send failed, skipping version rolling");
                s_state.configure_id = 0;
            }
        }

        // Subscribe
        {
            char sub_params[64];
            int n = stratum_machine_build_subscribe(sub_params, sizeof(sub_params));
            if (n < 0) {
                bb_log_e(TAG, "subscribe params truncated");
                goto reconnect;
            }
            s_state.subscribe_id = stratum_request("mining.subscribe", sub_params);
        }
        if (s_state.subscribe_id < 0) {
            goto reconnect;
        }

        // Wait for subscribe response
        for (int i = 0; i < 50; i++) {  // 5s timeout
            int n = stratum_readline(line, sizeof(line), 100);
            if (n > 0) {
                process_message(line);
                if (s_state.extranonce1_len > 0) break;
            } else if (n < 0) {
                goto reconnect;
            }
        }

        if (s_state.extranonce1_len == 0) {
            bb_log_e(TAG, "subscribe timeout");
            goto reconnect;
        }

        // Authorize
        {
            char auth_params[256];
            int n = stratum_machine_build_authorize(auth_params, sizeof(auth_params),
                                                    s_wallet_addr, s_worker_name, pool_pass);
            if (n < 0) {
                bb_log_e(TAG, "auth params truncated");
                goto reconnect;
            }
            s_state.authorize_id = stratum_request("mining.authorize", auth_params);
            if (s_state.authorize_id < 0) {
                goto reconnect;
            }
        }

        /* TA-306: send mining.extranonce.subscribe if the active slot requests it */
        if (taipan_config_pool_extranonce_subscribe_idx(s_active_pool_idx)) {
            int sub_id = stratum_request("mining.extranonce.subscribe", "[]");
            if (sub_id >= 0) {
                s_state.extranonce_subscribe_id = sub_id;
                s_extranonce_sub_status = STRATUM_EXNX_SUB_PENDING;
            } else {
                bb_log_w(TAG, "extranonce.subscribe send failed, session continues");
                s_extranonce_sub_status = STRATUM_EXNX_SUB_REJECTED;
            }
        }

        // Wait for authorize response, set_difficulty, and initial notify
        bool job_received = false;
        for (int i = 0; i < 50; i++) {  // 5s timeout
            int n = stratum_readline(line, sizeof(line), 100);
            if (n > 0) {
                process_message(line);
                if (s_state.job.job_id[0] != '\0') {
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
                s_consecutive_fail_count++;
                if (s_consecutive_fail_count >= STRATUM_FAILOVER_THRESHOLD) {
                    int other_idx = (s_active_pool_idx == 0) ? 1 : 0;
                    if (taipan_config_pool_configured(other_idx)) {
                        bb_log_w(TAG, "failover: switching to pool slot %d after %d failures",
                                 other_idx, STRATUM_FAILOVER_THRESHOLD);
                        s_active_pool_idx = other_idx;
                        s_consecutive_fail_count = 0;
                    }
                }
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
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                uint32_t last_ms = pdTICKS_TO_MS(s_last_pool_job_tick);
                if (stratum_watchdog_job_drought(now_ms, last_ms)) {
                    bb_log_w(TAG, "no new job for 5 minutes, reconnecting");
                    break;
                }
            }

            // Reconnect if no share submitted in 30 minutes
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                uint32_t last_share_ms = pdTICKS_TO_MS(s_last_share_tick);
                uint32_t start_ms = pdTICKS_TO_MS(s_session_start_tick);
                if (stratum_watchdog_share_drought(now_ms, last_share_ms, start_ms)) {
                    bb_log_w(TAG, "no share submitted in 30 minutes, reconnecting");
                    break;
                }
            }

            // Periodic job dispatch — feed miner fresh nonce space (e.g. ASIC)
            if (g_miner_config.extranonce2_roll) {
                TickType_t now = xTaskGetTickCount();
                if (s_state.job.job_id[0] != '\0' &&
                    (now - s_last_job_tick) >= pdMS_TO_TICKS(g_miner_config.roll_interval_ms)) {
                    s_state.extranonce2++;
                    mining_work_t work;
                    if (!build_work(&work)) continue;
                    work.clean = false;
                    xQueueOverwrite(work_queue, &work);
                    s_last_job_tick = now;
                }
            }

            // App-level keepalive — keep NAT table alive on routers that ignore TCP keepalives
            {
                uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
                uint32_t last_tx_ms = pdTICKS_TO_MS(s_last_tx_tick);
                if (stratum_watchdog_needs_keepalive(now_ms, last_tx_ms)) {
                    bb_log_d(TAG, "sending keepalive ping");
                    char params[32];
                    int n = stratum_machine_build_keepalive(params, sizeof(params), s_state.difficulty);
                    if (n < 0) {
                        bb_log_e(TAG, "keepalive params truncated");
                        break;
                    }
                    int kid = stratum_request("mining.suggest_difficulty", params);
                    if (kid < 0) {
                        bb_log_w(TAG, "keepalive send failed, reconnecting");
                        break;
                    }
                    s_state.keepalive_id = kid;
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
        s_state.extranonce1_len = 0;
        s_state.subscribe_id = 0;
        s_state.authorize_id = 0;
        s_state.configure_id = 0;
        s_state.keepalive_id = 0;
        s_state.extranonce_subscribe_id = 0;
        s_extranonce_sub_status = STRATUM_EXNX_SUB_OFF;
        s_state.version_mask = 0;
        memset(&s_state.job, 0, sizeof(s_state.job));
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
