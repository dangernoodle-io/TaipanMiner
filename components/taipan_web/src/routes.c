#include "taipan_web.h"
#include "bb_http.h"
#include "bb_info.h"
#include "bb_json.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "bb_system.h"
#include "board.h"
#include "mining.h"
#include "routes_json.h"
#include "asic_drop_log.h"
#include "bb_nv.h"
#include "taipan_config.h"
#include "bb_wifi.h"
#include "bb_prov.h"
#include "bb_mdns.h"
#include "stratum.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <inttypes.h>
#include "bb_ota_pull.h"
#include "bb_ota_push.h"
#include "bb_ota_validator.h"
#include "bb_log.h"
#include "bb_board.h"
#include "knot.h"

static const char *TAG = "web";

// ============================================================================
// DEFERRED RESTART HELPERS
// ============================================================================

static void deferred_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    bb_system_restart();
    vTaskDelete(NULL);
}

static void schedule_deferred_restart(void)
{
    xTaskCreate(deferred_restart_task, "deferred_restart", 2048, NULL, 5, NULL);
}

extern const uint8_t prov_form_html_gz[];
extern const size_t prov_form_html_gz_len;
extern const uint8_t theme_css_gz[];
extern const size_t theme_css_gz_len;
extern const uint8_t index_html_gz[];
extern const size_t index_html_gz_len;
extern const uint8_t index_js_gz[];
extern const size_t index_js_gz_len;
extern const uint8_t index_css_gz[];
extern const size_t index_css_gz_len;
extern const uint8_t prov_save_html_gz[];
extern const size_t prov_save_html_gz_len;
extern const uint8_t logo_svg_gz[];
extern const size_t logo_svg_gz_len;
extern const uint8_t favicon_svg_gz[];
extern const size_t favicon_svg_gz_len;

static uint32_t s_wdt_resets = 0;

static void set_common_headers(bb_http_request_t *req)
{
    bb_http_resp_set_header(req, "Connection", "close");
    bb_http_resp_set_header(req, "Access-Control-Allow-Origin", "*");
    bb_http_resp_set_header(req, "Access-Control-Allow-Private-Network", "true");
}


// ============================================================================
// PROVISIONING SAVE CALLBACK
// ============================================================================

static bb_err_t taipan_prov_save_cb(bb_http_request_t *req, const char *body, int len)
{
    (void)len;
    char pool_host[64] = "", wallet[64] = "", worker[32] = "";
    char pool_pass[64] = "", hostname[33] = "";
    char port_str[8] = "";

    bb_url_decode_field(body, "pool_host", pool_host, sizeof(pool_host));
    bb_url_decode_field(body, "pool_port", port_str, sizeof(port_str));
    bb_url_decode_field(body, "wallet", wallet, sizeof(wallet));
    bb_url_decode_field(body, "worker", worker, sizeof(worker));
    bb_url_decode_field(body, "pool_pass", pool_pass, sizeof(pool_pass));
    bb_url_decode_field(body, "hostname", hostname, sizeof(hostname));

    if (pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        bb_http_resp_send_err(req, 400, "All fields required");
        return BB_ERR_INVALID_ARG;
    }
    uint16_t port = (uint16_t)strtoul(port_str, NULL, 10);
    if (port == 0) {
        bb_http_resp_send_err(req, 400, "Valid port required");
        return BB_ERR_INVALID_ARG;
    }

    // Derive hostname from worker if not provided
    if (hostname[0] == '\0') {
        bb_mdns_build_hostname(worker, NULL, hostname, sizeof(hostname));
    }

    // Validate and save hostname
    if (taipan_config_set_hostname(hostname) != BB_OK) {
        bb_http_resp_send_err(req, 400, "Invalid hostname");
        return BB_ERR_INVALID_ARG;
    }

    if (taipan_config_set_pool(pool_host, port, wallet, worker, pool_pass) != BB_OK) {
        bb_http_resp_send_err(req, 500, "Failed to save config");
        return BB_ERR_INVALID_ARG;
    }

    bb_http_resp_set_header(req, "Connection", "close");
    bb_http_resp_set_header(req, "Access-Control-Allow-Origin", "*");
    bb_http_resp_set_header(req, "Access-Control-Allow-Private-Network", "true");
    bb_http_resp_set_header(req, "Content-Encoding", "gzip");
    bb_http_resp_set_header(req, "Content-Type", "text/html");
    bb_http_resp_send(req, (const char *)prov_save_html_gz, prov_save_html_gz_len);

    // schedule deferred restart so config changes apply
    schedule_deferred_restart();
    return BB_OK;
}

// ============================================================================
// PORTABLE HANDLERS (migrated to bb_http API)
// ============================================================================

static bb_err_t stats_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    stats_snapshot_t s = {0};
    s.session_rejected_other_last_code = -1;
#ifdef ASIC_CHIP
    s.asic_freq_cfg = -1.0f;
    s.asic_freq_eff = -1.0f;
#endif

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s.hw_rate    = mining_stats.hw_hashrate;
        s.hw_ema     = mining_stats.hw_ema.value;
        s.hw_shares  = mining_stats.hw_shares;
        s.temp_c     = mining_stats.temp_c;
        s.session_shares                   = mining_stats.session.shares;
        s.session_rejected                 = mining_stats.session.rejected;
        s.session_rejected_job_not_found   = mining_stats.session.rejected_job_not_found;
        s.session_rejected_low_difficulty  = mining_stats.session.rejected_low_difficulty;
        s.session_rejected_duplicate       = mining_stats.session.rejected_duplicate;
        s.session_rejected_stale_prevhash  = mining_stats.session.rejected_stale_prevhash;
        s.session_rejected_other           = mining_stats.session.rejected_other;
        s.session_rejected_other_last_code = mining_stats.session.rejected_other_last_code;
        s.last_share_us    = mining_stats.session.last_share_us;
        s.session_start_us = mining_stats.session.start_us;
        s.best_diff        = mining_stats.session.best_diff;
        s.lifetime_shares  = mining_stats.lifetime.total_shares;
