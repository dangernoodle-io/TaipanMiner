#pragma once

#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "esp_http_server.h"
#endif

/**
 * Parse a GitHub releases/latest JSON response and extract the latest tag
 * and asset download URL for the given board.
 *
 * @param json       Full JSON response body
 * @param board_name Board name to match (e.g. "tdongle-s3")
 * @param out_tag    Buffer for tag_name (min 32 bytes)
 * @param tag_size   Size of out_tag buffer
 * @param out_url    Buffer for browser_download_url (min 256 bytes)
 * @param url_size   Size of out_url buffer
 * @return 0 on success, -1 if tag not found, -2 if no matching asset
 */
int ota_pull_parse_release_json(const char *json, const char *board_name,
                                char *out_tag, size_t tag_size,
                                char *out_url, size_t url_size);

#ifdef ESP_PLATFORM
/**
 * Register OTA pull HTTP handlers with an existing httpd instance.
 * Adds GET /api/ota/check and POST /api/ota/update.
 */
esp_err_t ota_pull_register_handler(httpd_handle_t server);
#endif
