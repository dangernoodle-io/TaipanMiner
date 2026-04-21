#pragma once

#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "bb_http.h"

// Returns the TM provisioning asset table. n is set to the entry count.
const bb_http_asset_t *taipan_web_prov_assets(size_t *n);

// Installs TM's save callback (for pool config fields).
void taipan_web_install_prov_save_cb(void);

// Finish prov setup by registering common routes and info extender (called after bb_prov_start).
bb_err_t taipan_web_finish_prov_setup(bb_http_handle_t server);

// Register mining mode route handlers (called after provisioning completes).
// Takes bb_http_handle_t and returns bb_err_t.
bb_err_t taipan_web_register_mining_routes(bb_http_handle_t server);

// Register preflight OPTIONS handler (called once at startup)
bb_err_t taipan_web_register_preflight(bb_http_handle_t server);

// Register TaipanMiner-specific info extender callback (call before HTTP server starts)
bb_err_t taipan_web_register_info_extender(void);

#endif