#ifdef ASIC_CHIP
        s.asic_rate         = mining_stats.asic_hashrate;
        s.asic_ema          = mining_stats.asic_ema.value;
        s.asic_shares       = mining_stats.asic_shares;
        s.asic_temp_c       = mining_stats.asic_temp_c;
        s.asic_freq_cfg     = mining_stats.asic_freq_configured_mhz;
        s.asic_freq_eff     = mining_stats.asic_freq_effective_mhz;
        s.asic_total_ghs    = mining_stats.asic_total_ghs;
        s.asic_hw_error_pct = mining_stats.asic_hw_error_pct;
        s.asic_total_ghs_1m       = mining_stats.asic_total_ghs_1m;
        s.asic_total_ghs_10m      = mining_stats.asic_total_ghs_10m;
        s.asic_total_ghs_1h       = mining_stats.asic_total_ghs_1h;
        s.asic_hw_error_pct_1m    = mining_stats.asic_hw_error_pct_1m;
        s.asic_hw_error_pct_10m   = mining_stats.asic_hw_error_pct_10m;
        s.asic_hw_error_pct_1h    = mining_stats.asic_hw_error_pct_1h;
        s.asic_total_valid  = (s.asic_total_ghs > 0.001f);
        s.asic_small_cores  = BOARD_SMALL_CORES;
        s.asic_count        = BOARD_ASIC_COUNT;
#endif
        xSemaphoreGive(mining_stats.mutex);
    }

    s.now_us = esp_timer_get_time();

#ifdef ASIC_CHIP
    /* Per-chip telemetry (TA-192 phase 2) — collected after mutex released */
    asic_chip_telemetry_t chip_tel[BOARD_ASIC_COUNT];
    int n_chips = asic_task_get_chip_telemetry(chip_tel, BOARD_ASIC_COUNT);
    s.n_chips = (n_chips <= ROUTES_JSON_MAX_CHIPS) ? n_chips : ROUTES_JSON_MAX_CHIPS;
    for (int c = 0; c < s.n_chips; c++) {
        s.chips[c].total_ghs   = chip_tel[c].total_ghs;
        s.chips[c].error_ghs   = chip_tel[c].error_ghs;
        s.chips[c].hw_err_pct  = chip_tel[c].hw_err_pct;
        s.chips[c].total_raw   = chip_tel[c].total_raw;
        s.chips[c].error_raw   = chip_tel[c].error_raw;
        s.chips[c].total_drops = chip_tel[c].total_drops;
        s.chips[c].error_drops = chip_tel[c].error_drops;
        s.chips[c].last_drop_us = chip_tel[c].last_drop_us;
        for (int d = 0; d < 4; d++) {
            s.chips[c].domain_ghs[d]   = chip_tel[c].domain_ghs[d];
            s.chips[c].domain_drops[d] = chip_tel[c].domain_drops[d];
        }
    }
    /* Re-read now_us after chip telemetry fetch for accurate last_drop_ago_s */
    s.now_us = (int64_t)(uint64_t)esp_timer_get_time();
#endif

    bb_json_t root = bb_json_obj_new();
    build_stats_json(&s, root);
    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

// ----------------------------------------------------------------------------
// /api/pool — TA-281/TA-286
// Locked shape: pool config (always populated from taipan_config_*) +
// session-scoped negotiated values (extranonce1, extranonce2_size,
// version_mask) + most-recent stratum mining.notify exposed as a `notify`
// sub-object. Pre-stratum-connect, connected=false, session_start_ago_s/
// extranonce*/version_mask/notify are all null; current_difficulty defaults
// to stratum_state.difficulty (512.0).
// ----------------------------------------------------------------------------
static bb_err_t pool_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    pool_snapshot_t s = {0};

    // Pool config — always populated from NVS-backed accessors.
    {
        const char *h = taipan_config_pool_host();
        if (h) strncpy(s.host, h, sizeof(s.host) - 1);
        const char *wk = taipan_config_worker_name();
        if (wk) strncpy(s.worker, wk, sizeof(s.worker) - 1);
        const char *wa = taipan_config_wallet_addr();
        if (wa) strncpy(s.wallet, wa, sizeof(s.wallet) - 1);
    }
    s.port      = taipan_config_pool_port();
    s.connected = stratum_is_connected();

    // session_start_ago_s — null pre-connect; wrap-safe diff in ms then /1000.
    uint32_t start_ms = stratum_get_session_start_ms();
    if (start_ms != 0) {
        uint32_t now_ms   = pdTICKS_TO_MS(xTaskGetTickCount());
        uint32_t delta_ms = now_ms - start_ms;  // unsigned wrap-safe
        s.session_start_ago_s = delta_ms / 1000U;
        s.has_session_start   = true;
    }

    s.current_difficulty = stratum_get_difficulty();

    // Negotiated session params — null until subscribe response received.
    stratum_session_snapshot_t sess;
    if (stratum_get_session_snapshot(&sess) && sess.extranonce1_len > 0) {
        size_t copy_len = sess.extranonce1_len;
        if (copy_len > ROUTES_JSON_EXTRANONCE1_MAX)
            copy_len = ROUTES_JSON_EXTRANONCE1_MAX;
        memcpy(s.extranonce1, sess.extranonce1, copy_len);
        s.extranonce1_len  = copy_len;
        s.extranonce2_size = sess.extranonce2_size;
        s.version_mask     = sess.version_mask;
    }

    // notify sub-object — most recent mining.notify (TA-201).
    const stratum_job_t *job = NULL;
    if (stratum_get_job_snapshot(&job) && job) {
        s.has_notify = true;
        strncpy(s.job_id, job->job_id, sizeof(s.job_id) - 1);
        memcpy(s.prevhash, job->prevhash, 32);

        size_t cb1 = job->coinb1_len < ROUTES_JSON_MAX_COINB ? job->coinb1_len : ROUTES_JSON_MAX_COINB;
        memcpy(s.coinb1, job->coinb1, cb1);
        s.coinb1_len = cb1;

        size_t cb2 = job->coinb2_len < ROUTES_JSON_MAX_COINB ? job->coinb2_len : ROUTES_JSON_MAX_COINB;
        memcpy(s.coinb2, job->coinb2, cb2);
        s.coinb2_len = cb2;

        size_t mc = job->merkle_count < ROUTES_JSON_MAX_MERKLE ? job->merkle_count : ROUTES_JSON_MAX_MERKLE;
        for (size_t i = 0; i < mc; i++)
            memcpy(s.merkle_branches[i], job->merkle_branches[i], 32);
        s.merkle_count = mc;

        s.version    = job->version;
        s.nbits      = job->nbits;
        s.ntime      = job->ntime;
        s.clean_jobs = job->clean_jobs;
    }

    bb_json_t root = bb_json_obj_new();
    build_pool_json(&s, root);
    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

