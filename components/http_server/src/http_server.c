#include "http_server.h"
#include "esp_http_server.h"
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
#include "nvs.h"
#include "board.h"
#include "mining.h"
#include "nv_config.h"
#include "taipan_config.h"
#include "wifi_prov.h"
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
#include "ota_pull.h"
#include "ota_push.h"
#include "ota_validator.h"
#include "log_stream.h"

static const char *TAG = "http";
static httpd_handle_t s_server = NULL;
static uint32_t s_wdt_resets = 0;

extern const unsigned char prov_form_html_gz[];
extern const unsigned int prov_form_html_gz_len;
extern const unsigned char theme_css_gz[];
extern const unsigned int theme_css_gz_len;
extern const unsigned char logo_svg_gz[];
extern const unsigned int logo_svg_gz_len;
extern const unsigned char mining_html_gz[];
extern const unsigned int mining_html_gz_len;
extern const unsigned char mining_js_gz[];
extern const unsigned int mining_js_gz_len;
extern const unsigned char prov_save_html_gz[];
extern const unsigned int prov_save_html_gz_len;
extern const uint8_t favicon_svg_gz[];
extern const unsigned int favicon_svg_gz_len;

static esp_err_t preflight_handler(httpd_req_t *req);

static esp_err_t ensure_server_started(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 32;
    config.stack_size = 6144;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;

    httpd_uri_t preflight = { .uri = "/*", .method = HTTP_OPTIONS, .handler = preflight_handler };
    httpd_register_uri_handler(s_server, &preflight);
    return ESP_OK;
}

static void set_common_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
}

static esp_err_t preflight_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t prov_form_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)prov_form_html_gz, prov_form_html_gz_len);
    return ESP_OK;
}

// This function has been replaced by the embedded HTML above
static esp_err_t prov_save_handler(httpd_req_t *req)
{
    set_common_headers(req);
    char body[512];

    // Validate content length to prevent silent body truncation
    if (req->content_len > sizeof(body) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    // Parse URL-encoded fields
    char ssid[32] = "", pass[64] = "";
    char pool_host[64] = "", wallet[64] = "", worker[32] = "";
    char pool_pass[64] = "";
    char port_str[8] = "";

    url_decode_field(body, "ssid", ssid, sizeof(ssid));
    url_decode_field(body, "pass", pass, sizeof(pass));
    url_decode_field(body, "pool_host", pool_host, sizeof(pool_host));
    url_decode_field(body, "pool_port", port_str, sizeof(port_str));
    url_decode_field(body, "wallet", wallet, sizeof(wallet));
    url_decode_field(body, "worker", worker, sizeof(worker));
    url_decode_field(body, "pool_pass", pool_pass, sizeof(pool_pass));

    if (ssid[0] == '\0' || pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "All fields required");
        return ESP_FAIL;
    }

    uint16_t port = (uint16_t)strtoul(port_str, NULL, 10);
    if (port == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Valid port required");
        return ESP_FAIL;
    }

    esp_err_t err = bb_nv_config_set_wifi(ssid, pass);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    err = taipan_config_set_pool(pool_host, port, wallet, worker, pool_pass);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)prov_save_html_gz, prov_save_html_gz_len);

    // Signal provisioning complete
    extern EventGroupHandle_t g_prov_event_group;
    xEventGroupSetBits(g_prov_event_group, BIT0);

    return ESP_OK;
}

static esp_err_t prov_redirect_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)mining_html_gz, mining_html_gz_len);
    return ESP_OK;
}

static esp_err_t stats_handler(httpd_req_t *req)
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
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
static esp_err_t power_handler(httpd_req_t *req)
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
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t fan_handler(httpd_req_t *req)
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
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}
#endif // ASIC_BM1370 || ASIC_BM1368

static esp_err_t version_handler(httpd_req_t *req)
{
    set_common_headers(req);
    const esp_app_desc_t *app = esp_app_get_description();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, app->version, strlen(app->version));
    return ESP_OK;
}

