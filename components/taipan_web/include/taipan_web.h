#pragma once

#include <stddef.h>

typedef void *bb_http_handle_t;

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_http_server.h"

// Register provisioning mode route handlers
esp_err_t taipan_web_register_prov_routes(httpd_handle_t server);

// Unregister provisioning mode route handlers
esp_err_t taipan_web_unregister_prov_routes(httpd_handle_t server);

// Register mining mode route handlers
esp_err_t taipan_web_register_mining_routes(httpd_handle_t server);

// Register preflight OPTIONS handler (called from http_server)
esp_err_t taipan_web_register_preflight(httpd_handle_t server);

#endif