#ifdef ASIC_CHIP
static bb_err_t power_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    power_snapshot_t s = {
        .vcore_mv      = -1,
        .icore_ma      = -1,
        .pcore_mw      = -1,
        .vin_mv        = -1,
        .asic_hashrate = 0,
        .board_temp_c  = -1.0f,
        .vr_temp_c     = -1.0f,
        .nominal_vin_mv = BOARD_NOMINAL_VIN_MV,
    };

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s.vcore_mv      = mining_stats.vcore_mv;
        s.icore_ma      = mining_stats.icore_ma;
        s.pcore_mw      = mining_stats.pcore_mw;
        s.vin_mv        = mining_stats.vin_mv;
        s.asic_hashrate = mining_stats.asic_hashrate;
        s.board_temp_c  = mining_stats.board_temp_c;
        s.vr_temp_c     = mining_stats.vr_temp_c;
        xSemaphoreGive(mining_stats.mutex);
    }

    bb_json_t root = bb_json_obj_new();
    build_power_json(&s, root);
    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

static bb_err_t fan_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    fan_snapshot_t s = { .fan_rpm = -1, .fan_duty_pct = -1 };

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s.fan_rpm      = mining_stats.fan_rpm;
        s.fan_duty_pct = mining_stats.fan_duty_pct;
        xSemaphoreGive(mining_stats.mutex);
    }

    bb_json_t root = bb_json_obj_new();
    build_fan_json(&s, root);
    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}
#endif // ASIC_BM1370 || ASIC_BM1368

static bb_err_t knot_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    /* Off-stack: 32 * sizeof(knot_peer_t) ≈ 9 KB blows the httpd task stack.
     * Heap-allocate the snapshot buffer; httpd serializes per-handler so a
     * static would also be safe, but heap keeps the lifetime explicit. */
    knot_peer_t *peers = malloc(sizeof(knot_peer_t) * ROUTES_JSON_MAX_PEERS);
    if (!peers) {
        bb_http_resp_send_err(req, 500, "out of memory");
        return BB_ERR_INVALID_ARG;
    }
    size_t peer_count = knot_snapshot(peers, ROUTES_JSON_MAX_PEERS);

    knot_snapshot_t s = {0};
    s.now_us  = esp_timer_get_time();
    s.n_peers = peer_count < ROUTES_JSON_MAX_PEERS ? peer_count : ROUTES_JSON_MAX_PEERS;
    for (size_t i = 0; i < s.n_peers; i++) {
        strncpy(s.peers[i].instance, peers[i].instance_name, sizeof(s.peers[i].instance) - 1);
        strncpy(s.peers[i].hostname, peers[i].hostname,      sizeof(s.peers[i].hostname)  - 1);
        strncpy(s.peers[i].ip,       peers[i].ip4,           sizeof(s.peers[i].ip)        - 1);
        strncpy(s.peers[i].worker,   peers[i].worker,        sizeof(s.peers[i].worker)    - 1);
        strncpy(s.peers[i].board,    peers[i].board,         sizeof(s.peers[i].board)     - 1);
        strncpy(s.peers[i].version,  peers[i].version,       sizeof(s.peers[i].version)   - 1);
        strncpy(s.peers[i].state,    peers[i].state,         sizeof(s.peers[i].state)     - 1);
        s.peers[i].last_seen_us = peers[i].last_seen_us;
    }
    free(peers);

    bb_json_t root = bb_json_arr_new();
    build_knot_json(&s, root);
    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

static void taipan_info_extender(bb_json_t root)
{
    bb_json_obj_set_string(root, "worker_name", taipan_config_worker_name());
    bb_json_obj_set_string(root, "hostname", taipan_config_hostname());

    const char *ssid = bb_nv_config_wifi_ssid();
    if (ssid) bb_json_obj_set_string(root, "ssid", ssid);

    bb_json_obj_set_bool(root, "validated", !bb_ota_is_pending());
    bb_json_obj_set_number(root, "wdt_resets", s_wdt_resets);

    time_t now = time(NULL);
    if (now > 1700000000) {
        int64_t uptime_s = esp_timer_get_time() / 1000000LL;
        bb_json_obj_set_number(root, "boot_time", (double)(now - uptime_s));
    }

    bb_json_t network = bb_json_obj_get_item(root, "network");
    if (network) {
        bb_json_obj_set_bool(network, "mdns", bb_mdns_started());
        bb_json_obj_set_bool(network, "stratum", stratum_is_connected());
        bb_json_obj_set_number(network, "stratum_reconnect_ms", stratum_get_reconnect_delay_ms());
        bb_json_obj_set_number(network, "stratum_fail_count", stratum_get_connect_fail_count());
    }
}