static esp_err_t info_handler(httpd_req_t *req)
{
    set_common_headers(req);
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "board", BOARD_NAME);
    cJSON_AddStringToObject(root, "project_name", app->project_name);
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "idf_version", app->idf_ver);
    cJSON_AddStringToObject(root, "build_date", app->date);
    cJSON_AddStringToObject(root, "build_time", app->time);
    cJSON_AddNumberToObject(root, "cores", chip.cores);

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddStringToObject(root, "worker_name", taipan_config_worker_name());
    cJSON_AddStringToObject(root, "ssid", bb_nv_config_wifi_ssid());

    cJSON_AddNumberToObject(root, "total_heap", (double)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "free_heap", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    cJSON_AddNumberToObject(root, "flash_size", (double)flash_size);

    esp_reset_reason_t reason = esp_reset_reason();
    const char *reason_str;
    switch (reason) {
    case ESP_RST_POWERON:   reason_str = "power-on"; break;
    case ESP_RST_SW:        reason_str = "software"; break;
    case ESP_RST_PANIC:     reason_str = "panic"; break;
    case ESP_RST_TASK_WDT:  reason_str = "task_wdt"; break;
    case ESP_RST_WDT:       reason_str = "wdt"; break;
    case ESP_RST_DEEPSLEEP: reason_str = "deep_sleep"; break;
    case ESP_RST_BROWNOUT:  reason_str = "brownout"; break;
    default:                reason_str = "unknown"; break;
    }
    cJSON_AddStringToObject(root, "reset_reason", reason_str);

    cJSON_AddNumberToObject(root, "wdt_resets", (double)s_wdt_resets);

    {
        time_t now = time(NULL);
        if (now > 1000000000) {
            int64_t uptime_s = esp_timer_get_time() / 1000000;
            cJSON_AddNumberToObject(root, "boot_time", (double)(now - uptime_s));
        }
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        cJSON_AddNumberToObject(root, "app_size", (double)running->size);
    }

    cJSON_AddBoolToObject(root, "validated", !ota_validator_is_pending());

    cJSON *network = cJSON_CreateObject();

    int8_t rssi = 0;
    wifi_prov_get_rssi(&rssi);
    cJSON_AddNumberToObject(network, "rssi", (double)rssi);

    char ip[16] = "0.0.0.0";
    wifi_prov_get_ip_str(ip, sizeof(ip));
    cJSON_AddStringToObject(network, "ip", ip);

    uint8_t disc_reason = 0;
    int64_t disc_age_us = 0;
    wifi_prov_get_disconnect(&disc_reason, &disc_age_us);
    cJSON_AddNumberToObject(network, "disc_reason", (double)disc_reason);
    uint32_t disc_age_s = (uint32_t)(disc_age_us / 1000000);
    cJSON_AddNumberToObject(network, "disc_age_s", (double)disc_age_s);

    int retry_count = wifi_prov_get_retry_count();
    cJSON_AddNumberToObject(network, "retry_count", (double)retry_count);

    bool mdns = wifi_prov_mdns_started();
    cJSON_AddBoolToObject(network, "mdns", mdns);

    bool strat = stratum_is_connected();
    cJSON_AddBoolToObject(network, "stratum", strat);

    uint32_t strat_delay = stratum_get_reconnect_delay_ms();
    cJSON_AddNumberToObject(network, "stratum_reconnect_ms", (double)strat_delay);

    int strat_fail = stratum_get_connect_fail_count();
    cJSON_AddNumberToObject(network, "stratum_fail_count", (double)strat_fail);

    cJSON_AddItemToObject(root, "network", network);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t ota_mark_valid_handler(httpd_req_t *req)
{
    set_common_headers(req);
    esp_err_t err = ota_validator_mark_valid_manual();

    cJSON *root = cJSON_CreateObject();

    if (err == ESP_OK) {
        httpd_resp_set_status(req, "200 OK");
        cJSON_AddBoolToObject(root, "validated", true);
    } else if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        cJSON_AddStringToObject(root, "error", "not pending");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        cJSON_AddStringToObject(root, "error", "mark_valid failed");
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t logo_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_send(req, (const char *)logo_svg_gz, logo_svg_gz_len);
    return ESP_OK;
}

static esp_err_t theme_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)theme_css_gz, theme_css_gz_len);
    return ESP_OK;
}

static esp_err_t mining_js_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)mining_js_gz, mining_js_gz_len);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    set_common_headers(req);
    httpd_resp_send(req, (const char *)favicon_svg_gz, favicon_svg_gz_len);
    return ESP_OK;
}

static volatile int s_sse_client_type = 0;  // 0=none, 1=browser, 2=external
static volatile bool s_sse_stop = false;
static volatile TaskHandle_t s_sse_task_handle = NULL;

