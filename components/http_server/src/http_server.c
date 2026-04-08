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
#include "board.h"
#include "mining.h"
#include "nv_config.h"
#include "wifi_prov.h"
#include "cJSON.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ota_pull.h"

static const char *TAG = "http";
static httpd_handle_t s_server = NULL;

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

static esp_err_t preflight_handler(httpd_req_t *req);

static esp_err_t ensure_server_started(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;
    config.stack_size = 6144;
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

    esp_err_t err = nv_config_set_wifi(ssid, pass);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    err = nv_config_set_config(pool_host, port, wallet, worker, pool_pass);
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
#ifdef ASIC_BM1370
    double asic_rate = 0, asic_ema = 0;
    uint32_t asic_shares = 0;
    float asic_temp = 0;
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
#ifdef ASIC_BM1370
        asic_rate = mining_stats.asic_hashrate;
        asic_ema = mining_stats.asic_ema.value;
        asic_shares = mining_stats.asic_shares;
        asic_temp = mining_stats.asic_temp_c;
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
    cJSON_AddStringToObject(root, "pool_host", nv_config_pool_host());
    cJSON_AddNumberToObject(root, "pool_port", nv_config_pool_port());
    cJSON_AddStringToObject(root, "worker", nv_config_worker_name());
    cJSON_AddStringToObject(root, "wallet", nv_config_wallet_addr());
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "build_date", app->date);
    cJSON_AddStringToObject(root, "build_time", app->time);
#ifdef ASIC_BM1370
    cJSON_AddNumberToObject(root, "asic_hashrate", asic_rate);
    cJSON_AddNumberToObject(root, "asic_hashrate_avg", asic_ema);
    cJSON_AddNumberToObject(root, "asic_shares", asic_shares);
    cJSON_AddNumberToObject(root, "asic_temp_c", (double)asic_temp);
#endif

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

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
    cJSON_AddStringToObject(root, "worker_name", nv_config_worker_name());
    cJSON_AddStringToObject(root, "ssid", nv_config_wifi_ssid());

    cJSON_AddNumberToObject(root, "total_heap", (double)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "free_heap", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    cJSON_AddNumberToObject(root, "flash_size", (double)flash_size);

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        cJSON_AddNumberToObject(root, "app_size", (double)running->size);
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
    httpd_resp_set_status(req, "204 No Content");
    set_common_headers(req);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    set_common_headers(req);
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA started, receiving %d bytes to partition '%s' at 0x%"PRIx32,
             req->content_len, partition->label, partition->address);

    char buf[1024];
    int received = 0;
    int timeout_count = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf, sizeof(buf) < (req->content_len - received) ? sizeof(buf) : (req->content_len - received));
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeout_count > 30) {  // 30 consecutive timeouts ~ 30s
                ESP_LOGE(TAG, "OTA upload timeout after %d retries", timeout_count);
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload timeout");
                return ESP_FAIL;
            }
            continue;
        }
        timeout_count = 0;  // reset on successful recv
        if (ret <= 0) {
            ESP_LOGE(TAG, "OTA receive error at %d/%d", received, req->content_len);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        if (received == 0 && ret >= (int)(sizeof(esp_image_header_t) +
            sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))) {
            const esp_app_desc_t *incoming = (const esp_app_desc_t *)
                (buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
            const esp_app_desc_t *running = esp_app_get_description();

            if (strncmp(incoming->project_name, running->project_name,
                        sizeof(incoming->project_name)) != 0) {
                ESP_LOGE(TAG, "OTA rejected: firmware is for '%s', this device is '%s'",
                         incoming->project_name, running->project_name);
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware board mismatch");
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, "OTA board check passed: %s", incoming->project_name);
        }

        err = esp_ota_write(ota_handle, buf, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        received += ret;
    }

    ESP_LOGI(TAG, "OTA receive complete (%d bytes), validating", received);

    // Suspend mining tasks — HW SHA mining conflicts with image verification
#ifdef ASIC_BM1370
    if (asic_task_handle) vTaskSuspend(asic_task_handle);
#else
    if (mining_hw_task_handle) vTaskSuspend(mining_hw_task_handle);
#endif

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s (0x%x)", esp_err_to_name(err), err);
        // Resume mining tasks on OTA failure
#ifdef ASIC_BM1370
        if (asic_task_handle) vTaskResume(asic_task_handle);
#else
        if (mining_hw_task_handle) vTaskResume(mining_hw_task_handle);
#endif
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        // Resume mining tasks on set_boot_partition failure
#ifdef ASIC_BM1370
        if (asic_task_handle) vTaskResume(asic_task_handle);
#else
        if (mining_hw_task_handle) vTaskResume(mining_hw_task_handle);
#endif
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete, rebooting");
    httpd_resp_sendstr(req, "OTA complete. Rebooting...");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;  // unreachable
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
    // Unregister prov handlers
    httpd_unregister_uri_handler(s_server, "/", HTTP_GET);
    httpd_unregister_uri_handler(s_server, "/save", HTTP_POST);
    httpd_unregister_uri_handler(s_server, "/api/scan", HTTP_GET);
    httpd_unregister_uri_handler(s_server, "/*", HTTP_GET);

    // Register mining handlers
    httpd_uri_t status_uri = { .uri = "/", .method = HTTP_GET, .handler = status_handler };
    httpd_uri_t stats_uri = { .uri = "/api/stats", .method = HTTP_GET, .handler = stats_handler };
    httpd_uri_t mining_js_uri = { .uri = "/mining.js", .method = HTTP_GET, .handler = mining_js_handler };
    httpd_uri_t ota_upload_uri = { .uri = "/ota/upload", .method = HTTP_POST, .handler = ota_upload_handler };

    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &stats_uri);
    httpd_register_uri_handler(s_server, &mining_js_uri);
    httpd_register_uri_handler(s_server, &ota_upload_uri);
    ota_pull_register_handler(s_server);
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
    httpd_uri_t ota_upload_uri = {
        .uri = "/ota/upload", .method = HTTP_POST, .handler = ota_upload_handler
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
    httpd_register_uri_handler(s_server, &ota_upload_uri);
    httpd_register_uri_handler(s_server, &theme_uri);
    httpd_register_uri_handler(s_server, &logo_uri);
    ota_pull_register_handler(s_server);

    ESP_LOGI(TAG, "HTTP server started on port %d", 80);
    return ESP_OK;
}
