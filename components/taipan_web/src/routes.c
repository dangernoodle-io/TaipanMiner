#include "taipan_web.h"
#include "bb_http.h"
#include "bb_info.h"
#include "bb_json.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "board.h"
#include "mining.h"
#include "bb_nv.h"
#include "taipan_config.h"
#include "bb_wifi.h"
#include "bb_prov.h"
#include "bb_mdns.h"
#include "stratum.h"
#include "cJSON.h"
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
#include "ota_validator.h"
#include "bb_log.h"
#include "bb_board.h"

static const char *TAG = "web";

extern const uint8_t prov_form_html_gz[];
extern const size_t prov_form_html_gz_len;
extern const uint8_t theme_css_gz[];
extern const size_t theme_css_gz_len;
extern const uint8_t logo_svg_gz[];
extern const size_t logo_svg_gz_len;
extern const uint8_t mining_html_gz[];
extern const size_t mining_html_gz_len;
extern const uint8_t mining_js_gz[];
extern const size_t mining_js_gz_len;
extern const uint8_t prov_save_html_gz[];
extern const size_t prov_save_html_gz_len;
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
    char pool_pass[64] = "";
    char port_str[8] = "";

    bb_url_decode_field(body, "pool_host", pool_host, sizeof(pool_host));
    bb_url_decode_field(body, "pool_port", port_str, sizeof(port_str));
    bb_url_decode_field(body, "wallet", wallet, sizeof(wallet));
    bb_url_decode_field(body, "worker", worker, sizeof(worker));
    bb_url_decode_field(body, "pool_pass", pool_pass, sizeof(pool_pass));

    if (pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        bb_http_resp_send_err(req, 400, "All fields required");
        return BB_ERR_INVALID_ARG;
    }
    uint16_t port = (uint16_t)strtoul(port_str, NULL, 10);
    if (port == 0) {
        bb_http_resp_send_err(req, 400, "Valid port required");
        return BB_ERR_INVALID_ARG;
    }
    if (taipan_config_set_pool(pool_host, port, wallet, worker, pool_pass) != ESP_OK) {
        bb_http_resp_send_err(req, 500, "Failed to save config");
        return BB_ERR_INVALID_ARG;
    }

    bb_http_resp_set_header(req, "Connection", "close");
    bb_http_resp_set_header(req, "Access-Control-Allow-Origin", "*");
    bb_http_resp_set_header(req, "Access-Control-Allow-Private-Network", "true");
    bb_http_resp_set_header(req, "Content-Encoding", "gzip");
    bb_http_resp_set_header(req, "Content-Type", "text/html");
    bb_http_resp_send(req, (const char *)prov_save_html_gz, prov_save_html_gz_len);
    return BB_OK;
}

// ============================================================================
// PORTABLE HANDLERS (migrated to bb_http API)
// ============================================================================

static bb_err_t stats_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    double hw_rate = 0, hw_ema = 0;
    double pool_diff = 0;
    double best_diff = 0;
    uint32_t hw_shares = 0;
    float temp = 0;
    uint32_t session_shares = 0, session_rejected = 0;
    int64_t last_share_us = 0, session_start_us = 0;
    mining_lifetime_t lifetime = {0};
#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
    double asic_rate = 0, asic_ema = 0;
    uint32_t asic_shares = 0;
    float asic_temp = 0;
    float asic_freq_cfg = -1.0f, asic_freq_eff = -1.0f;
#endif

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        hw_rate = mining_stats.hw_hashrate;
        hw_ema = mining_stats.hw_ema.value;
        hw_shares = mining_stats.hw_shares;
        pool_diff = mining_stats.pool_difficulty;
        temp = mining_stats.temp_c;
        session_shares = mining_stats.session.shares;
        session_rejected = mining_stats.session.rejected;
        last_share_us = mining_stats.session.last_share_us;
        session_start_us = mining_stats.session.start_us;
        best_diff = mining_stats.session.best_diff;
        lifetime = mining_stats.lifetime;
#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
        asic_rate = mining_stats.asic_hashrate;
        asic_ema = mining_stats.asic_ema.value;
        asic_shares = mining_stats.asic_shares;
        asic_temp = mining_stats.asic_temp_c;
        asic_freq_cfg = mining_stats.asic_freq_configured_mhz;
        asic_freq_eff = mining_stats.asic_freq_effective_mhz;