static void s_sse_task(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;

    int fd = httpd_req_to_sockfd(req);
    struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");

    esp_err_t err = httpd_resp_send_chunk(req, ": connected\n\n", HTTPD_RESP_USE_STRLEN);

    char line[192];
    char frame[220];

    while (err == ESP_OK && !s_sse_stop) {
        size_t n = bb_log_stream_drain(line, sizeof(line), 500);
        if (n == 0) continue;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        int flen = snprintf(frame, sizeof(frame), "data: %s\n\n", line);
        err = httpd_resp_send_chunk(req, frame,
                    (flen > 0 && flen < (int)sizeof(frame)) ? flen : HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_chunk(req, NULL, 0);
    httpd_req_async_handler_complete(req);
    s_sse_task_handle = NULL;
    s_sse_client_type = 0;
    vTaskDelete(NULL);
}

static esp_err_t logs_handler(httpd_req_t *req)
{
    // Determine client type from query string
    int client_type = 2;  // default: external
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "source", val, sizeof(val)) == ESP_OK
            && strcmp(val, "browser") == 0) {
            client_type = 1;
        }
    }

    // Only an external client can preempt a browser stream;
    // all other combinations (browser→external, external→external) get 503
    if (s_sse_task_handle && !(client_type == 2 && s_sse_client_type == 1)) {
        set_common_headers(req);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "Log stream in use");
        return ESP_OK;
    }

    // Stop existing browser stream (external client taking over)
    if (s_sse_task_handle) {
        s_sse_stop = true;
        for (int i = 0; i < 10 && s_sse_task_handle; i++)
            vTaskDelay(pdMS_TO_TICKS(100));
    }
    s_sse_stop = false;

    if (client_type == 2) {
        ESP_LOGI(TAG, "external log client connected");
    }

    httpd_req_t *async_req = NULL;
    if (httpd_req_async_handler_begin(req, &async_req) != ESP_OK) {
        set_common_headers(req);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Async init failed");
        return ESP_FAIL;
    }

    s_sse_client_type = client_type;
    if (xTaskCreate(s_sse_task, "sse_log", 4096, async_req, 1, (TaskHandle_t *)&s_sse_task_handle) != pdPASS) {
        httpd_req_async_handler_complete(async_req);
        s_sse_client_type = 0;
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t logs_status_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_type(req, "application/json");
    char buf[96];
    uint32_t dropped = bb_log_stream_dropped_lines();
    if (s_sse_client_type == 0) {
        snprintf(buf, sizeof(buf), "{\"active\":false,\"client\":null,\"dropped\":%" PRIu32 "}", dropped);
    } else {
        snprintf(buf, sizeof(buf), "{\"active\":true,\"client\":\"%s\",\"dropped\":%" PRIu32 "}",
                 s_sse_client_type == 1 ? "browser" : "external", dropped);
    }
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;  // unreachable
}

static esp_err_t settings_get_handler(httpd_req_t *req)
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
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

