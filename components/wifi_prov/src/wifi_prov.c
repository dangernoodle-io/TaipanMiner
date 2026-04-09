#include "wifi_prov.h"
#include "nv_config.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#ifdef ESP_PLATFORM
#include "mdns.h"
#include "esp_app_desc.h"
#include "board.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_timer.h"

static const char *TAG = "wifi_prov";
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_MAX_RETRY 10

// State tracking
static bool s_netif_initialized = false;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static volatile bool s_dns_running = false;
static TaskHandle_t s_dns_task_handle = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_count = 0;
static char s_ap_ssid[32];
static esp_event_handler_instance_t s_wifi_handler = NULL;
static esp_event_handler_instance_t s_ip_handler = NULL;
static esp_timer_handle_t s_reconnect_timer = NULL;
#ifdef ESP_PLATFORM
static bool s_mdns_started = false;
#endif

// Public event group for provisioning
EventGroupHandle_t g_prov_event_group = NULL;

// WiFi scan cache
static wifi_scan_ap_t s_cached_scan[WIFI_SCAN_MAX];
static int s_cached_scan_count = 0;
static volatile bool s_scan_in_progress = false;

#ifdef ESP_PLATFORM
static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void mdns_build_hostname(char *out, size_t out_size)
{
    const char *worker = nv_config_worker_name();
    char sanitized[64] = "";

    if (worker && worker[0] != '\0') {
        size_t i = 0, j = 0;
        bool last_was_hyphen = false;
        while (worker[i] && j < sizeof(sanitized) - 1) {
            char c = worker[i++];
            if (c >= 'A' && c <= 'Z') {
                c += 32;
            }
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                sanitized[j++] = c;
                last_was_hyphen = false;
            } else if (!last_was_hyphen && j > 0) {
                sanitized[j++] = '-';
                last_was_hyphen = true;
            }
        }
        while (j > 0 && sanitized[j - 1] == '-') {
            j--;
        }
        sanitized[j] = '\0';
    }

    if (sanitized[0] != '\0') {
        snprintf(out, out_size, "taipanminer-%s", sanitized);
    } else {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(out, out_size, "taipanminer-%02x%02x", mac[4], mac[5]);
    }

    /* mDNS label max 63 chars */
    out[63] = '\0';
}

static void mdns_start(void)
{
    if (s_mdns_started) {
        return;
    }

    char hostname[64];
    mdns_build_hostname(hostname, sizeof(hostname));

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_hostname_set failed: %s", esp_err_to_name(err));
        return;
    }
    err = mdns_instance_name_set("TaipanMiner");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_instance_name_set failed: %s", esp_err_to_name(err));
        return;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    mdns_txt_item_t txt[] = {
        {"board",   BOARD_NAME},
        {"version", app->version},
        {"mac",     mac_str},
    };

    err = mdns_service_add(NULL, "_taipanminer", "_tcp", 80, txt, 3);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_service_add failed: %s", esp_err_to_name(err));
        return;
    }
    s_mdns_started = true;

    ESP_LOGI(TAG, "mDNS started: %s.local (_taipanminer._tcp)", hostname);
}
#endif /* ESP_PLATFORM */

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count >= WIFI_MAX_RETRY) {
            ESP_LOGW(TAG, "max retries reached, delaying 5s before retry");
            s_retry_count = 0;
            if (!s_reconnect_timer) {
                const esp_timer_create_args_t args = {
                    .callback = reconnect_timer_cb,
                    .name = "wifi_reconn",
                };
                esp_timer_create(&args, &s_reconnect_timer);
            }
            esp_timer_start_once(s_reconnect_timer, 5000000);  // 5s in microseconds
            return;  // don't call esp_wifi_connect() now
        }
        esp_wifi_connect();
        s_retry_count++;
        ESP_LOGW(TAG, "retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        nv_config_reset_boot_count();
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t wifi_connect_sta(bool restart_on_timeout)
{
    s_wifi_event_group = xEventGroupCreate();

    // Initialize netif and event loop (idempotent, guarded by flag)
    if (!s_netif_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_netif_initialized = true;
    }

    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (!s_wifi_handler) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &s_wifi_handler));
    }
    if (!s_ip_handler) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &s_ip_handler));
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, nv_config_wifi_ssid(), sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, nv_config_wifi_pass(), sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to %s", nv_config_wifi_ssid());
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));

    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;

        // Clean up WiFi so it can be reinitialized
        esp_wifi_stop();
        esp_wifi_deinit();

        // Unregister event handlers and clear handles for fresh registration on retry
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_wifi_handler = NULL;
        s_ip_handler = NULL;

        if (restart_on_timeout) {
            ESP_LOGE(TAG, "WiFi connection timeout after 60s, restarting");
            esp_restart();
        } else {
            ESP_LOGE(TAG, "WiFi connection timeout after 60s");
            return ESP_ERR_TIMEOUT;
        }
    }

    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;

