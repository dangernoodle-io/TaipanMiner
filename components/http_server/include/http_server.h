#pragma once

#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"

// Start HTTP server with provisioning form handlers (GET / and POST /save)
esp_err_t http_server_start_prov(void);

// Start HTTP server with mining status handlers (GET /, /api/stats, /api/ota/check, /api/ota/upload)
esp_err_t http_server_start(void);

// Remove provisioning handlers and register mining handlers (after provisioning completes)
void http_server_switch_to_mining(void);
#endif

// URL-decode a named field from a URL-encoded body (e.g., "field=value&...")
void url_decode_field(const char *body, const char *field, char *out, size_t out_size);