bb_err_t taipan_web_register_info_extender(void)
{
    return bb_info_register_extender(taipan_info_extender);
}

static bb_err_t settings_get_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    settings_snapshot_t s = {0};
    {
        const char *h = taipan_config_pool_host();
        if (h) strncpy(s.pool_host, h, sizeof(s.pool_host) - 1);
        const char *wa = taipan_config_wallet_addr();
        if (wa) strncpy(s.wallet, wa, sizeof(s.wallet) - 1);
        const char *wk = taipan_config_worker_name();
        if (wk) strncpy(s.worker, wk, sizeof(s.worker) - 1);
        const char *pp = taipan_config_pool_pass();
        if (pp) strncpy(s.pool_pass, pp, sizeof(s.pool_pass) - 1);
        const char *hn = taipan_config_hostname();
        if (hn) strncpy(s.hostname, hn, sizeof(s.hostname) - 1);
    }
    s.pool_port      = taipan_config_pool_port();
    s.display_en     = bb_nv_config_display_enabled();
    s.ota_skip_check = bb_nv_config_ota_skip_check();

    bb_json_t root = bb_json_obj_new();
    build_settings_json(&s, root);
    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

// Shared helper for POST (full) and PATCH (partial) settings
// Returns BB_OK on successful response send; sets *out_reboot_required to true if restart needed
static bb_err_t apply_settings(bb_http_request_t *req, bool partial, bool *out_reboot_required)
{
    *out_reboot_required = false;

    set_common_headers(req);
    char body[512];

    int body_len = bb_http_req_body_len(req);
    if (body_len > sizeof(body) - 1) {
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "Body too large";
        return bb_http_resp_send(req, msg, strlen(msg));
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "Empty body";
        return bb_http_resp_send(req, msg, strlen(msg));
    }
    body[len] = '\0';

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) {
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "Invalid JSON";
        return bb_http_resp_send(req, msg, strlen(msg));
    }

    // Extract fields — use current values as defaults for PATCH
    const char *pool_host = taipan_config_pool_host();
    uint16_t pool_port = taipan_config_pool_port();
    const char *wallet = taipan_config_wallet_addr();
    const char *worker = taipan_config_worker_name();
    const char *pool_pass = taipan_config_pool_pass();
    bool reboot_required = false;

    bb_json_t j;

    j = bb_json_obj_get_item(root, "pool_host");
    if (j && bb_json_item_is_string(j)) { pool_host = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "pool_host required", 18); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "pool_port");
    if (j && bb_json_item_is_number(j)) { pool_port = (uint16_t)bb_json_item_get_double(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "pool_port required", 18); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "wallet");
    if (j && bb_json_item_is_string(j)) { wallet = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "wallet required", 15); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "worker");
    if (j && bb_json_item_is_string(j)) { worker = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "worker required", 15); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "pool_pass");
    if (j && bb_json_item_is_string(j)) { pool_pass = bb_json_item_get_string(j); }
    // pool_pass is optional even for POST

    // Compare against current values to determine if reboot is needed
    if (strcmp(pool_host, taipan_config_pool_host()) != 0 ||
        pool_port != taipan_config_pool_port() ||
        strcmp(wallet, taipan_config_wallet_addr()) != 0 ||
        strcmp(worker, taipan_config_worker_name()) != 0 ||
        strcmp(pool_pass, taipan_config_pool_pass()) != 0) {
        reboot_required = true;
    }

    // Validate
    if (pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "pool_host, wallet, worker must not be empty";
        return bb_http_resp_send(req, msg, strlen(msg));
    }
    if (pool_port == 0) {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "pool_port must be > 0";
        return bb_http_resp_send(req, msg, strlen(msg));
    }

    // Save mining config if any mining field was provided
    if (reboot_required) {
        bb_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != BB_OK) {
            bb_json_free(root);
            bb_http_resp_set_status(req, 500);
            bb_http_resp_set_header(req, "Content-Type", "text/plain");
            static const char *msg = "Failed to save config";
            return bb_http_resp_send(req, msg, strlen(msg));
        }
    }

    // Handle display_en separately (takes effect immediately, no reboot needed)
    {
        bool display_val = false;
        if (bb_json_obj_get_bool(root, "display_en", &display_val)) {
            bb_err_t err = bb_nv_config_set_display_enabled(display_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_set_status(req, 500);
                bb_http_resp_set_header(req, "Content-Type", "text/plain");
                static const char *msg = "Failed to save display setting";
                return bb_http_resp_send(req, msg, strlen(msg));
            }
        }
    }

    {
        bool skip_val = false;
        if (bb_json_obj_get_bool(root, "ota_skip_check", &skip_val)) {
            bb_err_t err = bb_nv_config_set_ota_skip_check(skip_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_set_status(req, 500);
                bb_http_resp_set_header(req, "Content-Type", "text/plain");
                static const char *msg = "Failed to save ota_skip_check";
                return bb_http_resp_send(req, msg, strlen(msg));
            }
        }
    }

    bb_json_free(root);

    // Response
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"saved\",\"reboot_required\":%s}",
             reboot_required ? "true" : "false");
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t send_rc = bb_http_resp_send(req, resp, strlen(resp));

    if (send_rc == BB_OK) {
        *out_reboot_required = reboot_required;
    }

    return send_rc;
}

static bb_err_t settings_post_handler(bb_http_request_t *req)
{
    bool reboot_required = false;
    bb_err_t rc = apply_settings(req, false, &reboot_required);

    if (rc == BB_OK && reboot_required) {
        schedule_deferred_restart();
    }

    return rc;
}

// ============================================================================
// PATCH HANDLER
// ============================================================================

