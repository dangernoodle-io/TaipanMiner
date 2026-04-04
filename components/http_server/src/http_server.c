#include "http_server.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mining.h"
#include "nv_config.h"
#include "wifi_prov.h"
#include "cJSON.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "http";
static int64_t s_start_time;
static httpd_handle_t s_server = NULL;

extern const char prov_form_html[];
extern const unsigned int prov_form_html_len;
extern const char theme_css[];
extern const unsigned int theme_css_len;
extern const char logo_svg[];
extern const unsigned int logo_svg_len;

static esp_err_t ensure_server_started(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 6;
    config.max_uri_handlers = 12;
    config.stack_size = 6144;
    config.uri_match_fn = httpd_uri_match_wildcard;
    return httpd_start(&s_server, &config);
}

// Extract a field from URL-encoded body: "field=value&..."
// Handles %XX decoding and + as space
static void url_decode_field(const char *body, const char *field, char *out, size_t out_size)
{
    out[0] = '\0';
    // Find "field=" in body
    char key[64];
    snprintf(key, sizeof(key), "%s=", field);
    const char *start = strstr(body, key);
    if (!start) return;
    start += strlen(key);
    const char *end = strchr(start, '&');
    if (!end) end = start + strlen(start);

    size_t i = 0;
    while (start < end && i < out_size - 1) {
        if (*start == '+') {
            out[i++] = ' ';
            start++;
        } else if (*start == '%' && start + 2 < end) {
            char hex[3] = { start[1], start[2], '\0' };
            out[i++] = (char)strtoul(hex, NULL, 16);
            start += 3;
        } else {
            out[i++] = *start++;
        }
    }
    out[i] = '\0';
}

static esp_err_t prov_form_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, prov_form_html, prov_form_html_len);
    return ESP_OK;
}

// This function has been replaced by the embedded HTML above
static esp_err_t prov_save_handler(httpd_req_t *req)
{
    char body[512];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    // Parse URL-encoded fields
    char ssid[32] = "", pass[64] = "";
    char pool_host[64] = "", wallet[64] = "", worker[32] = "";
    char port_str[8] = "";

    url_decode_field(body, "ssid", ssid, sizeof(ssid));
    url_decode_field(body, "pass", pass, sizeof(pass));
    url_decode_field(body, "pool_host", pool_host, sizeof(pool_host));
    url_decode_field(body, "pool_port", port_str, sizeof(port_str));
    url_decode_field(body, "wallet", wallet, sizeof(wallet));
    url_decode_field(body, "worker", worker, sizeof(worker));

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

    nv_config_set_config(pool_host, port, wallet, worker);

    const char *response =
        "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>TaipanMiner Setup</title>"
        "<link rel='stylesheet' href='/theme.css'>"
        "</head><body><div class='container'>"
        "<div class='header'><div id='logo'></div><h1>TaipanMiner</h1></div>"
        "<div class='card' style='text-align:center'>"
        "<h2>Configuration Saved</h2><div class='spinner'></div>"
        "<p style='margin-top:20px;color:var(--label)'>Connecting to WiFi...</p>"
        "</div><div class='footer'>Powered by TaipanMiner <span id='ver'></span></div>"
        "</div><script>"
        "fetch('/logo.svg').then(function(r){return r.text()}).then(function(s){document.getElementById('logo').innerHTML=s});"
        "fetch('/api/version').then(function(r){return r.text()}).then(function(v){document.getElementById('ver').textContent='v'+v})"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, strlen(response));

    // Signal provisioning complete
    extern EventGroupHandle_t g_prov_event_group;
    xEventGroupSetBits(g_prov_event_group, BIT0);

    return ESP_OK;
}

static esp_err_t prov_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    double hw_rate = 0, sw_rate = 0;
    uint32_t hw_shares = 0, sw_shares = 0;

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        hw_rate = mining_stats.hw_hashrate;
        sw_rate = mining_stats.sw_hashrate;
        hw_shares = mining_stats.hw_shares;
        sw_shares = mining_stats.sw_shares;
        xSemaphoreGive(mining_stats.mutex);
    }

    const esp_app_desc_t *app = esp_app_get_description();
    int64_t uptime_s = (esp_timer_get_time() - s_start_time) / 1000000;

    char buf[640];
    int len = snprintf(buf, sizeof(buf),
        "<html><body>"
        "<h2>TaipanMiner %s</h2>"
        "<p>Build: %s %s</p>"
        "<p>Pool: %s:%u</p>"
        "<p>Worker: %s.%s</p>"
        "<p>Hashrate: %.1f kH/s (hw: %.1f / sw: %.1f)</p>"
        "<p>Shares: %"PRIu32" (hw: %"PRIu32" / sw: %"PRIu32")</p>"
        "<p>Uptime: %llds</p>"
        "<p><a href=\"/ota\">OTA Update</a></p>"
        "</body></html>",
        app->version,
        app->date, app->time,
        nv_config_pool_host(), nv_config_pool_port(),
        nv_config_wallet_addr(), nv_config_worker_name(),
        (hw_rate + sw_rate) / 1000.0, hw_rate / 1000.0, sw_rate / 1000.0,
        hw_shares + sw_shares, hw_shares, sw_shares,
        (long long)uptime_s);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t stats_handler(httpd_req_t *req)
{
    double hw_rate = 0, sw_rate = 0;
    uint32_t hw_shares = 0, sw_shares = 0;

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        hw_rate = mining_stats.hw_hashrate;
        sw_rate = mining_stats.sw_hashrate;
        hw_shares = mining_stats.hw_shares;
        sw_shares = mining_stats.sw_shares;
        xSemaphoreGive(mining_stats.mutex);
    }

    const esp_app_desc_t *app = esp_app_get_description();
    int64_t uptime_s = (esp_timer_get_time() - s_start_time) / 1000000;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "hw_hashrate", hw_rate);
    cJSON_AddNumberToObject(root, "sw_hashrate", sw_rate);
    cJSON_AddNumberToObject(root, "total_hashrate", hw_rate + sw_rate);
    cJSON_AddNumberToObject(root, "hw_shares", hw_shares);
    cJSON_AddNumberToObject(root, "sw_shares", sw_shares);
    cJSON_AddNumberToObject(root, "total_shares", hw_shares + sw_shares);
    cJSON_AddStringToObject(root, "pool_host", nv_config_pool_host());
    cJSON_AddNumberToObject(root, "pool_port", nv_config_pool_port());
    cJSON_AddStringToObject(root, "worker", nv_config_worker_name());
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "build_date", app->date);
    cJSON_AddStringToObject(root, "build_time", app->time);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t version_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, app->version, strlen(app->version));
    return ESP_OK;
}