#endif
        xSemaphoreGive(mining_stats.mutex);
    }

    const esp_app_desc_t *app = esp_app_get_description();
    int64_t now_us = esp_timer_get_time();
    int64_t uptime_s = (session_start_us > 0) ? (now_us - session_start_us) / 1000000 : 0;
    int64_t last_share_ago_s = (last_share_us > 0) ? (now_us - last_share_us) / 1000000 : -1;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "hashrate", hw_rate);
    cJSON_AddNumberToObject(root, "hashrate_avg", hw_ema);
    cJSON_AddNumberToObject(root, "temp_c", (double)temp);
    cJSON_AddNumberToObject(root, "shares", hw_shares);
    cJSON_AddNumberToObject(root, "pool_difficulty", pool_diff);
    cJSON_AddNumberToObject(root, "session_shares", session_shares);
    cJSON_AddNumberToObject(root, "session_rejected", session_rejected);
    cJSON_AddNumberToObject(root, "last_share_ago_s", (double)last_share_ago_s);
    cJSON_AddNumberToObject(root, "lifetime_shares", lifetime.total_shares);
    cJSON_AddNumberToObject(root, "best_diff", best_diff);
    cJSON_AddStringToObject(root, "pool_host", taipan_config_pool_host());
    cJSON_AddNumberToObject(root, "pool_port", taipan_config_pool_port());
    cJSON_AddStringToObject(root, "worker", taipan_config_worker_name());
    cJSON_AddStringToObject(root, "wallet", taipan_config_wallet_addr());
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);
    cJSON_AddNumberToObject(root, "free_heap", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "total_heap", (double)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(root, "rssi_dbm", ap_info.rssi);
    } else {
        cJSON_AddNullToObject(root, "rssi_dbm");
    }
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "build_date", app->date);
    cJSON_AddStringToObject(root, "build_time", app->time);
    cJSON_AddStringToObject(root, "board", BOARD_NAME);
    cJSON_AddBoolToObject(root, "display_en", bb_nv_config_display_enabled());
#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
    cJSON_AddNumberToObject(root, "asic_hashrate", asic_rate);
    cJSON_AddNumberToObject(root, "asic_hashrate_avg", asic_ema);
    cJSON_AddNumberToObject(root, "asic_shares", asic_shares);
    cJSON_AddNumberToObject(root, "asic_temp_c", (double)asic_temp);
    if (asic_freq_cfg >= 0) {
        cJSON_AddNumberToObject(root, "asic_freq_configured_mhz", (double)asic_freq_cfg);
    } else {
        cJSON_AddNullToObject(root, "asic_freq_configured_mhz");
    }
    if (asic_freq_eff >= 0) {
        cJSON_AddNumberToObject(root, "asic_freq_effective_mhz", (double)asic_freq_eff);
    } else {
        cJSON_AddNullToObject(root, "asic_freq_effective_mhz");
    }
    cJSON_AddNumberToObject(root, "asic_small_cores", BOARD_SMALL_CORES);
    cJSON_AddNumberToObject(root, "asic_count", BOARD_ASIC_COUNT);
#endif

    char *json = cJSON_PrintUnformatted(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return rc;
}

#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
static bb_err_t power_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    int vcore_mv = -1, icore_ma = -1, pcore_mw = -1;
    int vin_mv = -1;
    float board_temp_c = -1.0f, vr_temp_c = -1.0f;
    double asic_hashrate = 0;

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        vcore_mv = mining_stats.vcore_mv;
        icore_ma = mining_stats.icore_ma;
        pcore_mw = mining_stats.pcore_mw;
        vin_mv = mining_stats.vin_mv;
        asic_hashrate = mining_stats.asic_hashrate;
        board_temp_c = mining_stats.board_temp_c;
        vr_temp_c = mining_stats.vr_temp_c;
        xSemaphoreGive(mining_stats.mutex);
    }

    cJSON *root = cJSON_CreateObject();
    if (vcore_mv >= 0) {
        cJSON_AddNumberToObject(root, "vcore_mv", vcore_mv);
    } else {
        cJSON_AddNullToObject(root, "vcore_mv");
    }
    if (icore_ma >= 0) {
        cJSON_AddNumberToObject(root, "icore_ma", icore_ma);
    } else {
        cJSON_AddNullToObject(root, "icore_ma");
    }
    if (pcore_mw >= 0) {
        cJSON_AddNumberToObject(root, "pcore_mw", pcore_mw);
    } else {
        cJSON_AddNullToObject(root, "pcore_mw");
    }
    if (pcore_mw > 0 && asic_hashrate > 0) {
        cJSON_AddNumberToObject(root, "efficiency_jth", (pcore_mw / 1000.0) / (asic_hashrate / 1e12));
    } else {
        cJSON_AddNullToObject(root, "efficiency_jth");
    }
    if (vin_mv >= 0) {
        cJSON_AddNumberToObject(root, "vin_mv", vin_mv);
    } else {
        cJSON_AddNullToObject(root, "vin_mv");
    }
    if (vin_mv >= 0) {
        bool vin_low = (vin_mv < (BOARD_NOMINAL_VIN_MV + 500) * 87 / 100);
        cJSON_AddBoolToObject(root, "vin_low", vin_low);
    } else {
        cJSON_AddNullToObject(root, "vin_low");
    }
    if (board_temp_c >= 0.0f) {
        cJSON_AddNumberToObject(root, "board_temp_c", (double)board_temp_c);
    } else {
        cJSON_AddNullToObject(root, "board_temp_c");
    }
    if (vr_temp_c >= 0.0f) {
        cJSON_AddNumberToObject(root, "vr_temp_c", (double)vr_temp_c);
    } else {
        cJSON_AddNullToObject(root, "vr_temp_c");
    }

    char *json = cJSON_PrintUnformatted(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return rc;
}

