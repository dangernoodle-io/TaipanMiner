#include "ota_pull.h"
#include "cJSON.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "board.h"
#include "mining.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "ota_pull";

#define GITHUB_API_URL "https://api.github.com/repos/dangernoodle-io/TaipanMiner/releases/latest"
#define OTA_TASK_STACK 16384
#define OTA_TASK_PRIO  3
#define OTA_CHECK_STACK 8192
#define OTA_CHECK_PRIO 3
#define API_BUF_MAX    16384

static volatile bool s_ota_in_progress = false;

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR,
} ota_state_t;

static const char *s_ota_state_names[] = {
    [OTA_STATE_IDLE]               = "idle",
    [OTA_STATE_CHECKING]           = "checking",
    [OTA_STATE_DOWNLOADING]        = "downloading",
    [OTA_STATE_VERIFYING]          = "verifying",
    [OTA_STATE_COMPLETE]           = "complete",
    [OTA_STATE_ERROR]              = "error",
};

typedef struct {
    volatile ota_state_t state;
    char last_error[128];
    volatile int progress_pct;
} ota_status_t;

static ota_status_t s_ota_status = {
    .state = OTA_STATE_IDLE,
    .last_error = {0},
    .progress_pct = 0,
};

static void ota_set_error(const char *fmt, ...)
{
    s_ota_status.state = OTA_STATE_ERROR;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_ota_status.last_error, sizeof(s_ota_status.last_error), fmt, ap);
    va_end(ap);
}

typedef struct {
    char latest_tag[32];
    char asset_url[256];
    bool update_available;
} ota_pull_check_result_t;

static volatile bool s_check_in_progress = false;
static volatile bool s_check_done = false;
static ota_pull_check_result_t s_cached_check = {0};

#endif // ESP_PLATFORM

/**
 * Parse a GitHub releases/latest JSON response and extract the latest tag
 * and asset download URL for the given board.
 *
 * Platform-independent implementation, testable on host.
 */
int ota_pull_parse_release_json(const char *json, const char *board_name,
                                char *out_tag, size_t tag_size,
                                char *out_url, size_t url_size)
{
    if (!json || !board_name || !out_tag || !out_url) {
        return -1;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return -1;
    }

    // Extract tag_name
    cJSON *tag_item = cJSON_GetObjectItem(root, "tag_name");
    if (!tag_item || !tag_item->valuestring) {
        cJSON_Delete(root);
        return -1;
    }

    strncpy(out_tag, tag_item->valuestring, tag_size - 1);
    out_tag[tag_size - 1] = '\0';

    // Build expected asset name
    char asset_name[128];
    snprintf(asset_name, sizeof(asset_name), "taipanminer-%s.bin", board_name);

    // Iterate assets array
    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (!assets || !cJSON_IsArray(assets)) {
        cJSON_Delete(root);
        return -2;
    }

    cJSON *asset_item = NULL;
    cJSON_ArrayForEach(asset_item, assets) {
        cJSON *name_item = cJSON_GetObjectItem(asset_item, "name");
        if (name_item && name_item->valuestring &&
            strcmp(name_item->valuestring, asset_name) == 0) {
            // Found matching asset
            cJSON *url_item = cJSON_GetObjectItem(asset_item, "browser_download_url");
            if (url_item && url_item->valuestring) {
                strncpy(out_url, url_item->valuestring, url_size - 1);
                out_url[url_size - 1] = '\0';
                cJSON_Delete(root);
                return 0;
            }
        }
    }

    cJSON_Delete(root);
    return -2;
}

#ifdef ESP_PLATFORM

static esp_err_t ota_pull_check(ota_pull_check_result_t *result)
{
    char *buf = malloc(API_BUF_MAX);
    if (!buf) {
        ESP_LOGE(TAG, "failed to allocate response buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = GITHUB_API_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .user_agent = "TaipanMiner",
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http open failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "GitHub API returned %d", status);
        err = ESP_FAIL;
        goto cleanup;
    }

    int total = 0;
    int read_len;
    while (total < API_BUF_MAX - 1 &&
           (read_len = esp_http_client_read(client, buf + total,
                                            API_BUF_MAX - total - 1)) > 0) {
        total += read_len;
    }
    buf[total] = '\0';
    (void)content_length;

    if (total == 0) {
        ESP_LOGE(TAG, "empty response from GitHub API");
        err = ESP_FAIL;
        goto cleanup;
    }

    int parse_ret = ota_pull_parse_release_json(
        buf, BOARD_NAME,
        result->latest_tag, sizeof(result->latest_tag),
        result->asset_url, sizeof(result->asset_url));

    if (parse_ret != 0) {
        ESP_LOGE(TAG, "failed to parse release json: %d", parse_ret);
        err = ESP_FAIL;
        goto cleanup;
    }

    const esp_app_desc_t *running = esp_app_get_description();
    result->update_available = strncmp(running->version, "dev", 3) == 0 ||
                               strcmp(result->latest_tag, running->version) != 0;

    if (result->update_available) {
        ESP_LOGI(TAG, "update available: %s -> %s",
                 running->version, result->latest_tag);
    } else {
        ESP_LOGI(TAG, "already up to date: %s", result->latest_tag);
    }

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buf);
    return err;
}