static bb_err_t settings_patch_handler(bb_http_request_t *req)
{
    // Inline the apply_settings logic with bb_http_* API
    set_common_headers(req);
    char body[512];

    int body_len = bb_http_req_body_len(req);
    if (body_len > sizeof(body) - 1) {
        bb_http_resp_send_err(req, 400, "Body too large");
        return BB_ERR_INVALID_ARG;
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_send_err(req, 400, "Empty body");
        return BB_ERR_INVALID_ARG;
    }
    body[len] = '\0';

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) {
        bb_http_resp_send_err(req, 400, "Invalid JSON");
        return BB_ERR_INVALID_ARG;
    }

    // Extract fields — use current values as defaults for PATCH
    const char *pool_host = taipan_config_pool_host();
    uint16_t pool_port = taipan_config_pool_port();
    const char *wallet = taipan_config_wallet_addr();
    const char *worker = taipan_config_worker_name();
    const char *pool_pass = taipan_config_pool_pass();
    const char *hostname = taipan_config_hostname();
    bool reboot_required = false;

    bb_json_t j;

    j = bb_json_obj_get_item(root, "pool_host");
    if (j && bb_json_item_is_string(j)) { pool_host = bb_json_item_get_string(j); }

    j = bb_json_obj_get_item(root, "pool_port");
    if (j && bb_json_item_is_number(j)) { pool_port = (uint16_t)bb_json_item_get_double(j); }

    j = bb_json_obj_get_item(root, "wallet");
    if (j && bb_json_item_is_string(j)) { wallet = bb_json_item_get_string(j); }

    j = bb_json_obj_get_item(root, "worker");
    if (j && bb_json_item_is_string(j)) { worker = bb_json_item_get_string(j); }

    j = bb_json_obj_get_item(root, "pool_pass");
    if (j && bb_json_item_is_string(j)) { pool_pass = bb_json_item_get_string(j); }

    j = bb_json_obj_get_item(root, "hostname");
    if (j && bb_json_item_is_string(j)) { hostname = bb_json_item_get_string(j); }

    // Check if hostname was provided and validate it
    bool hostname_changed = false;
    if (bb_json_obj_get_item(root, "hostname")) {
        if (strcmp(hostname, taipan_config_hostname()) != 0) {
            hostname_changed = true;
            if (hostname[0] != '\0' && taipan_config_set_hostname(hostname) != BB_OK) {
                bb_json_free(root);
                bb_http_resp_send_err(req, 400, "invalid hostname");
                return BB_ERR_INVALID_ARG;
            }
        }
    }

    // Compare against current values to determine if reboot is needed
    if (strcmp(pool_host, taipan_config_pool_host()) != 0 ||
        pool_port != taipan_config_pool_port() ||
        strcmp(wallet, taipan_config_wallet_addr()) != 0 ||
        strcmp(worker, taipan_config_worker_name()) != 0 ||
        strcmp(pool_pass, taipan_config_pool_pass()) != 0 ||
        hostname_changed) {
        reboot_required = true;
    }

    // Validate
    if (pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        bb_json_free(root);
        bb_http_resp_send_err(req, 400, "pool_host, wallet, worker must not be empty");
        return BB_ERR_INVALID_ARG;
    }
    if (pool_port == 0) {
        bb_json_free(root);
        bb_http_resp_send_err(req, 400, "pool_port must be > 0");
        return BB_ERR_INVALID_ARG;
    }

    // Save mining config if any mining field was provided
    if (reboot_required && !hostname_changed) {
        bb_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != BB_OK) {
            bb_json_free(root);
            bb_http_resp_send_err(req, 500, "Failed to save config");
            return BB_ERR_INVALID_ARG;
        }
    } else if (reboot_required && hostname_changed) {
        bb_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != BB_OK) {
            bb_json_free(root);
            bb_http_resp_send_err(req, 500, "Failed to save config");
            return BB_ERR_INVALID_ARG;
        }
    }

    // Handle display_en separately (takes effect immediately, no reboot needed)
    {
        bool display_val = false;
        if (bb_json_obj_get_bool(root, "display_en", &display_val)) {
            bb_err_t err = bb_nv_config_set_display_enabled(display_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_send_err(req, 500, "Failed to save display setting");
                return BB_ERR_INVALID_ARG;
            }
        }
    }

    {
        bool skip_val = false;
        if (bb_json_obj_get_bool(root, "ota_skip_check", &skip_val)) {
            bb_err_t err = bb_nv_config_set_ota_skip_check(skip_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_send_err(req, 500, "Failed to save ota_skip_check");
                return BB_ERR_INVALID_ARG;
            }
        }
    }

    bb_json_free(root);

    // Response
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"saved\",\"reboot_required\":%s}",
             reboot_required ? "true" : "false");
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t send_rc = bb_http_resp_send(req, resp, strlen(resp));

    // schedule deferred restart if reboot is needed
    if (reboot_required) {
        schedule_deferred_restart();
    }

    return send_rc;
}

// ============================================================================
// ROUTE DESCRIPTORS
// ============================================================================

// ---------------------------------------------------------------------------
// /api/stats — GET
// ---------------------------------------------------------------------------