#ifdef ESP_PLATFORM
    mdns_start();
#endif

    return ESP_OK;
}

esp_err_t wifi_init(void)
{
    return wifi_connect_sta(true);
}

esp_err_t wifi_init_sta(void)
{
    return wifi_connect_sta(false);
}

// Captive DNS task - responds to all DNS queries with 192.168.4.1
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "failed to create DNS socket");
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "failed to bind DNS socket");
        close(sock);
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Set receive timeout so we can check s_dns_running
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "captive DNS listening on 0.0.0.0:53");

    uint8_t buf[256];
    while (s_dns_running) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client, &client_len);

        if (len < 12) {
            // Too short for DNS header or timeout
            continue;
        }

        // Build response: copy query, set response flags, add A record pointing to 192.168.4.1
        uint8_t resp[256];
        memcpy(resp, buf, len);
        resp[2] = 0x81;  // QR=1 (response)
        resp[3] = 0x80;  // AA=1 (authoritative), no error

        resp[6] = 0;     // ANCOUNT high byte
        resp[7] = 1;     // ANCOUNT = 1 answer

        // Append answer: name pointer + type A + class IN + TTL + rdlength + IP
        int pos = len;
        resp[pos++] = 0xC0;  // pointer to question name
        resp[pos++] = 0x0C;
        resp[pos++] = 0x00;  // type A
        resp[pos++] = 0x01;
        resp[pos++] = 0x00;  // class IN
        resp[pos++] = 0x01;
        resp[pos++] = 0x00;  // TTL 60 seconds
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 0x3C;
        resp[pos++] = 0x00;  // rdlength 4
        resp[pos++] = 0x04;
        resp[pos++] = 192;   // 192.168.4.1
        resp[pos++] = 168;
        resp[pos++] = 4;
        resp[pos++] = 1;

        sendto(sock, resp, pos, 0, (struct sockaddr *)&client, client_len);
    }

    close(sock);
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t wifi_init_ap(void)
{
    // Create provisioning event group if not already created
    if (g_prov_event_group == NULL) {
        g_prov_event_group = xEventGroupCreate();
    }

    // Initialize netif and event loop (idempotent, guarded by flag)
    if (!s_netif_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        s_netif_initialized = true;
    }

    // Create AP netif with default config (auto-starts DHCPS)
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "failed to create AP netif");
        return ESP_FAIL;
    }

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Build AP SSID from MAC address
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));

    char ssid[32];
    snprintf(ssid, sizeof(ssid), "TaipanMiner-%02X%02X", mac[4], mac[5]);
    strncpy(s_ap_ssid, ssid, sizeof(s_ap_ssid) - 1);
    s_ap_ssid[sizeof(s_ap_ssid) - 1] = '\0';

    // Configure AP
    wifi_config_t ap_config = {
        .ap = {
            .password = "taipanminer",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(ssid);

    // Set WiFi mode and config (APSTA allows scanning while AP is running)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start DNS task
    s_dns_running = true;
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        dns_task,
        "dns",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        &s_dns_task_handle,
        0
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "failed to create DNS task");
        s_dns_running = false;
        esp_wifi_stop();
        esp_wifi_deinit();
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "AP started: SSID=%s, password=taipanminer", ssid);

    return ESP_OK;
}

