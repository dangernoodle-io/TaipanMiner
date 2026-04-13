#pragma once

#include <stdbool.h>

// Stratum v1 client task — runs on Core 0, priority 5
void stratum_task(void *arg);

// Get stratum connection status
bool stratum_is_connected(void);

// Request stratum reconnect (called from WiFi event handler on IP loss)
void stratum_request_reconnect(void);

// WiFi kick callback — called after consecutive connect failures to force reassociation
typedef void (*stratum_wifi_kick_cb_t)(void);
void stratum_set_wifi_kick_cb(stratum_wifi_kick_cb_t cb);