static const bb_route_response_t s_stats_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"hashrate\":{\"type\":\"number\",\"description\":\"ESP32-S3 HW hashrate H/s\"},"
      "\"hashrate_avg\":{\"type\":\"number\",\"description\":\"EMA hashrate H/s\"},"
      "\"temp_c\":{\"type\":\"number\"},"
      "\"shares\":{\"type\":\"integer\",\"description\":\"HW shares found\"},"
      "\"session_shares\":{\"type\":\"integer\"},"
      "\"session_rejected\":{\"type\":\"integer\"},"
      "\"rejected\":{\"type\":\"object\","
      "\"properties\":{"
      "\"total\":{\"type\":\"integer\"},"
      "\"job_not_found\":{\"type\":\"integer\"},"
      "\"low_difficulty\":{\"type\":\"integer\"},"
      "\"duplicate\":{\"type\":\"integer\"},"
      "\"stale_prevhash\":{\"type\":\"integer\"},"
      "\"other\":{\"type\":\"integer\"},"
      "\"other_last_code\":{\"type\":\"integer\"}}},"
      "\"last_share_ago_s\":{\"type\":\"integer\",\"description\":\"-1 if no share yet\"},"
      "\"lifetime_shares\":{\"type\":\"integer\"},"
      "\"best_diff\":{\"type\":\"number\"},"
      "\"uptime_s\":{\"type\":\"integer\"},"
      "\"expected_ghs\":{\"type\":\"number\","
      "\"description\":\"theoretical max GH/s for this platform\"},"
      "\"asic_chips\":{\"type\":\"array\","
      "\"items\":{\"type\":\"object\","
      "\"properties\":{"
      "\"idx\":{\"type\":\"integer\"},"
      "\"total_ghs\":{\"type\":\"number\"},"
      "\"error_ghs\":{\"type\":\"number\"},"
      "\"hw_err_pct\":{\"type\":\"number\"},"
      "\"total_raw\":{\"type\":\"integer\"},"
      "\"error_raw\":{\"type\":\"integer\"},"
      "\"total_drops\":{\"type\":\"integer\"},"
      "\"error_drops\":{\"type\":\"integer\"},"
      "\"last_drop_ago_s\":{\"type\":[\"number\",\"null\"]},"
      "\"domain_ghs\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}},"
      "\"domain_drops\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}}}}}}}",
      "mining statistics snapshot" },
    { 0 },
};

static const bb_route_t s_stats_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/stats",
    .tag          = "mining",
    .summary      = "Get mining statistics",
    .operation_id = "getStats",
    .responses    = s_stats_responses,
    .handler      = stats_handler,
};

// ---------------------------------------------------------------------------
// /api/pool — GET (TA-281, TA-286; closes TA-201)
// ---------------------------------------------------------------------------

static const bb_route_response_t s_pool_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"host\":{\"type\":\"string\"},"
      "\"port\":{\"type\":\"integer\"},"
      "\"worker\":{\"type\":\"string\"},"
      "\"wallet\":{\"type\":\"string\"},"
      "\"connected\":{\"type\":\"boolean\"},"
      "\"session_start_ago_s\":{\"type\":[\"integer\",\"null\"]},"
      "\"current_difficulty\":{\"type\":\"number\"},"
      "\"extranonce1\":{\"type\":[\"string\",\"null\"]},"
      "\"extranonce2_size\":{\"type\":[\"integer\",\"null\"]},"
      "\"version_mask\":{\"type\":[\"string\",\"null\"],"
      "\"description\":\"BIP320 mask, 8-char lowercase hex; null if not negotiated\"},"
      "\"notify\":{\"type\":[\"object\",\"null\"],"
      "\"properties\":{"
      "\"job_id\":{\"type\":\"string\"},"
      "\"prev_hash\":{\"type\":\"string\"},"
      "\"coinb1\":{\"type\":\"string\"},"
      "\"coinb2\":{\"type\":\"string\"},"
      "\"merkle_branches\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
      "\"version\":{\"type\":\"string\"},"
      "\"nbits\":{\"type\":\"string\"},"
      "\"ntime\":{\"type\":\"string\"},"
      "\"clean_jobs\":{\"type\":\"boolean\"}}}},"
      "\"required\":[\"host\",\"port\",\"worker\",\"wallet\",\"connected\","
      "\"session_start_ago_s\",\"current_difficulty\","
      "\"extranonce1\",\"extranonce2_size\",\"version_mask\",\"notify\"]}",
      "pool connection state and current job" },
    { 0 },
};

static const bb_route_t s_pool_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/pool",
    .tag          = "pool",
    .summary      = "Get pool connection state and current job",
    .operation_id = "getPool",
    .responses    = s_pool_responses,
    .handler      = pool_handler,
};

// ---------------------------------------------------------------------------
// /api/diag/asic — GET (TA-282, TA-287)
// Recent telemetry-drop log. On ASIC boards, calls asic_task_get_drop_log()
// and serialises up to ASIC_DROP_LOG_CAP entries as recent_drops[].
// On tdongle (no ASIC), returns { "recent_drops": [] } — keeps the webui
// path uniform with no special-casing.
// ---------------------------------------------------------------------------
static bb_err_t diag_asic_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    diag_asic_snapshot_t s = {0};

#ifdef ASIC_CHIP
    asic_drop_event_t drops[ASIC_DROP_LOG_CAP];
    size_t n = asic_task_get_drop_log(drops, ASIC_DROP_LOG_CAP);
    s.now_us  = (uint64_t)esp_timer_get_time();
    s.n_drops = n < ROUTES_JSON_DROP_LOG_CAP ? n : ROUTES_JSON_DROP_LOG_CAP;
    for (size_t i = 0; i < s.n_drops; i++) {
        s.drops[i].ts_us      = drops[i].ts_us;
        s.drops[i].chip_idx   = drops[i].chip_idx;
        s.drops[i].kind       = drops[i].kind;
        s.drops[i].domain_idx = drops[i].domain_idx;
        s.drops[i].asic_addr  = drops[i].asic_addr;
        s.drops[i].ghs        = drops[i].ghs;
        s.drops[i].delta      = drops[i].delta;
        s.drops[i].elapsed_s  = drops[i].elapsed_s;
    }
#endif /* ASIC_CHIP */

    bb_json_t root = bb_json_obj_new();
    build_diag_asic_json(&s, root);
    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