static bb_err_t fan_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    int fan_rpm = -1;
    int fan_duty_pct = -1;

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        fan_rpm = mining_stats.fan_rpm;
        fan_duty_pct = mining_stats.fan_duty_pct;
        xSemaphoreGive(mining_stats.mutex);
    }

    cJSON *root = cJSON_CreateObject();
    if (fan_rpm >= 0) {
        cJSON_AddNumberToObject(root, "rpm", fan_rpm);
    } else {
        cJSON_AddNullToObject(root, "rpm");
    }
    if (fan_duty_pct >= 0) {
        cJSON_AddNumberToObject(root, "duty_pct", fan_duty_pct);
    } else {
        cJSON_AddNullToObject(root, "duty_pct");
    }

    char *json = cJSON_PrintUnformatted(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return rc;
}
#endif // ASIC_BM1370 || ASIC_BM1368

static bb_err_t ota_mark_valid_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    esp_err_t err = ota_validator_mark_valid_manual();

    cJSON *root = cJSON_CreateObject();

    if (err == ESP_OK) {
        bb_http_resp_set_status(req, 200);
        cJSON_AddBoolToObject(root, "validated", true);
    } else if (err == ESP_ERR_INVALID_STATE) {
        bb_http_resp_set_status(req, 409);
        cJSON_AddStringToObject(root, "error", "not pending");
    } else {
        bb_http_resp_set_status(req, 500);
        cJSON_AddStringToObject(root, "error", "mark_valid failed");
    }

    char *json = cJSON_PrintUnformatted(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return rc;
}

static void taipan_info_extender(bb_json_t root)
{
    // On ESP-IDF, bb_json_t is cJSON* underneath; cast to use cJSON API
    // (per plan: cJSON → bb_json migration is backlog item B4, not in step 6)
    cJSON *json_root = (cJSON *)root;

    cJSON_AddStringToObject(json_root, "worker_name", taipan_config_worker_name());

    const char *ssid = bb_nv_config_wifi_ssid();
    if (ssid) cJSON_AddStringToObject(json_root, "ssid", ssid);

    cJSON_AddBoolToObject(json_root, "validated", !ota_validator_is_pending());
    cJSON_AddNumberToObject(json_root, "wdt_resets", s_wdt_resets);

    time_t now = time(NULL);
    if (now > 1700000000) {
        int64_t uptime_s = esp_timer_get_time() / 1000000LL;
        cJSON_AddNumberToObject(json_root, "boot_time", (double)(now - uptime_s));
    }

    cJSON *network = cJSON_GetObjectItem(json_root, "network");
    if (network) {
        cJSON_AddBoolToObject(network, "mdns", bb_mdns_started());
        cJSON_AddBoolToObject(network, "stratum", stratum_is_connected());
        cJSON_AddNumberToObject(network, "stratum_reconnect_ms", stratum_get_reconnect_delay_ms());
        cJSON_AddNumberToObject(network, "stratum_fail_count", stratum_get_connect_fail_count());
    }
}

bb_err_t taipan_web_register_info_extender(void)
{
    return bb_info_register_extender(taipan_info_extender);
}

