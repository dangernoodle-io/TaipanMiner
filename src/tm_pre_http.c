#include "bb_init.h"
#include "bb_http.h"
#include "bb_openapi.h"
#include "webui.h"
#include "bb_log.h"

static const char *TAG = "tm_pre_http";

static bb_err_t taipan_cors_init(void)
{
    bb_log_d(TAG, "setting CORS methods");
    bb_http_set_cors_methods("GET, POST, PATCH, OPTIONS");
    return BB_OK;
}
BB_INIT_REGISTER_PRE_HTTP(taipan_cors, taipan_cors_init);

static bb_err_t taipan_openapi_init(void)
{
    static const bb_openapi_meta_t meta = {
        .title       = "TaipanMiner API",
        .version     = NULL,
        .description = "Bitcoin mining firmware API for ESP32-S3 boards",
    };
    bb_openapi_set_meta(&meta);
    return BB_OK;
}
BB_INIT_REGISTER_PRE_HTTP(taipan_openapi, taipan_openapi_init);

static bb_err_t taipan_webui_reserve_init(void)
{
    webui_reserve_mining_routes();
    return BB_OK;
}
BB_INIT_REGISTER_PRE_HTTP(taipan_webui_reserve, taipan_webui_reserve_init);