void wifi_stop_ap(void)
{
    if (s_ap_netif == NULL) {
        ESP_LOGW(TAG, "AP not initialized");
        return;
    }

    // Stop DNS task
    s_dns_running = false;
    if (s_dns_task_handle != NULL) {
        // Wait for task to exit with a timeout
        for (int i = 0; i < 50; i++) {  // 5 second timeout (50 * 100ms)
            if (s_dns_task_handle == NULL) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    // Stop WiFi
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    // Destroy AP netif
    esp_netif_destroy(s_ap_netif);
    s_ap_netif = NULL;

    ESP_LOGI(TAG, "AP stopped");
}

int wifi_scan_networks(wifi_scan_ap_t *results, int max_results)
{
    if (!results || max_results <= 0) {
        return 0;
    }

    wifi_scan_config_t scan_config = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true); // blocking
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count == 0) {
        return 0;
    }
    if (count > max_results) {
        count = max_results;
    }

    wifi_ap_record_t *records = malloc(count * sizeof(wifi_ap_record_t));
    if (!records) {
        return 0;
    }

    esp_wifi_scan_get_ap_records(&count, records);

    // Deduplicate and sort by RSSI (scan can return same SSID multiple times)
    int unique = 0;
    for (int i = 0; i < count && unique < max_results; i++) {
        if (records[i].ssid[0] == '\0') {
            continue;  // skip hidden
        }

        // Check if already added
        bool dup = false;
        for (int j = 0; j < unique; j++) {
            if (strcmp(results[j].ssid, (char *)records[i].ssid) == 0) {
                dup = true;
                break;
            }
        }

        if (!dup) {
            memset(results[unique].ssid, 0, sizeof(results[unique].ssid));
            strncpy(results[unique].ssid, (char *)records[i].ssid, 32);
            results[unique].rssi = records[i].rssi;
            results[unique].secure = records[i].authmode != WIFI_AUTH_OPEN;
            unique++;
        }
    }

    free(records);
    return unique;
}

static void scan_worker_task(void *arg)
{
    wifi_scan_ap_t results[WIFI_SCAN_MAX];
    memset(results, 0, sizeof(results));
    int count = wifi_scan_networks(results, WIFI_SCAN_MAX);

    // Update cache
    memcpy(s_cached_scan, results, sizeof(results));
    s_cached_scan_count = count;
    s_scan_in_progress = false;

    ESP_LOGD(TAG, "async scan complete: %d networks found", count);
    vTaskDelete(NULL);
}

void wifi_scan_start_async(void)
{
    if (s_scan_in_progress) {
        return;
    }
    s_scan_in_progress = true;

    BaseType_t xReturned = xTaskCreate(
        scan_worker_task,
        "wifi_scan",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    if (xReturned != pdPASS) {
        ESP_LOGW(TAG, "failed to create wifi_scan task");
        s_scan_in_progress = false;
    }
}

int wifi_scan_get_cached(wifi_scan_ap_t *results, int max_results)
{
    if (!results || max_results <= 0) {
        return 0;
    }

    int count = s_cached_scan_count;
    if (count > max_results) {
        count = max_results;
    }
    if (count > 0) {
        memcpy(results, s_cached_scan, count * sizeof(wifi_scan_ap_t));
    }
    return count;
}

void wifi_prov_get_ap_ssid(char *buf, size_t len)
{
    strncpy(buf, s_ap_ssid, len - 1);
    buf[len - 1] = '\0';
}