static const bb_route_response_t s_diag_asic_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"recent_drops\":{\"type\":\"array\","
      "\"items\":{\"type\":\"object\","
      "\"properties\":{"
      "\"ts_ago_s\":{\"type\":\"number\",\"description\":\"seconds since drop event\"},"
      "\"chip\":{\"type\":\"integer\"},"
      "\"kind\":{\"type\":\"string\",\"enum\":[\"total\",\"error\",\"domain\"]},"
      "\"domain\":{\"type\":\"integer\"},"
      "\"addr\":{\"type\":\"integer\"},"
      "\"ghs\":{\"type\":\"number\"},"
      "\"delta\":{\"type\":\"integer\"},"
      "\"elapsed_s\":{\"type\":\"number\"}},"
      "\"required\":[\"ts_ago_s\",\"chip\",\"kind\",\"domain\","
      "\"addr\",\"ghs\",\"delta\",\"elapsed_s\"]}}}}",
      "recent ASIC telemetry drop log" },
    { 0 },
};

static const bb_route_t s_diag_asic_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/diag/asic",
    .tag          = "diag",
    .summary      = "Get recent ASIC telemetry drop log",
    .operation_id = "getDiagAsic",
    .responses    = s_diag_asic_responses,
    .handler      = diag_asic_handler,
};

// ---------------------------------------------------------------------------
// /api/knot — GET
// ---------------------------------------------------------------------------

static const bb_route_response_t s_knot_responses[] = {
    { 200, "application/json",
      "{\"type\":\"array\","
      "\"items\":{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"instance\":{\"type\":\"string\"},"
      "\"hostname\":{\"type\":\"string\"},"
      "\"ip\":{\"type\":\"string\"},"
      "\"worker\":{\"type\":\"string\"},"
      "\"board\":{\"type\":\"string\"},"
      "\"version\":{\"type\":\"string\"},"
      "\"state\":{\"type\":\"string\"},"
      "\"seen_ago_s\":{\"type\":\"integer\"}},"
      "\"required\":[\"instance\",\"hostname\",\"ip\","
      "\"worker\",\"board\",\"version\",\"state\",\"seen_ago_s\"]}}",
      "mDNS-discovered TaipanMiner peer table snapshot" },
    { 0 },
};

static const bb_route_t s_knot_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/knot",
    .tag          = "knot",
    .summary      = "Get peer table snapshot",
    .operation_id = "getKnot",
    .responses    = s_knot_responses,
    .handler      = knot_handler,
};

// ---------------------------------------------------------------------------
// /api/settings — GET, POST, PATCH
// ---------------------------------------------------------------------------

static const bb_route_response_t s_settings_get_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"pool_host\":{\"type\":\"string\"},"
      "\"pool_port\":{\"type\":\"integer\"},"
      "\"wallet\":{\"type\":\"string\"},"
      "\"worker\":{\"type\":\"string\"},"
      "\"pool_pass\":{\"type\":\"string\"},"
      "\"hostname\":{\"type\":\"string\"},"
      "\"display_en\":{\"type\":\"boolean\"},"
      "\"ota_skip_check\":{\"type\":\"boolean\"}},"
      "\"required\":[\"pool_host\",\"pool_port\",\"wallet\",\"worker\","
      "\"pool_pass\",\"hostname\",\"display_en\",\"ota_skip_check\"]}",
      "current persisted settings" },
    { 0 },
};

static const bb_route_t s_settings_get_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/settings",
    .tag          = "config",
    .summary      = "Get current settings",
    .operation_id = "getSettings",
    .responses    = s_settings_get_responses,
    .handler      = settings_get_handler,
};

static const bb_route_response_t s_settings_write_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"status\":{\"type\":\"string\",\"enum\":[\"saved\"]},"
      "\"reboot_required\":{\"type\":\"boolean\"}},"
      "\"required\":[\"status\",\"reboot_required\"]}",
      "settings saved" },
    { 400, "text/plain", NULL, "validation error" },
    { 500, "text/plain", NULL, "save failed" },
    { 0 },
};

static const bb_route_t s_settings_post_route = {
    .method               = BB_HTTP_POST,
    .path                 = "/api/settings",
    .tag                  = "config",
    .summary              = "Replace settings (full update)",
    .operation_id         = "postSettings",
    .request_content_type = "application/json",
    .request_schema       =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"pool_host\":{\"type\":\"string\"},"
        "\"pool_port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535},"
        "\"wallet\":{\"type\":\"string\"},"
        "\"worker\":{\"type\":\"string\"},"
        "\"pool_pass\":{\"type\":\"string\"},"
        "\"display_en\":{\"type\":\"boolean\"},"
        "\"ota_skip_check\":{\"type\":\"boolean\"}},"
        "\"required\":[\"pool_host\",\"pool_port\",\"wallet\",\"worker\"]}",
    .responses            = s_settings_write_responses,
    .handler              = settings_post_handler,
};

static const bb_route_t s_settings_patch_route = {
    .method               = BB_HTTP_PATCH,
    .path                 = "/api/settings",
    .tag                  = "config",
    .summary              = "Partial settings update",
    .operation_id         = "patchSettings",
    .request_content_type = "application/json",
    .request_schema       =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"pool_host\":{\"type\":\"string\"},"
        "\"pool_port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535},"
        "\"wallet\":{\"type\":\"string\"},"
        "\"worker\":{\"type\":\"string\"},"
        "\"pool_pass\":{\"type\":\"string\"},"
        "\"hostname\":{\"type\":\"string\"},"
        "\"display_en\":{\"type\":\"boolean\"},"
        "\"ota_skip_check\":{\"type\":\"boolean\"}}}",
    .responses            = s_settings_write_responses,
    .handler              = settings_patch_handler,
};

#ifdef ASIC_CHIP

// ---------------------------------------------------------------------------
// /api/power — GET (ASIC boards only)
// ---------------------------------------------------------------------------

