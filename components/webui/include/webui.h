#pragma once

#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "bb_http.h"

// Returns the TM provisioning asset table. n is set to the entry count.
const bb_http_asset_t *webui_prov_assets(size_t *n);

// Installs TM's save callback (for pool config fields).
void webui_install_prov_save_cb(void);

// Register mining mode route handlers (called after provisioning completes).
// Takes bb_http_handle_t and returns bb_err_t.
bb_err_t webui_register_mining_routes(bb_http_handle_t server);

// Register TaipanMiner-specific info extender callback (call before HTTP server starts)
bb_err_t webui_register_info_extender(void);

#endif
