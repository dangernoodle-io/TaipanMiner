#pragma once

#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "http_server.h"

// Register provisioning UI routes (called by bb_prov_start)
// Takes bb_http_handle_t and returns bb_err_t, matching bb_http_app_routes_fn signature
bb_err_t taipan_web_register_prov_routes(bb_http_handle_t server);

// Register mining mode route handlers (called by bb_prov_switch_to_normal)
// Takes bb_http_handle_t and returns bb_err_t, matching bb_http_app_routes_fn signature
bb_err_t taipan_web_register_mining_routes(bb_http_handle_t server);

// Register preflight OPTIONS handler (called once at startup)
bb_err_t taipan_web_register_preflight(bb_http_handle_t server);

#endif
