#include "http_server.h"
#include "taipan_web.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "http";
static httpd_handle_t s_server = NULL;

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

    // Register preflight handler
    err = taipan_web_register_preflight(s_server);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t http_server_start_prov(void)
{
    esp_err_t err = ensure_server_started();
    if (err != ESP_OK) return err;

    err = taipan_web_register_prov_routes(s_server);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "provisioning server started on port 80");
    return ESP_OK;
}

void http_server_switch_to_mining(void)
{
    if (!s_server) return;

    taipan_web_unregister_prov_routes(s_server);
    taipan_web_register_mining_routes(s_server);
}

esp_err_t http_server_start(void)
{
    esp_err_t err = ensure_server_started();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    err = taipan_web_register_mining_routes(s_server);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "HTTP server started on port %d", 80);
    return ESP_OK;
}