static bb_err_t settings_get_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "pool_host", taipan_config_pool_host());
    cJSON_AddNumberToObject(root, "pool_port", taipan_config_pool_port());
    cJSON_AddStringToObject(root, "wallet", taipan_config_wallet_addr());
    cJSON_AddStringToObject(root, "worker", taipan_config_worker_name());
    cJSON_AddStringToObject(root, "pool_pass", taipan_config_pool_pass());
    cJSON_AddBoolToObject(root, "display_en", bb_nv_config_display_enabled());
    cJSON_AddBoolToObject(root, "ota_skip_check", bb_nv_config_ota_skip_check());

    char *json = cJSON_PrintUnformatted(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return rc;
}

// Shared helper for POST (full) and PATCH (partial) settings
static bb_err_t apply_settings(bb_http_request_t *req, bool partial)
{
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

    cJSON *root = cJSON_Parse(body);
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

    cJSON *j;

    j = cJSON_GetObjectItem(root, "pool_host");
    if (j && cJSON_IsString(j)) { pool_host = j->valuestring; }
    else if (!partial) { if (!j) { cJSON_Delete(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "pool_host required", 18); return BB_ERR_INVALID_ARG; } }

    j = cJSON_GetObjectItem(root, "pool_port");
    if (j && cJSON_IsNumber(j)) { pool_port = (uint16_t)j->valuedouble; }
    else if (!partial) { if (!j) { cJSON_Delete(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "pool_port required", 18); return BB_ERR_INVALID_ARG; } }

    j = cJSON_GetObjectItem(root, "wallet");
    if (j && cJSON_IsString(j)) { wallet = j->valuestring; }
    else if (!partial) { if (!j) { cJSON_Delete(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "wallet required", 15); return BB_ERR_INVALID_ARG; } }

    j = cJSON_GetObjectItem(root, "worker");
    if (j && cJSON_IsString(j)) { worker = j->valuestring; }
    else if (!partial) { if (!j) { cJSON_Delete(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "worker required", 15); return BB_ERR_INVALID_ARG; } }

    j = cJSON_GetObjectItem(root, "pool_pass");
    if (j && cJSON_IsString(j)) { pool_pass = j->valuestring; }
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
        cJSON_Delete(root);
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "pool_host, wallet, worker must not be empty";
        return bb_http_resp_send(req, msg, strlen(msg));
    }
    if (pool_port == 0) {
        cJSON_Delete(root);
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "pool_port must be > 0";
        return bb_http_resp_send(req, msg, strlen(msg));
    }

    // Save mining config if any mining field was provided
    if (reboot_required) {
        esp_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            bb_http_resp_set_status(req, 500);
            bb_http_resp_set_header(req, "Content-Type", "text/plain");
            static const char *msg = "Failed to save config";
            return bb_http_resp_send(req, msg, strlen(msg));
        }
    }

    // Handle display_en separately (takes effect immediately, no reboot needed)
    j = cJSON_GetObjectItem(root, "display_en");
    if (j && cJSON_IsBool(j)) {
        esp_err_t err = bb_nv_config_set_display_enabled(cJSON_IsTrue(j));
        if (err != ESP_OK) {
            cJSON_Delete(root);
            bb_http_resp_set_status(req, 500);
            bb_http_resp_set_header(req, "Content-Type", "text/plain");
            static const char *msg = "Failed to save display setting";
            return bb_http_resp_send(req, msg, strlen(msg));
        }
    }

    j = cJSON_GetObjectItem(root, "ota_skip_check");
    if (j && cJSON_IsBool(j)) {
        esp_err_t err = bb_nv_config_set_ota_skip_check(cJSON_IsTrue(j));
        if (err != ESP_OK) {
            cJSON_Delete(root);
            bb_http_resp_set_status(req, 500);
            bb_http_resp_set_header(req, "Content-Type", "text/plain");
            static const char *msg = "Failed to save ota_skip_check";
            return bb_http_resp_send(req, msg, strlen(msg));
        }
    }

    cJSON_Delete(root);

    // Response
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"saved\",\"reboot_required\":%s}",
             reboot_required ? "true" : "false");
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    return bb_http_resp_send(req, resp, strlen(resp));
}

