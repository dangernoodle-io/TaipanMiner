#include "nv_config.h"
#include <string.h>

#define NVS_NAMESPACE "taipanminer"

static struct {
    char wifi_ssid[32];
    char wifi_pass[64];
    char pool_host[64];
    uint16_t pool_port;
    char wallet_addr[64];
    char worker_name[32];
} s_config;

// Helper to load a string from NVS with fallback (ESP only)
#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#if __has_include("config.h")
#include "config.h"
#endif

static const char *TAG = "nv_config";

static void load_str(nvs_handle_t handle, const char *key, char *buf, size_t buf_size, const char *fallback)
{
    size_t len = buf_size;
    if (nvs_get_str(handle, key, buf, &len) != ESP_OK) {
        strlcpy(buf, fallback, buf_size);
    }
}
#endif

esp_err_t nv_config_init(void)
{
#ifdef ESP_PLATFORM
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no config in NVS");
        memset(&s_config, 0, sizeof(s_config));
#ifdef CONFIG_TEST_POOL_HOST
        strlcpy(s_config.wifi_ssid, CONFIG_TEST_WIFI_SSID, sizeof(s_config.wifi_ssid));
        strlcpy(s_config.wifi_pass, CONFIG_TEST_WIFI_PASS, sizeof(s_config.wifi_pass));
        strlcpy(s_config.pool_host, CONFIG_TEST_POOL_HOST, sizeof(s_config.pool_host));
        s_config.pool_port = CONFIG_TEST_POOL_PORT;
        strlcpy(s_config.wallet_addr, CONFIG_TEST_WALLET, sizeof(s_config.wallet_addr));
        strlcpy(s_config.worker_name, CONFIG_TEST_WORKER, sizeof(s_config.worker_name));
        ESP_LOGI(TAG, "test config loaded from config.h");
#endif
        return ESP_OK;
    }

    if (err != ESP_OK) {
        return err;
    }

    load_str(handle, "wifi_ssid", s_config.wifi_ssid, sizeof(s_config.wifi_ssid), "");
    load_str(handle, "wifi_pass", s_config.wifi_pass, sizeof(s_config.wifi_pass), "");
    load_str(handle, "pool_host", s_config.pool_host, sizeof(s_config.pool_host), "");
    load_str(handle, "wallet_addr", s_config.wallet_addr, sizeof(s_config.wallet_addr), "");
    load_str(handle, "worker", s_config.worker_name, sizeof(s_config.worker_name), "");

    if (nvs_get_u16(handle, "pool_port", &s_config.pool_port) != ESP_OK) {
        s_config.pool_port = 0;
    }

    nvs_close(handle);

#ifdef CONFIG_TEST_POOL_HOST
    if (s_config.pool_host[0] == '\0') {
        strlcpy(s_config.wifi_ssid, CONFIG_TEST_WIFI_SSID, sizeof(s_config.wifi_ssid));
        strlcpy(s_config.wifi_pass, CONFIG_TEST_WIFI_PASS, sizeof(s_config.wifi_pass));
        strlcpy(s_config.pool_host, CONFIG_TEST_POOL_HOST, sizeof(s_config.pool_host));
        s_config.pool_port = CONFIG_TEST_POOL_PORT;
        strlcpy(s_config.wallet_addr, CONFIG_TEST_WALLET, sizeof(s_config.wallet_addr));
        strlcpy(s_config.worker_name, CONFIG_TEST_WORKER, sizeof(s_config.worker_name));
        ESP_LOGI(TAG, "test config loaded from config.h (NVS pool_host empty)");
    }
#endif

    ESP_LOGI(TAG, "config loaded (pool=%s:%u worker=%s.%s)",
             s_config.pool_host, s_config.pool_port,
             s_config.wallet_addr, s_config.worker_name);
#else
    // Native build: no NVS, all fields empty/zero
    memset(&s_config, 0, sizeof(s_config));
#endif
    return ESP_OK;
}

#ifdef ESP_PLATFORM
bool nv_config_is_provisioned(void)
{
#ifdef CONFIG_TEST_POOL_HOST
    if (s_config.pool_host[0] != '\0') return true;
#endif

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if (err != ESP_OK) {
        return false;
    }

    uint8_t value = 0;
    err = nvs_get_u8(handle, "provisioned", &value);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }

    return value == 1;
}

esp_err_t nv_config_set_provisioned(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, "provisioned", 1);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return err;
}

esp_err_t nv_config_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, "wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "wifi_pass", pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        strncpy(s_config.wifi_ssid, ssid, sizeof(s_config.wifi_ssid) - 1);
        s_config.wifi_ssid[sizeof(s_config.wifi_ssid) - 1] = '\0';
        strncpy(s_config.wifi_pass, pass, sizeof(s_config.wifi_pass) - 1);
        s_config.wifi_pass[sizeof(s_config.wifi_pass) - 1] = '\0';
    }

    return err;
}

esp_err_t nv_config_set_config(const char *pool_host, uint16_t pool_port,
                                const char *wallet_addr, const char *worker_name)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, "pool_host", pool_host);
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, "pool_port", pool_port);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "wallet_addr", wallet_addr);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "worker", worker_name);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        strncpy(s_config.pool_host, pool_host, sizeof(s_config.pool_host) - 1);
        s_config.pool_host[sizeof(s_config.pool_host) - 1] = '\0';
        s_config.pool_port = pool_port;
        strncpy(s_config.wallet_addr, wallet_addr, sizeof(s_config.wallet_addr) - 1);
        s_config.wallet_addr[sizeof(s_config.wallet_addr) - 1] = '\0';
        strncpy(s_config.worker_name, worker_name, sizeof(s_config.worker_name) - 1);
        s_config.worker_name[sizeof(s_config.worker_name) - 1] = '\0';
    }

    return err;
}
#endif

const char *nv_config_wifi_ssid(void) { return s_config.wifi_ssid; }
const char *nv_config_wifi_pass(void) { return s_config.wifi_pass; }
const char *nv_config_pool_host(void) { return s_config.pool_host; }
uint16_t nv_config_pool_port(void) { return s_config.pool_port; }
const char *nv_config_wallet_addr(void) { return s_config.wallet_addr; }
const char *nv_config_worker_name(void) { return s_config.worker_name; }
