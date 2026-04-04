#pragma once

#include "esp_err.h"

// Start HTTP server with provisioning form handlers (GET / and POST /save)
esp_err_t http_server_start_prov(void);

// Start HTTP server with mining status handlers (GET /, /api/stats, /ota, /ota/upload)
esp_err_t http_server_start(void);

// Remove provisioning handlers and register mining handlers (after provisioning completes)
void http_server_switch_to_mining(void);