/**
 * OTA worker task - performs the actual firmware update.
 */
static void ota_worker_task(void *arg)
{
    ota_pull_check_result_t result;
    if (arg) {
        memcpy(&result, arg, sizeof(ota_pull_check_result_t));
        free(arg);
    } else {
        vTaskDelete(NULL);
        return;
    }

    // Suspend mining task
    #ifdef ASIC_BM1370
    if (asic_task_handle) {
        vTaskSuspend(asic_task_handle);
    }
    #else
    if (mining_hw_task_handle) {
        vTaskSuspend(mining_hw_task_handle);
    }
    #endif

    ESP_LOGI(TAG, "starting OTA update from %s", result.asset_url);
    s_ota_status.state = OTA_STATE_DOWNLOADING;
    s_ota_status.progress_pct = 0;
    s_ota_status.last_error[0] = '\0';

    esp_http_client_config_t http_config = {
        .url = result.asset_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
        .user_agent = "TaipanMiner",
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    // Verify OTA partition exists before attempting
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "no OTA update partition found");
        ota_set_error("no OTA update partition (check partition table)");
        goto resume_and_exit;
    }
    ESP_LOGI(TAG, "OTA target partition: %s", update_partition->label);

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        ota_set_error("ota_begin: %s", esp_err_to_name(err));
        goto resume_and_exit;
    }

    esp_app_desc_t img_desc;
    err = esp_https_ota_get_img_desc(ota_handle, &img_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed: %s", esp_err_to_name(err));
        ota_set_error("get_img_desc: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        goto resume_and_exit;
    }

    const esp_app_desc_t *running = esp_app_get_description();
    if (strncmp(img_desc.project_name, running->project_name,
                sizeof(img_desc.project_name)) != 0) {
        ESP_LOGE(TAG, "board mismatch: got '%s', expected '%s'",
                 img_desc.project_name, running->project_name);
        ota_set_error("board mismatch: got '%s', expected '%s'",
                      img_desc.project_name, running->project_name);
        esp_https_ota_abort(ota_handle);
        goto resume_and_exit;
    }

    int image_size = esp_https_ota_get_image_size(ota_handle);

    while (true) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        if (image_size > 0) {
            int read_so_far = esp_https_ota_get_image_len_read(ota_handle);
            s_ota_status.progress_pct = (read_so_far * 100) / image_size;
        }
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "incomplete OTA data received");
        ota_set_error("incomplete OTA data received");
        esp_https_ota_abort(ota_handle);
        goto resume_and_exit;
    }

    s_ota_status.state = OTA_STATE_VERIFYING;
    s_ota_status.progress_pct = 100;

    err = esp_https_ota_finish(ota_handle);
    if (err == ESP_OK) {
        s_ota_status.state = OTA_STATE_COMPLETE;
        ESP_LOGI(TAG, "OTA complete, rebooting to %s", result.latest_tag);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
    ota_set_error("esp_https_ota_finish: %s", esp_err_to_name(err));

resume_and_exit:
    s_ota_in_progress = false;

    // Resume mining task
    #ifdef ASIC_BM1370
    if (asic_task_handle) {
        vTaskResume(asic_task_handle);
    }
    #else
    if (mining_hw_task_handle) {
        vTaskResume(mining_hw_task_handle);
    }
    #endif

    vTaskDelete(NULL);
}

/**
 * OTA check worker task - performs the version check in background.
 */
static void ota_check_worker_task(void *arg)
{
    ota_pull_check_result_t result = {0};
    esp_err_t err = ota_pull_check(&result);

    if (err == ESP_OK) {
        memcpy(&s_cached_check, &result, sizeof(ota_pull_check_result_t));
        s_check_done = true;
        ESP_LOGI(TAG, "background check completed");
    } else {
        ESP_LOGE(TAG, "background check failed");
        s_check_done = false;
    }

    s_check_in_progress = false;
    vTaskDelete(NULL);
}

/**
 * GET /api/ota/check - Check for available updates (non-blocking)
 */