// Shared helper for POST (full) and PATCH (partial) settings
static esp_err_t apply_settings(httpd_req_t *req, bool partial)
{
    set_common_headers(req);
    char body[512];

    if (req->content_len > sizeof(body) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
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
    else if (!partial) { if (!j) { cJSON_Delete(root); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pool_host required"); return ESP_FAIL; } }

    j = cJSON_GetObjectItem(root, "pool_port");
    if (j && cJSON_IsNumber(j)) { pool_port = (uint16_t)j->valuedouble; }
    else if (!partial) { if (!j) { cJSON_Delete(root); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pool_port required"); return ESP_FAIL; } }

    j = cJSON_GetObjectItem(root, "wallet");
    if (j && cJSON_IsString(j)) { wallet = j->valuestring; }
    else if (!partial) { if (!j) { cJSON_Delete(root); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wallet required"); return ESP_FAIL; } }

    j = cJSON_GetObjectItem(root, "worker");
    if (j && cJSON_IsString(j)) { worker = j->valuestring; }
    else if (!partial) { if (!j) { cJSON_Delete(root); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "worker required"); return ESP_FAIL; } }

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
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pool_host, wallet, worker must not be empty");
        return ESP_FAIL;
    }
    if (pool_port == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pool_port must be > 0");
        return ESP_FAIL;
    }

    // Save mining config if any mining field was provided
    if (reboot_required) {
        esp_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
            return ESP_FAIL;
        }
    }

    // Handle display_en separately (takes effect immediately, no reboot needed)
    j = cJSON_GetObjectItem(root, "display_en");
    if (j && cJSON_IsBool(j)) {
        esp_err_t err = bb_nv_config_set_display_enabled(cJSON_IsTrue(j));
        if (err != ESP_OK) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save display setting");
            return ESP_FAIL;
        }
    }

    j = cJSON_GetObjectItem(root, "ota_skip_check");
    if (j && cJSON_IsBool(j)) {
        esp_err_t err = bb_nv_config_set_ota_skip_check(cJSON_IsTrue(j));
        if (err != ESP_OK) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Failed to save ota_skip_check");
            return ESP_FAIL;
        }
    }

    cJSON_Delete(root);

    // Response
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"saved\",\"reboot_required\":%s}",
             reboot_required ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    return apply_settings(req, false);
}

static esp_err_t settings_patch_handler(httpd_req_t *req)
{
    return apply_settings(req, true);
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    set_common_headers(req);

    // Trigger a background scan for next request
    wifi_scan_start_async();

    // Return cached results immediately
    wifi_scan_ap_t aps[WIFI_SCAN_MAX];
    memset(aps, 0, sizeof(aps));
    int count = wifi_scan_get_cached(aps, WIFI_SCAN_MAX);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", aps[i].rssi);
        cJSON_AddBoolToObject(ap, "secure", aps[i].secure);
        cJSON_AddItemToArray(arr, ap);
    }

    char *json = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(arr);
    return ESP_OK;
}

esp_err_t http_server_start_prov(void)
{
    esp_err_t err = ensure_server_started();
    if (err != ESP_OK) return err;

    httpd_uri_t prov_form = { .uri = "/", .method = HTTP_GET, .handler = prov_form_handler };
    httpd_uri_t prov_save = { .uri = "/save", .method = HTTP_POST, .handler = prov_save_handler };
    httpd_uri_t scan_uri = { .uri = "/api/scan", .method = HTTP_GET, .handler = scan_handler };
    httpd_uri_t version_uri = { .uri = "/api/version", .method = HTTP_GET, .handler = version_handler };
    httpd_uri_t info_uri = { .uri = "/api/info", .method = HTTP_GET, .handler = info_handler };
    httpd_uri_t theme_uri = { .uri = "/theme.css", .method = HTTP_GET, .handler = theme_handler };
    httpd_uri_t logo_uri = { .uri = "/logo.svg", .method = HTTP_GET, .handler = logo_handler };
    httpd_uri_t favicon_uri = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler };
    httpd_uri_t prov_redirect = { .uri = "/*", .method = HTTP_GET, .handler = prov_redirect_handler };

    httpd_register_uri_handler(s_server, &prov_form);
    httpd_register_uri_handler(s_server, &prov_save);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &version_uri);
    httpd_register_uri_handler(s_server, &info_uri);
    httpd_register_uri_handler(s_server, &theme_uri);
    httpd_register_uri_handler(s_server, &logo_uri);
    httpd_register_uri_handler(s_server, &favicon_uri);
    httpd_register_uri_handler(s_server, &prov_redirect);

    // Pre-populate scan cache with initial background scan
    wifi_scan_start_async();

    ESP_LOGI(TAG, "provisioning server started on port 80");
    return ESP_OK;
}

void http_server_switch_to_mining(void)
{
    // Cache WDT reset count — only changes on boot
    {
        nvs_handle_t h;
        if (nvs_open("taipanminer", NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u32(h, "wdt_resets", &s_wdt_resets);
            nvs_close(h);
        }
    }

    // Unregister prov handlers
    httpd_unregister_uri_handler(s_server, "/", HTTP_GET);
    httpd_unregister_uri_handler(s_server, "/save", HTTP_POST);
    httpd_unregister_uri_handler(s_server, "/api/scan", HTTP_GET);
    httpd_unregister_uri_handler(s_server, "/*", HTTP_GET);

    // Register mining handlers
    httpd_uri_t status_uri = { .uri = "/", .method = HTTP_GET, .handler = status_handler };
    httpd_uri_t stats_uri = { .uri = "/api/stats", .method = HTTP_GET, .handler = stats_handler };
    httpd_uri_t mining_js_uri = { .uri = "/mining.js", .method = HTTP_GET, .handler = mining_js_handler };

    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &stats_uri);
    httpd_register_uri_handler(s_server, &mining_js_uri);
    bb_ota_pull_register_handler((bb_http_handle_t)s_server);
    bb_ota_push_register_handler((bb_http_handle_t)s_server);

    httpd_uri_t ota_mark_valid_uri = { .uri = "/api/ota/mark-valid", .method = HTTP_POST, .handler = ota_mark_valid_handler };
    httpd_register_uri_handler(s_server, &ota_mark_valid_uri);

    httpd_uri_t logs_status_uri = { .uri = "/api/logs/status", .method = HTTP_GET, .handler = logs_status_handler };
    httpd_uri_t logs_uri = { .uri = "/api/logs", .method = HTTP_GET, .handler = logs_handler };
    httpd_uri_t reboot_uri = { .uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_handler };
    httpd_register_uri_handler(s_server, &logs_status_uri);
    httpd_register_uri_handler(s_server, &logs_uri);
    httpd_register_uri_handler(s_server, &reboot_uri);

    httpd_uri_t settings_get_uri = { .uri = "/api/settings", .method = HTTP_GET, .handler = settings_get_handler };
    httpd_uri_t settings_post_uri = { .uri = "/api/settings", .method = HTTP_POST, .handler = settings_post_handler };
    httpd_uri_t settings_patch_uri = { .uri = "/api/settings", .method = HTTP_PATCH, .handler = settings_patch_handler };
    httpd_register_uri_handler(s_server, &settings_get_uri);
    httpd_register_uri_handler(s_server, &settings_post_uri);
    httpd_register_uri_handler(s_server, &settings_patch_uri);

#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
    httpd_uri_t power_uri = { .uri = "/api/power", .method = HTTP_GET, .handler = power_handler };
    httpd_register_uri_handler(s_server, &power_uri);
    httpd_uri_t fan_uri = { .uri = "/api/fan", .method = HTTP_GET, .handler = fan_handler };
    httpd_register_uri_handler(s_server, &fan_uri);
#endif
}