static bb_err_t settings_post_handler(bb_http_request_t *req)
{
    return apply_settings(req, false);
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

    cJSON *root = cJSON_Parse(body);
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
    bool reboot_required = false;

    cJSON *j;

    j = cJSON_GetObjectItem(root, "pool_host");
    if (j && cJSON_IsString(j)) { pool_host = j->valuestring; }

    j = cJSON_GetObjectItem(root, "pool_port");
    if (j && cJSON_IsNumber(j)) { pool_port = (uint16_t)j->valuedouble; }

    j = cJSON_GetObjectItem(root, "wallet");
    if (j && cJSON_IsString(j)) { wallet = j->valuestring; }

    j = cJSON_GetObjectItem(root, "worker");
    if (j && cJSON_IsString(j)) { worker = j->valuestring; }

    j = cJSON_GetObjectItem(root, "pool_pass");
    if (j && cJSON_IsString(j)) { pool_pass = j->valuestring; }

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
        cJSON_Delete(root);
        bb_http_resp_send_err(req, 400, "pool_host, wallet, worker must not be empty");
        return BB_ERR_INVALID_ARG;
    }
    if (pool_port == 0) {
        cJSON_Delete(root);
        bb_http_resp_send_err(req, 400, "pool_port must be > 0");
        return BB_ERR_INVALID_ARG;
    }

    // Save mining config if any mining field was provided
    if (reboot_required) {
        esp_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            bb_http_resp_send_err(req, 500, "Failed to save config");
            return BB_ERR_INVALID_ARG;
        }
    }

    // Handle display_en separately (takes effect immediately, no reboot needed)
    j = cJSON_GetObjectItem(root, "display_en");
    if (j && cJSON_IsBool(j)) {
        esp_err_t err = bb_nv_config_set_display_enabled(cJSON_IsTrue(j));
        if (err != ESP_OK) {
            cJSON_Delete(root);
            bb_http_resp_send_err(req, 500, "Failed to save display setting");
            return BB_ERR_INVALID_ARG;
        }
    }

    j = cJSON_GetObjectItem(root, "ota_skip_check");
    if (j && cJSON_IsBool(j)) {
        esp_err_t err = bb_nv_config_set_ota_skip_check(cJSON_IsTrue(j));
        if (err != ESP_OK) {
            cJSON_Delete(root);
            bb_http_resp_send_err(req, 500, "Failed to save ota_skip_check");
            return BB_ERR_INVALID_ARG;
        }
    }

    cJSON_Delete(root);

    // Response
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"saved\",\"reboot_required\":%s}",
             reboot_required ? "true" : "false");
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    return bb_http_resp_send(req, resp, strlen(resp));
}

// ============================================================================
// REGISTRATION
// ============================================================================

void taipan_web_install_prov_save_cb(void)
{
    bb_prov_set_save_callback(taipan_prov_save_cb);
}

bb_err_t taipan_web_finish_prov_setup(bb_http_handle_t server)
{
    bb_http_register_common_routes(server);
    bb_info_register_routes(server);
    ESP_LOGI(TAG, "provisioning routes registered");
    return BB_OK;
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
    { "/",            "text/html",       "gzip", NULL, 0    },
    { "/mining.js",   "application/javascript", "gzip", NULL, 0    },
    { "/theme.css",   "text/css",        "gzip", NULL, 0      },
    { "/logo.svg",    "image/svg+xml",   "gzip", NULL, 0       },
    { "/favicon.ico", "image/svg+xml",   "gzip", NULL, 0    },
};

static void init_mining_assets(void)
{
    static bool initialized = false;
    if (!initialized) {
        s_mining_assets[0].data = mining_html_gz;
        s_mining_assets[0].len = mining_html_gz_len;
        s_mining_assets[1].data = mining_js_gz;
        s_mining_assets[1].len = mining_js_gz_len;
        s_mining_assets[2].data = theme_css_gz;
        s_mining_assets[2].len = theme_css_gz_len;
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

    // Register dynamic handlers (portable bb_http)
    bb_err_t rc;
    rc = bb_http_register_route(server, BB_HTTP_GET, "/api/stats", stats_handler);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_route(server, BB_HTTP_POST, "/api/ota/mark-valid", ota_mark_valid_handler);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_route(server, BB_HTTP_GET, "/api/settings", settings_get_handler);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_route(server, BB_HTTP_POST, "/api/settings", settings_post_handler);
    if (rc != BB_OK) return rc;

#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
    rc = bb_http_register_route(server, BB_HTTP_GET, "/api/power", power_handler);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_route(server, BB_HTTP_GET, "/api/fan", fan_handler);
    if (rc != BB_OK) return rc;
#endif

    rc = bb_http_register_route(server, BB_HTTP_PATCH, "/api/settings", settings_patch_handler);
    if (rc != BB_OK) return rc;

    bb_ota_pull_register_handler(server);
    bb_ota_push_register_handler(server);

    bb_http_register_common_routes(server);
    bb_info_register_routes(server);
    bb_wifi_register_routes(server);
    bb_board_register_routes(server);
    bb_log_stream_register_routes(server);

    ESP_LOGI(TAG, "mining routes registered");
    return BB_OK;
}
