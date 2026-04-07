#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef ESP_PLATFORM
#include <stdint.h>
#include <stdbool.h>

// WiFi scan results
#define WIFI_SCAN_MAX 20
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;   // true if not WIFI_AUTH_OPEN
} wifi_scan_ap_t;

// Performs a blocking WiFi scan. Returns number of APs found (up to max_results).
int wifi_scan_networks(wifi_scan_ap_t *results, int max_results);

// Start a non-blocking WiFi scan in the background. Returns immediately.
void wifi_scan_start_async(void);

// Get cached WiFi scan results. Returns number of APs found (0 if no scan done yet).
int wifi_scan_get_cached(wifi_scan_ap_t *results, int max_results);
#endif

// STA mode — blocks until connected or timeout
esp_err_t wifi_init(void);           // restarts on timeout (normal boot)
esp_err_t wifi_init_sta(void);       // returns ESP_ERR_TIMEOUT on failure (provisioning retry)

// AP mode — for provisioning
esp_err_t wifi_init_ap(void);        // starts AP + captive DNS
void wifi_stop_ap(void);             // stops AP + DNS, deinits wifi
void wifi_prov_get_ap_ssid(char *buf, size_t len);  // get AP SSID

// Provisioning event
#define PROV_DONE_BIT BIT0
extern EventGroupHandle_t g_prov_event_group;