static esp_err_t ota_check_handler(httpd_req_t *req)
{
    // If we have a cached result, return it
    if (s_check_done) {
        cJSON *root = cJSON_CreateObject();
        if (!root) {
            const char *error_response = "{\"error\":\"json_error\"}";
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, error_response, strlen(error_response));
            return ESP_OK;
        }

        const esp_app_desc_t *running_desc = esp_app_get_description();
        if (running_desc) {
            cJSON_AddStringToObject(root, "current_version", running_desc->version);
        }
        cJSON_AddStringToObject(root, "latest_version", s_cached_check.latest_tag);
        cJSON_AddBoolToObject(root, "update_available", s_cached_check.update_available);

        char asset_name[128];
        snprintf(asset_name, sizeof(asset_name), "taipanminer-%s.bin", BOARD_NAME);
        cJSON_AddStringToObject(root, "asset", asset_name);

        char *response_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (response_str) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response_str, strlen(response_str));
            free(response_str);
        } else {
            const char *error_response = "{\"error\":\"json_error\"}";
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, error_response, strlen(error_response));
        }

        // Invalidate cache so next request triggers a fresh check
        s_check_done = false;
        return ESP_OK;
    }

    // Trigger background check if not already running
    if (!s_check_in_progress) {
        s_check_in_progress = true;
        BaseType_t task_result = xTaskCreate(
            ota_check_worker_task,
            "ota_chk",
            OTA_CHECK_STACK,
            NULL,
            OTA_CHECK_PRIO,
            NULL
        );

        if (task_result != pdPASS) {
            s_check_in_progress = false;
            const char *response = "{\"error\":\"task_create_failed\"}";
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, strlen(response));
            return ESP_OK;
        }
    }

    const char *response = "{\"status\":\"checking\"}";
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

/**
 * POST /api/ota/update - Trigger firmware update
 */
static esp_err_t ota_update_handler(httpd_req_t *req)
{
    if (s_ota_in_progress) {
        const char *response = "{\"error\":\"update_in_progress\"}";
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }

    ota_pull_check_result_t result = {0};
    esp_err_t err = ota_pull_check(&result);

    if (err != ESP_OK) {
        const char *response = "{\"error\":\"check_failed\"}";
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }

    if (!result.update_available) {
        const char *response = "{\"status\":\"already_up_to_date\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }

    // Allocate task argument
    ota_pull_check_result_t *task_arg = malloc(sizeof(ota_pull_check_result_t));
    if (!task_arg) {
        const char *response = "{\"error\":\"allocation_failed\"}";
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }

    memcpy(task_arg, &result, sizeof(ota_pull_check_result_t));
    s_ota_in_progress = true;
    s_ota_status.state = OTA_STATE_CHECKING;
    s_ota_status.progress_pct = 0;
    s_ota_status.last_error[0] = '\0';

    TaskHandle_t task_handle = NULL;
    BaseType_t task_result = xTaskCreate(
        ota_worker_task,
        "ota_pull",
        OTA_TASK_STACK,
        task_arg,
        OTA_TASK_PRIO,
        &task_handle
    );

    if (task_result != pdPASS) {
        free(task_arg);
        s_ota_in_progress = false;
        const char *response = "{\"error\":\"task_create_failed\"}";
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }

    const char *response = "{\"status\":\"update_started\"}";
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

/**
 * GET /api/ota/status - Return OTA debug status
 */
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        const char *response = "{\"error\":\"json_error\"}";
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }

    cJSON_AddStringToObject(root, "state", s_ota_state_names[s_ota_status.state]);
    cJSON_AddBoolToObject(root, "in_progress", s_ota_in_progress);
    cJSON_AddNumberToObject(root, "progress_pct", s_ota_status.progress_pct);
    if (s_ota_status.last_error[0] != '\0') {
        cJSON_AddStringToObject(root, "last_error", s_ota_status.last_error);
    }

    char *response_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (response_str) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response_str, strlen(response_str));
        free(response_str);
    } else {
        const char *response = "{\"error\":\"json_error\"}";
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, response, strlen(response));
    }

    return ESP_OK;
}

/**
 * Register OTA pull HTTP handlers with an existing httpd instance.
 */
esp_err_t ota_pull_register_handler(httpd_handle_t server)
{
    if (!server) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_uri_t check_uri = {
        .uri = "/api/ota/check",
        .method = HTTP_GET,
        .handler = ota_check_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t update_uri = {
        .uri = "/api/ota/update",
        .method = HTTP_POST,
        .handler = ota_update_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t status_uri = {
        .uri = "/api/ota/status",
        .method = HTTP_GET,
        .handler = ota_status_handler,
        .user_ctx = NULL,
    };

    esp_err_t err = httpd_register_uri_handler(server, &check_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to register /api/ota/check handler: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(server, &update_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to register /api/ota/update handler: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(server, &status_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to register /api/ota/status handler: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA pull handlers registered");
    return ESP_OK;
}

#endif // ESP_PLATFORM