static const bb_route_response_t s_power_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"vcore_mv\":{\"type\":[\"integer\",\"null\"]},"
      "\"icore_ma\":{\"type\":[\"integer\",\"null\"]},"
      "\"pcore_mw\":{\"type\":[\"integer\",\"null\"]},"
      "\"efficiency_jth\":{\"type\":[\"number\",\"null\"],"
      "\"description\":\"J/TH; null until ASIC hashrate and power both available\"},"
      "\"vin_mv\":{\"type\":[\"integer\",\"null\"]},"
      "\"vin_low\":{\"type\":[\"boolean\",\"null\"]},"
      "\"board_temp_c\":{\"type\":[\"number\",\"null\"]},"
      "\"vr_temp_c\":{\"type\":[\"number\",\"null\"]}}}",
      "ASIC power and thermal telemetry" },
    { 0 },
};

static const bb_route_t s_power_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/power",
    .tag          = "mining",
    .summary      = "Get ASIC power and thermal telemetry",
    .operation_id = "getPower",
    .responses    = s_power_responses,
    .handler      = power_handler,
};

// ---------------------------------------------------------------------------
// /api/fan — GET (ASIC boards only)
// ---------------------------------------------------------------------------

static const bb_route_response_t s_fan_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"rpm\":{\"type\":[\"integer\",\"null\"]},"
      "\"duty_pct\":{\"type\":[\"integer\",\"null\"],"
      "\"description\":\"curve-controlled duty %; null until first telemetry tick\"}}}",
      "fan telemetry" },
    { 0 },
};

static const bb_route_t s_fan_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/fan",
    .tag          = "mining",
    .summary      = "Get fan speed and duty cycle",
    .operation_id = "getFan",
    .responses    = s_fan_responses,
    .handler      = fan_handler,
};

#endif /* ASIC_CHIP */

// ============================================================================
// REGISTRATION
// ============================================================================

void taipan_web_install_prov_save_cb(void)
{
    bb_prov_set_save_callback(taipan_prov_save_cb);
}

static bb_http_asset_t s_prov_assets[] = {
    { "/",            "text/html",     "gzip", NULL, 0  },
    { "/theme.css",   "text/css",      "gzip", NULL, 0  },
    { "/logo.svg",    "image/svg+xml", "gzip", NULL, 0  },
    { "/favicon.ico", "image/svg+xml", "gzip", NULL, 0  },
};

const bb_http_asset_t *taipan_web_prov_assets(size_t *n)
{
    // Initialize asset data on first call
    static bool initialized = false;
    if (!initialized) {
        s_prov_assets[0].data = prov_form_html_gz;
        s_prov_assets[0].len = prov_form_html_gz_len;
        s_prov_assets[1].data = theme_css_gz;
        s_prov_assets[1].len = theme_css_gz_len;
        s_prov_assets[2].data = logo_svg_gz;
        s_prov_assets[2].len = logo_svg_gz_len;
        s_prov_assets[3].data = favicon_svg_gz;
        s_prov_assets[3].len = favicon_svg_gz_len;
        initialized = true;
    }
    *n = sizeof(s_prov_assets) / sizeof(s_prov_assets[0]);
    return s_prov_assets;
}

static bb_http_asset_t s_mining_assets[] = {
    { "/",                  "text/html",              "gzip", NULL, 0 },
    { "/assets/index.js",   "application/javascript", "gzip", NULL, 0 },
    { "/assets/index.css",  "text/css",               "gzip", NULL, 0 },
    { "/logo.svg",          "image/svg+xml",          "gzip", NULL, 0 },
    { "/favicon.svg",       "image/svg+xml",          "gzip", NULL, 0 },
};

static void init_mining_assets(void)
{
    static bool initialized = false;
    if (!initialized) {
        s_mining_assets[0].data = index_html_gz;
        s_mining_assets[0].len = index_html_gz_len;
        s_mining_assets[1].data = index_js_gz;
        s_mining_assets[1].len = index_js_gz_len;
        s_mining_assets[2].data = index_css_gz;
        s_mining_assets[2].len = index_css_gz_len;
        s_mining_assets[3].data = logo_svg_gz;
        s_mining_assets[3].len = logo_svg_gz_len;
        s_mining_assets[4].data = favicon_svg_gz;
        s_mining_assets[4].len = favicon_svg_gz_len;
        initialized = true;
    }
}

bb_err_t taipan_web_register_mining_routes(bb_http_handle_t server)
{
    // Cache WDT reset count — only changes on boot
    bb_nv_get_u32("taipanminer", "wdt_resets", &s_wdt_resets, 0);

    // Initialize and register static assets
    init_mining_assets();
    bb_http_register_assets(server, s_mining_assets, sizeof(s_mining_assets)/sizeof(s_mining_assets[0]));

    // Initialize breadboard's OTA validator (registers /api/ota/mark-valid handler)
    bb_ota_validator_init(server);

    // Register dynamic handlers with OpenAPI descriptors
    bb_err_t rc;
    rc = bb_http_register_described_route(server, &s_stats_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_pool_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_diag_asic_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_knot_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_settings_get_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_settings_post_route);
    if (rc != BB_OK) return rc;

#ifdef ASIC_CHIP
    rc = bb_http_register_described_route(server, &s_power_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_fan_route);
    if (rc != BB_OK) return rc;
#endif

    rc = bb_http_register_described_route(server, &s_settings_patch_route);
    if (rc != BB_OK) return rc;

    bb_ota_pull_register_handler(server);
    bb_ota_push_register_handler(server);

    bb_http_register_common_routes(server);
    bb_info_register_routes(server);
    bb_wifi_register_routes(server);
    bb_board_register_routes(server);
    bb_log_stream_register_routes(server);
    bb_log_register_routes(server);

    bb_log_i(TAG, "mining routes registered");
    return BB_OK;
}