esp_err_t http_server_start(void)
{
    esp_err_t err = ensure_server_started();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t status_uri = {
        .uri = "/", .method = HTTP_GET, .handler = status_handler
    };
    httpd_uri_t stats_uri = {
        .uri = "/api/stats", .method = HTTP_GET, .handler = stats_handler
    };
    httpd_uri_t version_uri = {
        .uri = "/api/version", .method = HTTP_GET, .handler = version_handler
    };
    httpd_uri_t info_uri = {
        .uri = "/api/info", .method = HTTP_GET, .handler = info_handler
    };
    httpd_uri_t mining_js_uri = {
        .uri = "/mining.js", .method = HTTP_GET, .handler = mining_js_handler
    };
    httpd_uri_t theme_uri = {
        .uri = "/theme.css", .method = HTTP_GET, .handler = theme_handler
    };
    httpd_uri_t logo_uri = {
        .uri = "/logo.svg", .method = HTTP_GET, .handler = logo_handler
    };
    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler
    };

    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &stats_uri);
    httpd_register_uri_handler(s_server, &version_uri);
    httpd_register_uri_handler(s_server, &info_uri);
    httpd_register_uri_handler(s_server, &favicon_uri);
    httpd_register_uri_handler(s_server, &mining_js_uri);
    httpd_register_uri_handler(s_server, &theme_uri);
    httpd_register_uri_handler(s_server, &logo_uri);
    bb_ota_pull_register_handler((bb_http_handle_t)s_server);
    bb_ota_push_register_handler((bb_http_handle_t)s_server);

    httpd_uri_t ota_mark_valid_uri = { .uri = "/api/ota/mark-valid", .method = HTTP_POST, .handler = ota_mark_valid_handler };
    httpd_register_uri_handler(s_server, &ota_mark_valid_uri);

    httpd_uri_t logs_status_uri = { .uri = "/api/logs/status", .method = HTTP_GET, .handler = logs_status_handler };
    httpd_uri_t logs_uri = { .uri = "/api/logs", .method = HTTP_GET, .handler = logs_handler };
    httpd_uri_t reboot_uri = { .uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_handler };
    httpd_register_uri_handler(s_server, &logs_status_uri);
    httpd_register_uri_handler(s_server, &logs_uri);
    httpd_register_uri_handler(s_server, &reboot_uri);

    httpd_uri_t settings_get_uri = { .uri = "/api/settings", .method = HTTP_GET, .handler = settings_get_handler };
    httpd_uri_t settings_post_uri = { .uri = "/api/settings", .method = HTTP_POST, .handler = settings_post_handler };
    httpd_uri_t settings_patch_uri = { .uri = "/api/settings", .method = HTTP_PATCH, .handler = settings_patch_handler };
    httpd_register_uri_handler(s_server, &settings_get_uri);
    httpd_register_uri_handler(s_server, &settings_post_uri);
    httpd_register_uri_handler(s_server, &settings_patch_uri);

#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
    httpd_uri_t power_uri = { .uri = "/api/power", .method = HTTP_GET, .handler = power_handler };
    httpd_register_uri_handler(s_server, &power_uri);
    httpd_uri_t fan_uri = { .uri = "/api/fan", .method = HTTP_GET, .handler = fan_handler };
    httpd_register_uri_handler(s_server, &fan_uri);
#endif

    ESP_LOGI(TAG, "HTTP server started on port %d", 80);
    return ESP_OK;
}