static esp_err_t logo_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_send(req, logo_svg, logo_svg_len);
    return ESP_OK;
}

static esp_err_t theme_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, theme_css, theme_css_len);
    return ESP_OK;
}

static esp_err_t ota_page_handler(httpd_req_t *req)
{
    const char *html =
        "<html><body>"
        "<h2>OTA Firmware Update</h2>"
        "<p>Upload firmware via curl:</p>"
        "<pre>curl -X POST http://&lt;device-ip&gt;/ota/upload "
        "--data-binary @firmware.bin</pre>"
        "<p><a href=\"/\">Back</a></p>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
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
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf, sizeof(buf) < (req->content_len - received) ? sizeof(buf) : (req->content_len - received));
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            ESP_LOGE(TAG, "OTA receive error at %d/%d", received, req->content_len);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
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
    if (mining_sw_task_handle) vTaskSuspend(mining_sw_task_handle);
#endif

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s (0x%x)", esp_err_to_name(err), err);
        // Resume mining tasks on OTA failure
#ifdef ASIC_BM1370
        if (asic_task_handle) vTaskResume(asic_task_handle);
#else
        if (mining_hw_task_handle) vTaskResume(mining_hw_task_handle);
        if (mining_sw_task_handle) vTaskResume(mining_sw_task_handle);
#endif
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
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
    wifi_scan_ap_t aps[WIFI_SCAN_MAX];
    memset(aps, 0, sizeof(aps));
    int count = wifi_scan_networks(aps, WIFI_SCAN_MAX);

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
    httpd_uri_t theme_uri = { .uri = "/theme.css", .method = HTTP_GET, .handler = theme_handler };
    httpd_uri_t logo_uri = { .uri = "/logo.svg", .method = HTTP_GET, .handler = logo_handler };
    httpd_uri_t prov_redirect = { .uri = "/*", .method = HTTP_GET, .handler = prov_redirect_handler };

    httpd_register_uri_handler(s_server, &prov_form);
    httpd_register_uri_handler(s_server, &prov_save);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &version_uri);
    httpd_register_uri_handler(s_server, &theme_uri);
    httpd_register_uri_handler(s_server, &logo_uri);
    httpd_register_uri_handler(s_server, &prov_redirect);

    ESP_LOGI(TAG, "provisioning server started on port 80");
    return ESP_OK;
}

void http_server_switch_to_mining(void)
{
    s_start_time = esp_timer_get_time();

    // Unregister prov handlers
    httpd_unregister_uri_handler(s_server, "/", HTTP_GET);
    httpd_unregister_uri_handler(s_server, "/save", HTTP_POST);
    httpd_unregister_uri_handler(s_server, "/api/scan", HTTP_GET);
    httpd_unregister_uri_handler(s_server, "/*", HTTP_GET);

    // Register mining handlers
    httpd_uri_t status_uri = { .uri = "/", .method = HTTP_GET, .handler = status_handler };
    httpd_uri_t stats_uri = { .uri = "/api/stats", .method = HTTP_GET, .handler = stats_handler };
    httpd_uri_t version_uri = { .uri = "/api/version", .method = HTTP_GET, .handler = version_handler };
    httpd_uri_t ota_page_uri = { .uri = "/ota", .method = HTTP_GET, .handler = ota_page_handler };
    httpd_uri_t ota_upload_uri = { .uri = "/ota/upload", .method = HTTP_POST, .handler = ota_upload_handler };

    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &stats_uri);
    httpd_register_uri_handler(s_server, &version_uri);
    httpd_register_uri_handler(s_server, &ota_page_uri);
    httpd_register_uri_handler(s_server, &ota_upload_uri);
}

esp_err_t http_server_start(void)
{
    s_start_time = esp_timer_get_time();

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
    httpd_uri_t ota_page_uri = {
        .uri = "/ota", .method = HTTP_GET, .handler = ota_page_handler
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

    httpd_register_uri_handler(s_server, &status_uri);
    httpd_register_uri_handler(s_server, &stats_uri);
    httpd_register_uri_handler(s_server, &version_uri);
    httpd_register_uri_handler(s_server, &ota_page_uri);
    httpd_register_uri_handler(s_server, &ota_upload_uri);
    httpd_register_uri_handler(s_server, &theme_uri);
    httpd_register_uri_handler(s_server, &logo_uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", 80);
    return ESP_OK;
}
