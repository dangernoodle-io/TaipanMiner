#include "webui.h"
#include "bb_http.h"
#include "bb_info.h"
#include "bb_json.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "bb_timer.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "bb_system.h"
#include "board.h"
#include "mining.h"
#include "asic_drop_log.h"
#include "bb_nv.h"
#include "bb_diag.h"
#include "config.h"
#include "bb_wifi.h"
#include "bb_prov.h"
#include "bb_mdns.h"
#include "stratum.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <inttypes.h>
#include "bb_ota_pull.h"
#include "bb_ota_push.h"
#include "bb_ota_validator.h"
#include "bb_log.h"
#include "bb_board.h"
#include "knot.h"
#include "routes_json.h"
#include "sha256.h"
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3
#include "sha256_hw_ahb.h"
#elif CONFIG_IDF_TARGET_ESP32
#include "sha256_hw_dport.h"
#endif

/* Forward declaration: stratum_get_active_pool_idx from TA-202 phase B (provisional) */
extern int stratum_get_active_pool_idx(void);

static const char *TAG = "web";

// ============================================================================
// DEFERRED RESTART HELPERS
// ============================================================================

static void deferred_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    bb_system_restart();
    vTaskDelete(NULL);
}

static void schedule_deferred_restart(void)
{
    xTaskCreate(deferred_restart_task, "deferred_restart", 2048, NULL, 5, NULL);
}

extern const uint8_t index_html_gz[];
extern const size_t index_html_gz_len;
extern const uint8_t index_js_gz[];
extern const size_t index_js_gz_len;
extern const uint8_t index_css_gz[];
extern const size_t index_css_gz_len;
extern const uint8_t runtime_js_gz[];
extern const size_t runtime_js_gz_len;
extern const uint8_t vendor_js_gz[];
extern const size_t vendor_js_gz_len;
extern const uint8_t index2_js_gz[];
extern const size_t index2_js_gz_len;
extern const uint8_t index3_js_gz[];
extern const size_t index3_js_gz_len;
extern const uint8_t pool_js_gz[];
extern const size_t pool_js_gz_len;
extern const uint8_t update_js_gz[];
extern const size_t update_js_gz_len;
extern const uint8_t diagnostics_js_gz[];
extern const size_t diagnostics_js_gz_len;
extern const uint8_t settings_js_gz[];
extern const size_t settings_js_gz_len;
extern const uint8_t history_js_gz[];
extern const size_t history_js_gz_len;
extern const uint8_t knot_js_gz[];
extern const size_t knot_js_gz_len;
extern const uint8_t prov_index_html_gz[];
extern const size_t prov_index_html_gz_len;
extern const uint8_t prov_index_js_gz[];
extern const size_t prov_index_js_gz_len;
extern const uint8_t prov_index_css_gz[];
extern const size_t prov_index_css_gz_len;
extern const uint8_t logo_svg_gz[];
extern const size_t logo_svg_gz_len;
extern const uint8_t favicon_svg_gz[];
extern const size_t favicon_svg_gz_len;

static void set_common_headers(bb_http_request_t *req)
{
    bb_http_resp_set_header(req, "Connection", "close");
    bb_http_resp_set_header(req, "Access-Control-Allow-Origin", "*");
    bb_http_resp_set_header(req, "Access-Control-Allow-Private-Network", "true");
}


// ============================================================================
// PROVISIONING SAVE CALLBACK
// ============================================================================

static bb_err_t taipan_prov_save_cb(bb_http_request_t *req, const char *body, int len)
{
    (void)len;
    char pool_host[64] = "", wallet[64] = "", worker[32] = "";
    char pool_pass[64] = "", hostname[33] = "";
    char port_str[8] = "";

    bb_url_decode_field(body, "pool_host", pool_host, sizeof(pool_host));
    bb_url_decode_field(body, "pool_port", port_str, sizeof(port_str));
    bb_url_decode_field(body, "wallet", wallet, sizeof(wallet));
    bb_url_decode_field(body, "worker", worker, sizeof(worker));
    bb_url_decode_field(body, "pool_pass", pool_pass, sizeof(pool_pass));
    bb_url_decode_field(body, "hostname", hostname, sizeof(hostname));

    if (pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "All fields required");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    uint16_t port = (uint16_t)strtoul(port_str, NULL, 10);
    if (port == 0) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Valid port required");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    // Derive hostname from worker if not provided; otherwise normalize user input.
    if (hostname[0] == '\0') {
        bb_mdns_build_hostname(worker, NULL, hostname, sizeof(hostname));
    } else {
        char normalized[sizeof(hostname)];
        bb_mdns_build_hostname(hostname, NULL, normalized, sizeof(normalized));
        strncpy(hostname, normalized, sizeof(hostname));
        hostname[sizeof(hostname) - 1] = '\0';
    }

    // Validate and save hostname
    if (config_set_hostname(hostname) != BB_OK) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Invalid hostname");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    // Use atomic setter (TA-290 phase C): construct primary pool, preserve fallback.
    pool_cfg_t primary = {0};
    strncpy(primary.host,   pool_host, sizeof(primary.host)   - 1);
    primary.port = port;
    strncpy(primary.wallet, wallet,    sizeof(primary.wallet) - 1);
    strncpy(primary.worker, worker,    sizeof(primary.worker) - 1);
    strncpy(primary.pass,   pool_pass, sizeof(primary.pass)   - 1);

    // Preserve any existing fallback config (factory provisioning is primary-only).
    pool_cfg_t fallback = {0};
    bool has_fallback = config_pool_configured(POOL_FALLBACK);
    if (has_fallback) {
        strncpy(fallback.host,   config_pool_host_idx(POOL_FALLBACK),   sizeof(fallback.host)   - 1);
        fallback.port = config_pool_port_idx(POOL_FALLBACK);
        strncpy(fallback.wallet, config_wallet_addr_idx(POOL_FALLBACK), sizeof(fallback.wallet) - 1);
        strncpy(fallback.worker, config_worker_name_idx(POOL_FALLBACK), sizeof(fallback.worker) - 1);
        strncpy(fallback.pass,   config_pool_pass_idx(POOL_FALLBACK),   sizeof(fallback.pass)   - 1);
    }

    bb_err_t err = config_set_pools(&primary, has_fallback ? &fallback : NULL);
    if (err != BB_OK) {
        bb_http_resp_set_status(req, 500);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Failed to save config");
        bb_http_resp_json_obj_end(&e);
        return err;
    }

    bb_http_resp_set_header(req, "Connection", "close");
    bb_http_resp_set_header(req, "Access-Control-Allow-Origin", "*");
    bb_http_resp_set_header(req, "Access-Control-Allow-Private-Network", "true");
    bb_http_resp_send_chunk(req, NULL, 0);

    return BB_OK;
}

// ============================================================================
// PORTABLE HANDLERS (migrated to bb_http API)
// ============================================================================

static bb_err_t stats_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    stats_snapshot_t s = {0};
    s.session_rejected_other_last_code = -1;
    s.expected_ghs = -1.0;
#ifndef ASIC_CHIP
    s.hashrate_1m = -1.0;
    s.hashrate_10m = -1.0;
    s.hashrate_1h = -1.0;
    s.pool_effective_hashrate = -1.0;
    s.hw_error_pct_1m = -1.0;
    s.hw_error_pct_10m = -1.0;
    s.hw_error_pct_1h = -1.0;
#endif
#ifdef ASIC_CHIP
    s.pool_effective_hashrate = -1.0;
    s.asic_freq_cfg = -1.0f;
    s.asic_freq_eff = -1.0f;
#endif

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s.hw_rate    = mining_stats.hw_hashrate;
        s.hw_ema     = mining_stats.hw_ema.value;
        s.hw_shares  = mining_stats.hw_shares;
        s.temp_c     = mining_stats.temp_c;
        s.session_shares                   = mining_stats.session.shares;
        s.session_rejected                 = mining_stats.session.rejected;
        s.session_rejected_job_not_found   = mining_stats.session.rejected_job_not_found;
        s.session_rejected_low_difficulty  = mining_stats.session.rejected_low_difficulty;
        s.session_rejected_duplicate       = mining_stats.session.rejected_duplicate;
        s.session_rejected_stale_prevhash  = mining_stats.session.rejected_stale_prevhash;
        s.session_rejected_other           = mining_stats.session.rejected_other;
        s.session_rejected_other_last_code = mining_stats.session.rejected_other_last_code;
        s.session_blocks_found             = mining_stats.session.blocks_found;
        s.session_best_diff_ts             = mining_stats.session.best_diff_ts;
        s.session_last_block_ts            = mining_stats.session.last_block_ts;
        s.last_share_us    = mining_stats.session.last_share_us;
        s.session_start_us = mining_stats.session.start_us;
        s.best_diff        = mining_stats.session.best_diff;
#ifndef ASIC_CHIP
        s.hashrate_1m       = (double)mining_stats.hashrate_1m;
        s.hashrate_10m      = (double)mining_stats.hashrate_10m;
        s.hashrate_1h       = (double)mining_stats.hashrate_1h;
        s.hw_error_pct_1m   = (double)mining_stats.hw_error_pct_1m;
        s.hw_error_pct_10m  = (double)mining_stats.hw_error_pct_10m;
        s.hw_error_pct_1h   = (double)mining_stats.hw_error_pct_1h;
#endif
#ifdef ASIC_CHIP
        s.asic_rate         = mining_stats.asic_hashrate;
        s.asic_ema          = mining_stats.asic_ema.value;
        s.asic_shares       = mining_stats.asic_shares;
        s.asic_temp_c       = mining_stats.asic_temp_c;
        s.asic_freq_cfg     = mining_stats.asic_freq_configured_mhz;
        s.asic_freq_eff     = mining_stats.asic_freq_effective_mhz;
        s.asic_total_ghs    = mining_stats.asic_total_ghs;
        s.asic_hw_error_pct = mining_stats.asic_hw_error_pct;
        s.asic_total_ghs_1m       = mining_stats.asic_total_ghs_1m;
        s.asic_total_ghs_10m      = mining_stats.asic_total_ghs_10m;
        s.asic_total_ghs_1h       = mining_stats.asic_total_ghs_1h;
        s.asic_hw_error_pct_1m    = mining_stats.asic_hw_error_pct_1m;
        s.asic_hw_error_pct_10m   = mining_stats.asic_hw_error_pct_10m;
        s.asic_hw_error_pct_1h    = mining_stats.asic_hw_error_pct_1h;
        s.asic_total_valid  = (s.asic_total_ghs > 0.001f);
        s.asic_small_cores  = BOARD_SMALL_CORES;
        s.asic_count        = BOARD_ASIC_COUNT;
#endif
        xSemaphoreGive(mining_stats.mutex);
    }

    double expected = -1.0;
#ifdef ASIC_CHIP
    float expected_freq = s.asic_freq_cfg;
#else
    float expected_freq = 0.0f;
#endif
    if (mining_get_expected_ghs(expected_freq, &expected)) {
        s.expected_ghs = expected;
    } else {
        s.expected_ghs = -1.0;
    }

    double pool_eff_hr = mining_get_pool_effective_hashrate();
    s.pool_effective_hashrate = (pool_eff_hr > 0.0) ? pool_eff_hr : -1.0;

    s.now_us = (int64_t)bb_timer_now_us();

#ifdef ASIC_CHIP
    /* Per-chip telemetry (TA-192 phase 2) — collected after mutex released */
    asic_chip_telemetry_t chip_tel[BOARD_ASIC_COUNT];
    int n_chips = asic_task_get_chip_telemetry(chip_tel, BOARD_ASIC_COUNT);
    s.n_chips = (n_chips <= ROUTES_JSON_MAX_CHIPS) ? n_chips : ROUTES_JSON_MAX_CHIPS;
    for (int c = 0; c < s.n_chips; c++) {
        s.chips[c].total_ghs   = chip_tel[c].total_ghs;
        s.chips[c].error_ghs   = chip_tel[c].error_ghs;
        s.chips[c].hw_err_pct  = chip_tel[c].hw_err_pct;
        s.chips[c].total_raw   = chip_tel[c].total_raw;
        s.chips[c].error_raw   = chip_tel[c].error_raw;
        s.chips[c].total_drops = chip_tel[c].total_drops;
        s.chips[c].error_drops = chip_tel[c].error_drops;
        s.chips[c].last_drop_us = chip_tel[c].last_drop_us;
        for (int d = 0; d < 4; d++) {
            s.chips[c].domain_ghs[d]   = chip_tel[c].domain_ghs[d];
            s.chips[c].domain_drops[d] = chip_tel[c].domain_drops[d];
        }
    }
    /* Re-read now_us after chip telemetry fetch for accurate last_drop_ago_s */
    s.now_us = (int64_t)bb_timer_now_us();
#endif

    int64_t uptime_s = (s.session_start_us > 0)
                       ? (s.now_us - s.session_start_us) / 1000000
                       : 0;
    int64_t last_share_ago_s = (s.last_share_us > 0)
                               ? (s.now_us - s.last_share_us) / 1000000
                               : -1;

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;

    bb_http_resp_json_obj_set_num(&obj, "hashrate",        s.hw_rate);
    bb_http_resp_json_obj_set_num(&obj, "hashrate_avg",    s.hw_ema);
    bb_http_resp_json_obj_set_num(&obj, "temp_c",          (double)s.temp_c);
    bb_http_resp_json_obj_set_int(&obj, "shares",          (int64_t)s.hw_shares);
    bb_http_resp_json_obj_set_int(&obj, "session_shares",  (int64_t)s.session_shares);
    bb_http_resp_json_obj_set_int(&obj, "session_rejected",(int64_t)s.session_rejected);
    bb_http_resp_json_obj_set_int(&obj, "session_blocks_found", (int64_t)s.session_blocks_found);
    bb_http_resp_json_obj_set_int(&obj, "session_best_diff_ts", s.session_best_diff_ts);
    bb_http_resp_json_obj_set_int(&obj, "session_last_block_ts", s.session_last_block_ts);

    bb_http_resp_json_obj_set_obj_begin(&obj, "rejected");
    bb_http_resp_json_obj_set_int(&obj, "total",           (int64_t)s.session_rejected);
    bb_http_resp_json_obj_set_int(&obj, "job_not_found",   (int64_t)s.session_rejected_job_not_found);
    bb_http_resp_json_obj_set_int(&obj, "low_difficulty",  (int64_t)s.session_rejected_low_difficulty);
    bb_http_resp_json_obj_set_int(&obj, "duplicate",       (int64_t)s.session_rejected_duplicate);
    bb_http_resp_json_obj_set_int(&obj, "stale_prevhash",  (int64_t)s.session_rejected_stale_prevhash);
    bb_http_resp_json_obj_set_int(&obj, "other",           (int64_t)s.session_rejected_other);
    bb_http_resp_json_obj_set_int(&obj, "other_last_code", (int64_t)s.session_rejected_other_last_code);
    bb_http_resp_json_obj_set_obj_end(&obj);

    bb_http_resp_json_obj_set_int(&obj, "last_share_ago_s", last_share_ago_s);
    bb_http_resp_json_obj_set_num(&obj, "best_diff",        s.best_diff);
    bb_http_resp_json_obj_set_int(&obj, "uptime_s",         uptime_s);

    if (s.expected_ghs >= 0.0) {
        bb_http_resp_json_obj_set_num(&obj, "expected_ghs", s.expected_ghs);
    } else {
        bb_http_resp_json_obj_set_null(&obj, "expected_ghs");
    }

#ifndef ASIC_CHIP
    if (s.hashrate_1m >= 0.0)             bb_http_resp_json_obj_set_num(&obj, "hashrate_1m",             s.hashrate_1m);
    else                                  bb_http_resp_json_obj_set_null(&obj, "hashrate_1m");
    if (s.hashrate_10m >= 0.0)            bb_http_resp_json_obj_set_num(&obj, "hashrate_10m",            s.hashrate_10m);
    else                                  bb_http_resp_json_obj_set_null(&obj, "hashrate_10m");
    if (s.hashrate_1h >= 0.0)             bb_http_resp_json_obj_set_num(&obj, "hashrate_1h",             s.hashrate_1h);
    else                                  bb_http_resp_json_obj_set_null(&obj, "hashrate_1h");
    if (s.pool_effective_hashrate >= 0.0) bb_http_resp_json_obj_set_num(&obj, "pool_effective_hashrate", s.pool_effective_hashrate);
    else                                  bb_http_resp_json_obj_set_null(&obj, "pool_effective_hashrate");
    if (s.hw_error_pct_1m >= 0.0)         bb_http_resp_json_obj_set_num(&obj, "hw_error_pct_1m",         s.hw_error_pct_1m);
    else                                  bb_http_resp_json_obj_set_null(&obj, "hw_error_pct_1m");
    if (s.hw_error_pct_10m >= 0.0)        bb_http_resp_json_obj_set_num(&obj, "hw_error_pct_10m",        s.hw_error_pct_10m);
    else                                  bb_http_resp_json_obj_set_null(&obj, "hw_error_pct_10m");
    if (s.hw_error_pct_1h >= 0.0)         bb_http_resp_json_obj_set_num(&obj, "hw_error_pct_1h",         s.hw_error_pct_1h);
    else                                  bb_http_resp_json_obj_set_null(&obj, "hw_error_pct_1h");
#endif

#ifdef ASIC_CHIP
    bb_http_resp_json_obj_set_num(&obj, "asic_hashrate",     s.asic_rate);
    bb_http_resp_json_obj_set_num(&obj, "asic_hashrate_avg", s.asic_ema);
    bb_http_resp_json_obj_set_int(&obj, "asic_shares",       (int64_t)s.asic_shares);
    bb_http_resp_json_obj_set_num(&obj, "asic_temp_c",       (double)s.asic_temp_c);
    if (s.asic_freq_cfg >= 0.0f) bb_http_resp_json_obj_set_num(&obj, "asic_freq_configured_mhz", (double)s.asic_freq_cfg);
    else                         bb_http_resp_json_obj_set_null(&obj, "asic_freq_configured_mhz");
    if (s.asic_freq_eff >= 0.0f) bb_http_resp_json_obj_set_num(&obj, "asic_freq_effective_mhz", (double)s.asic_freq_eff);
    else                         bb_http_resp_json_obj_set_null(&obj, "asic_freq_effective_mhz");
    bb_http_resp_json_obj_set_int(&obj, "asic_small_cores", (int64_t)s.asic_small_cores);
    bb_http_resp_json_obj_set_int(&obj, "asic_count",       (int64_t)s.asic_count);
    if (s.asic_total_valid) {
        bb_http_resp_json_obj_set_num(&obj, "asic_total_ghs",    (double)s.asic_total_ghs);
        bb_http_resp_json_obj_set_num(&obj, "asic_hw_error_pct", (double)s.asic_hw_error_pct);
        if (s.asic_total_ghs_1m >= 0.0f)   bb_http_resp_json_obj_set_num(&obj, "asic_total_ghs_1m",   (double)s.asic_total_ghs_1m);
        else                                bb_http_resp_json_obj_set_null(&obj, "asic_total_ghs_1m");
        if (s.asic_total_ghs_10m >= 0.0f)  bb_http_resp_json_obj_set_num(&obj, "asic_total_ghs_10m",  (double)s.asic_total_ghs_10m);
        else                               bb_http_resp_json_obj_set_null(&obj, "asic_total_ghs_10m");
        if (s.asic_total_ghs_1h >= 0.0f)   bb_http_resp_json_obj_set_num(&obj, "asic_total_ghs_1h",   (double)s.asic_total_ghs_1h);
        else                               bb_http_resp_json_obj_set_null(&obj, "asic_total_ghs_1h");
        if (s.asic_hw_error_pct_1m >= 0.0f)  bb_http_resp_json_obj_set_num(&obj, "asic_hw_error_pct_1m",  (double)s.asic_hw_error_pct_1m);
        else                                  bb_http_resp_json_obj_set_null(&obj, "asic_hw_error_pct_1m");
        if (s.asic_hw_error_pct_10m >= 0.0f) bb_http_resp_json_obj_set_num(&obj, "asic_hw_error_pct_10m", (double)s.asic_hw_error_pct_10m);
        else                                  bb_http_resp_json_obj_set_null(&obj, "asic_hw_error_pct_10m");
        if (s.asic_hw_error_pct_1h >= 0.0f)  bb_http_resp_json_obj_set_num(&obj, "asic_hw_error_pct_1h",  (double)s.asic_hw_error_pct_1h);
        else                                  bb_http_resp_json_obj_set_null(&obj, "asic_hw_error_pct_1h");
    } else {
        bb_http_resp_json_obj_set_null(&obj, "asic_total_ghs");
        bb_http_resp_json_obj_set_null(&obj, "asic_hw_error_pct");
        bb_http_resp_json_obj_set_null(&obj, "asic_total_ghs_1m");
        bb_http_resp_json_obj_set_null(&obj, "asic_total_ghs_10m");
        bb_http_resp_json_obj_set_null(&obj, "asic_total_ghs_1h");
        bb_http_resp_json_obj_set_null(&obj, "asic_hw_error_pct_1m");
        bb_http_resp_json_obj_set_null(&obj, "asic_hw_error_pct_10m");
        bb_http_resp_json_obj_set_null(&obj, "asic_hw_error_pct_1h");
    }
    if (s.pool_effective_hashrate >= 0.0) bb_http_resp_json_obj_set_num(&obj, "pool_effective_hashrate", s.pool_effective_hashrate);
    else                                  bb_http_resp_json_obj_set_null(&obj, "pool_effective_hashrate");

    bb_http_resp_json_obj_set_arr_begin(&obj, "asic_chips");
    for (int c = 0; c < s.n_chips; c++) {
        bb_http_resp_json_obj_set_obj_begin(&obj, NULL);
        bb_http_resp_json_obj_set_int(&obj, "idx",         (int64_t)c);
        bb_http_resp_json_obj_set_num(&obj, "total_ghs",   (double)s.chips[c].total_ghs);
        bb_http_resp_json_obj_set_num(&obj, "error_ghs",   (double)s.chips[c].error_ghs);
        bb_http_resp_json_obj_set_num(&obj, "hw_err_pct",  (double)s.chips[c].hw_err_pct);
        bb_http_resp_json_obj_set_int(&obj, "total_raw",   (int64_t)s.chips[c].total_raw);
        bb_http_resp_json_obj_set_int(&obj, "error_raw",   (int64_t)s.chips[c].error_raw);
        bb_http_resp_json_obj_set_num(&obj, "total_drops", (double)s.chips[c].total_drops);
        bb_http_resp_json_obj_set_num(&obj, "error_drops", (double)s.chips[c].error_drops);
        if (s.chips[c].last_drop_us == 0 || s.now_us < (int64_t)s.chips[c].last_drop_us) {
            bb_http_resp_json_obj_set_null(&obj, "last_drop_ago_s");
        } else {
            uint64_t ago_us = (uint64_t)s.now_us - s.chips[c].last_drop_us;
            bb_http_resp_json_obj_set_num(&obj, "last_drop_ago_s", (double)(ago_us / 1000000ULL));
        }
        bb_http_resp_json_obj_set_arr_begin(&obj, "domain_ghs");
        for (int d = 0; d < 4; d++) bb_http_resp_json_obj_set_num(&obj, NULL, (double)s.chips[c].domain_ghs[d]);
        bb_http_resp_json_obj_set_arr_end(&obj);
        bb_http_resp_json_obj_set_arr_begin(&obj, "domain_drops");
        for (int d = 0; d < 4; d++) bb_http_resp_json_obj_set_num(&obj, NULL, (double)s.chips[c].domain_drops[d]);
        bb_http_resp_json_obj_set_arr_end(&obj);
        bb_http_resp_json_obj_set_obj_end(&obj);
    }
    bb_http_resp_json_obj_set_arr_end(&obj);
#endif

    return bb_http_resp_json_obj_end(&obj);
}

// ----------------------------------------------------------------------------
// /api/pool — TA-281/TA-286
// Locked shape: pool config (always populated from config_*) +
// session-scoped negotiated values (extranonce1, extranonce2_size,
// version_mask) + most-recent stratum mining.notify exposed as a `notify`
// sub-object. Pre-stratum-connect, connected=false, session_start_ago_s/
// extranonce*/version_mask/notify are all null; current_difficulty defaults
// to stratum_state.difficulty (512.0).
// ----------------------------------------------------------------------------
static bb_err_t pool_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    /* pool_snapshot_t (~1991 B) + stats_arr[8] (~912 B) + the streaming
     * obj state (~1040 B) overflow the 4 KB httpd worker stack — heap
     * these two and free before return. */
    pool_snapshot_t *s = calloc(1, sizeof(*s));
    pool_stat_snapshot_t *stats_arr = calloc(ROUTES_JSON_MAX_POOL_STATS, sizeof(*stats_arr));
    if (!s || !stats_arr) {
        free(s); free(stats_arr);
        bb_http_resp_set_status(req, 500);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "out of memory");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_NO_SPACE;
    }
    size_t stats_count = 0;

    s->connected = stratum_is_connected();

    // Top-level host/port/worker/wallet reflect the *active* pool slot so the
    // UI's connection summary tracks the real connection target after a
    // failover or manual switch. Falls back to primary when not connected.
    {
        int idx = stratum_get_active_pool_idx();
        if (idx < 0) idx = POOL_PRIMARY;
        const char *h = config_pool_host_idx(idx);
        if (h) strncpy(s->host, h, sizeof(s->host) - 1);
        const char *wk = config_worker_name_idx(idx);
        if (wk) strncpy(s->worker, wk, sizeof(s->worker) - 1);
        const char *wa = config_wallet_addr_idx(idx);
        if (wa) strncpy(s->wallet, wa, sizeof(s->wallet) - 1);
        s->port = config_pool_port_idx(idx);
    }

    // session_start_ago_s — null pre-connect; wrap-safe diff in ms then /1000.
    uint32_t start_ms = stratum_get_session_start_ms();
    if (start_ms != 0) {
        uint32_t now_ms   = pdTICKS_TO_MS(xTaskGetTickCount());
        uint32_t delta_ms = now_ms - start_ms;  // unsigned wrap-safe
        s->session_start_ago_s = delta_ms / 1000U;
        s->has_session_start   = true;
    }

    s->current_difficulty = stratum_get_difficulty();
    s->latency_ms = stratum_get_pool_rtt_ms();  // -1 if no sample yet (TA-118)

    // Negotiated session params — null until subscribe response received.
    stratum_session_snapshot_t sess;
    if (stratum_get_session_snapshot(&sess) && sess.extranonce1_len > 0) {
        size_t copy_len = sess.extranonce1_len;
        if (copy_len > ROUTES_JSON_EXTRANONCE1_MAX)
            copy_len = ROUTES_JSON_EXTRANONCE1_MAX;
        memcpy(s->extranonce1, sess.extranonce1, copy_len);
        s->extranonce1_len  = copy_len;
        s->extranonce2_size = sess.extranonce2_size;
        s->version_mask     = sess.version_mask;
    }

    // notify sub-object — most recent mining.notify (TA-201).
    const stratum_job_t *job = NULL;
    if (stratum_get_job_snapshot(&job) && job) {
        s->has_notify = true;
        strncpy(s->job_id, job->job_id, sizeof(s->job_id) - 1);
        memcpy(s->prevhash, job->prevhash, 32);

        size_t cb1 = job->coinb1_len < ROUTES_JSON_MAX_COINB ? job->coinb1_len : ROUTES_JSON_MAX_COINB;
        memcpy(s->coinb1, job->coinb1, cb1);
        s->coinb1_len = cb1;

        size_t cb2 = job->coinb2_len < ROUTES_JSON_MAX_COINB ? job->coinb2_len : ROUTES_JSON_MAX_COINB;
        memcpy(s->coinb2, job->coinb2, cb2);
        s->coinb2_len = cb2;

        size_t mc = job->merkle_count < ROUTES_JSON_MAX_MERKLE ? job->merkle_count : ROUTES_JSON_MAX_MERKLE;
        for (size_t i = 0; i < mc; i++)
            memcpy(s->merkle_branches[i], job->merkle_branches[i], 32);
        s->merkle_count = mc;

        s->version    = job->version;
        s->nbits      = job->nbits;
        s->ntime      = job->ntime;
        s->clean_jobs = job->clean_jobs;
    }

    // Configured pools (TA-290/TA-202 phase D) — expose persisted config.
    for (int i = 0; i < POOL_COUNT; i++) {
        s->configured[i].configured = config_pool_configured(i);
        if (s->configured[i].configured) {
            strncpy(s->configured[i].host,   config_pool_host_idx(i),   sizeof(s->configured[i].host)   - 1);
            s->configured[i].port = config_pool_port_idx(i);
            strncpy(s->configured[i].worker, config_worker_name_idx(i), sizeof(s->configured[i].worker) - 1);
            strncpy(s->configured[i].wallet, config_wallet_addr_idx(i), sizeof(s->configured[i].wallet) - 1);
            s->configured[i].extranonce_subscribe =
                config_pool_extranonce_subscribe_idx(i);
            s->configured[i].decode_coinbase =
                config_pool_decode_coinbase_idx(i);
        }
    }
    s->active_pool_idx = stratum_get_active_pool_idx();
    s->extranonce_subscribe_status = (int)stratum_get_extranonce_subscribe_status();

    double pool_eff_hr = mining_get_pool_effective_hashrate();
    s->pool_effective_hashrate = (pool_eff_hr > 0.0) ? pool_eff_hr : -1.0;

    /* TA-363: rolling 1m/10m/1h pool-effective windows */
    s->pool_effective_hashrate_1m = mining_get_pool_effective_1m();
    s->pool_effective_hashrate_1m = (s->pool_effective_hashrate_1m > 0.0) ? s->pool_effective_hashrate_1m : -1.0;
    s->pool_effective_hashrate_10m = mining_get_pool_effective_10m();
    s->pool_effective_hashrate_10m = (s->pool_effective_hashrate_10m > 0.0) ? s->pool_effective_hashrate_10m : -1.0;
    s->pool_effective_hashrate_1h = mining_get_pool_effective_1h();
    s->pool_effective_hashrate_1h = (s->pool_effective_hashrate_1h > 0.0) ? s->pool_effective_hashrate_1h : -1.0;

    // Snapshot per-pool stats (ordered by last_seen_us descending, empty slots omitted).
    {
        xSemaphoreTake(mining_stats.mutex, portMAX_DELAY);
        for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
            const mining_pool_stat_t *src = &mining_stats.pool_stats.slots[i];
            if (src->last_seen_us == 0) continue;
            pool_stat_snapshot_t *dst = &stats_arr[stats_count++];
            dst->last_seen_us = src->last_seen_us;
            dst->shares       = src->shares;
            dst->hashes       = src->hashes;
            dst->best_diff    = src->best_diff;
            dst->blocks_found = src->blocks_found;
            strncpy(dst->host, src->host, sizeof(dst->host) - 1);
            dst->host[sizeof(dst->host) - 1] = '\0';
            dst->port = src->port;
            dst->best_diff_ts  = src->best_diff_ts;
            dst->last_block_ts = src->last_block_ts;
        }
        s->lifetime_blocks_total    = mining_stats.pool_stats.lifetime_blocks_total;
        s->lifetime_last_block_ts   = mining_stats.pool_stats.lifetime_last_block_ts;
        xSemaphoreGive(mining_stats.mutex);

        // In-place sort by last_seen_us descending.
        for (size_t i = 0; i + 1 < stats_count; i++) {
            for (size_t j = i + 1; j < stats_count; j++) {
                if (stats_arr[j].last_seen_us > stats_arr[i].last_seen_us) {
                    pool_stat_snapshot_t tmp = stats_arr[i];
                    stats_arr[i] = stats_arr[j];
                    stats_arr[j] = tmp;
                }
            }
        }
    }

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;

    bb_http_resp_json_obj_set_str(&obj, "host",   s->host);
    bb_http_resp_json_obj_set_int(&obj, "port",   (int64_t)s->port);
    bb_http_resp_json_obj_set_str(&obj, "worker", s->worker);
    bb_http_resp_json_obj_set_str(&obj, "wallet", s->wallet);
    bb_http_resp_json_obj_set_bool(&obj, "connected", s->connected);

    if (s->has_session_start) {
        bb_http_resp_json_obj_set_int(&obj, "session_start_ago_s", (int64_t)s->session_start_ago_s);
    } else {
        bb_http_resp_json_obj_set_null(&obj, "session_start_ago_s");
    }

    bb_http_resp_json_obj_set_num(&obj, "current_difficulty", s->current_difficulty);

    if (s->pool_effective_hashrate >= 0.0) bb_http_resp_json_obj_set_num(&obj, "pool_effective_hashrate", s->pool_effective_hashrate);
    else                                  bb_http_resp_json_obj_set_null(&obj, "pool_effective_hashrate");
    if (s->pool_effective_hashrate_1m >= 0.0) bb_http_resp_json_obj_set_num(&obj, "pool_effective_hashrate_1m", s->pool_effective_hashrate_1m);
    else                                     bb_http_resp_json_obj_set_null(&obj, "pool_effective_hashrate_1m");
    if (s->pool_effective_hashrate_10m >= 0.0) bb_http_resp_json_obj_set_num(&obj, "pool_effective_hashrate_10m", s->pool_effective_hashrate_10m);
    else                                      bb_http_resp_json_obj_set_null(&obj, "pool_effective_hashrate_10m");
    if (s->pool_effective_hashrate_1h >= 0.0) bb_http_resp_json_obj_set_num(&obj, "pool_effective_hashrate_1h", s->pool_effective_hashrate_1h);
    else                                     bb_http_resp_json_obj_set_null(&obj, "pool_effective_hashrate_1h");

    if (s->latency_ms >= 0) bb_http_resp_json_obj_set_int(&obj, "latency_ms", (int64_t)s->latency_ms);
    else                   bb_http_resp_json_obj_set_null(&obj, "latency_ms");

    if (s->extranonce1_len > 0) {
        char en1_hex[2 * ROUTES_JSON_EXTRANONCE1_MAX + 1];
        bytes_to_hex(s->extranonce1, s->extranonce1_len, en1_hex);
        bb_http_resp_json_obj_set_str(&obj, "extranonce1", en1_hex);
        bb_http_resp_json_obj_set_int(&obj, "extranonce2_size", (int64_t)s->extranonce2_size);
        if (s->version_mask != 0) {
            char vm_hex[9];
            snprintf(vm_hex, sizeof(vm_hex), "%08lx", (unsigned long)s->version_mask);
            bb_http_resp_json_obj_set_str(&obj, "version_mask", vm_hex);
        } else {
            bb_http_resp_json_obj_set_null(&obj, "version_mask");
        }
    } else {
        bb_http_resp_json_obj_set_null(&obj, "extranonce1");
        bb_http_resp_json_obj_set_null(&obj, "extranonce2_size");
        bb_http_resp_json_obj_set_null(&obj, "version_mask");
    }

    if (s->has_notify) {
        bb_http_resp_json_obj_set_obj_begin(&obj, "notify");
        bb_http_resp_json_obj_set_str(&obj, "job_id", s->job_id);

        char prevhash_hex[65];
        bytes_to_hex(s->prevhash, 32, prevhash_hex);
        bb_http_resp_json_obj_set_str(&obj, "prev_hash", prevhash_hex);

        char coinb1_hex[2 * ROUTES_JSON_MAX_COINB + 1];
        bytes_to_hex(s->coinb1, s->coinb1_len, coinb1_hex);
        bb_http_resp_json_obj_set_str(&obj, "coinb1", coinb1_hex);

        char coinb2_hex[2 * ROUTES_JSON_MAX_COINB + 1];
        bytes_to_hex(s->coinb2, s->coinb2_len, coinb2_hex);
        bb_http_resp_json_obj_set_str(&obj, "coinb2", coinb2_hex);

        bb_http_resp_json_obj_set_arr_begin(&obj, "merkle_branches");
        for (size_t i = 0; i < s->merkle_count; i++) {
            char br_hex[65];
            bytes_to_hex(s->merkle_branches[i], 32, br_hex);
            bb_http_resp_json_obj_set_str(&obj, NULL, br_hex);
        }
        bb_http_resp_json_obj_set_arr_end(&obj);

        char hex8[9];
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->version);
        bb_http_resp_json_obj_set_str(&obj, "version", hex8);
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->nbits);
        bb_http_resp_json_obj_set_str(&obj, "nbits", hex8);
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)s->ntime);
        bb_http_resp_json_obj_set_str(&obj, "ntime", hex8);

        bb_http_resp_json_obj_set_bool(&obj, "clean_jobs", s->clean_jobs);
        bb_http_resp_json_obj_set_obj_end(&obj);
    } else {
        bb_http_resp_json_obj_set_null(&obj, "notify");
    }

    if (s->active_pool_idx >= 0) bb_http_resp_json_obj_set_int(&obj, "active_pool_idx", (int64_t)s->active_pool_idx);
    else                        bb_http_resp_json_obj_set_null(&obj, "active_pool_idx");

    {
        const char *sub_str;
        switch (s->extranonce_subscribe_status) {
            case 1:  sub_str = "pending";  break;
            case 2:  sub_str = "active";   break;
            case 3:  sub_str = "rejected"; break;
            default: sub_str = "off";      break;
        }
        bb_http_resp_json_obj_set_str(&obj, "extranonce_subscribe_status", sub_str);
    }

    bb_http_resp_json_obj_set_int(&obj, "lifetime_blocks_total",  (int64_t)s->lifetime_blocks_total);
    bb_http_resp_json_obj_set_int(&obj, "lifetime_last_block_ts", s->lifetime_last_block_ts);

    bb_http_resp_json_obj_set_obj_begin(&obj, "configured");
    for (int i = 0; i < 2; i++) {
        const char *cfg_key = (i == 0) ? "primary" : "fallback";
        if (s->configured[i].configured) {
            bb_http_resp_json_obj_set_obj_begin(&obj, cfg_key);
            bb_http_resp_json_obj_set_str(&obj, "host",   s->configured[i].host);
            bb_http_resp_json_obj_set_int(&obj, "port",   (int64_t)s->configured[i].port);
            bb_http_resp_json_obj_set_str(&obj, "worker", s->configured[i].worker);
            bb_http_resp_json_obj_set_str(&obj, "wallet", s->configured[i].wallet);
            bb_http_resp_json_obj_set_bool(&obj, "extranonce_subscribe", s->configured[i].extranonce_subscribe);
            bb_http_resp_json_obj_set_bool(&obj, "decode_coinbase",      s->configured[i].decode_coinbase);
            bb_http_resp_json_obj_set_obj_end(&obj);
        } else {
            bb_http_resp_json_obj_set_null(&obj, cfg_key);
        }
    }
    bb_http_resp_json_obj_set_obj_end(&obj);

    /* per-pool stats array */
    bb_http_resp_json_obj_set_arr_begin(&obj, "stats");
    for (size_t i = 0; i < stats_count; i++) {
        bb_http_resp_json_obj_set_obj_begin(&obj, NULL);
        bb_http_resp_json_obj_set_str(&obj, "host",         stats_arr[i].host);
        bb_http_resp_json_obj_set_int(&obj, "port",         (int64_t)stats_arr[i].port);
        bb_http_resp_json_obj_set_int(&obj, "shares",       (int64_t)stats_arr[i].shares);
        bb_http_resp_json_obj_set_int(&obj, "hashes",       (int64_t)stats_arr[i].hashes);
        bb_http_resp_json_obj_set_num(&obj, "best_diff",    stats_arr[i].best_diff);
        bb_http_resp_json_obj_set_int(&obj, "blocks_found", (int64_t)stats_arr[i].blocks_found);
        bb_http_resp_json_obj_set_int(&obj, "last_seen_s",  (int64_t)(stats_arr[i].last_seen_us / 1000000));
        bb_http_resp_json_obj_set_int(&obj, "best_diff_ts", stats_arr[i].best_diff_ts);
        bb_http_resp_json_obj_set_int(&obj, "last_block_ts", stats_arr[i].last_block_ts);
        bb_http_resp_json_obj_set_obj_end(&obj);
    }
    bb_http_resp_json_obj_set_arr_end(&obj);

    bb_err_t rc_end = bb_http_resp_json_obj_end(&obj);
    free(s); free(stats_arr);
    return rc_end;
}

// ---------------------------------------------------------------------------
// /api/pool — PUT (TA-290/TA-202 phase C)
// Set primary and optional fallback pool config atomically.
// ---------------------------------------------------------------------------

static bb_err_t pool_put_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    char body[768];
    int body_len = bb_http_req_body_len(req);
    if (body_len > (int)(sizeof(body) - 1)) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Body too large");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Empty body");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    body[len] = '\0';

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Invalid JSON");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    // Parse primary pool (required)
    bb_json_t primary_obj = bb_json_obj_get_item(root, "primary");
    if (!primary_obj) {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "primary pool required");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    pool_cfg_t primary = {0};
    bb_json_t j;

    j = bb_json_obj_get_item(primary_obj, "host");
    if (j && bb_json_item_is_string(j)) {
        strncpy(primary.host, bb_json_item_get_string(j), sizeof(primary.host) - 1);
    }
    if (primary.host[0] == '\0') {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "primary host required");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    j = bb_json_obj_get_item(primary_obj, "port");
    if (j && bb_json_item_is_number(j)) {
        primary.port = (uint16_t)bb_json_item_get_double(j);
    }
    if (primary.port == 0 || primary.port > 65535) {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "primary port must be in [1, 65535]");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    j = bb_json_obj_get_item(primary_obj, "wallet");
    if (j && bb_json_item_is_string(j)) {
        strncpy(primary.wallet, bb_json_item_get_string(j), sizeof(primary.wallet) - 1);
    }
    if (primary.wallet[0] == '\0') {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "primary wallet required");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    j = bb_json_obj_get_item(primary_obj, "worker");
    if (j && bb_json_item_is_string(j)) {
        strncpy(primary.worker, bb_json_item_get_string(j), sizeof(primary.worker) - 1);
    }
    if (primary.worker[0] == '\0') {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "primary worker required");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    /* pool_pass: if the field is *missing* from the JSON, preserve the
     * existing value. If present (even as empty string), use it. Lets the
     * UI PUT only the fields it wants to change without clobbering
     * passwords that aren't displayed client-side. */
    j = bb_json_obj_get_item(primary_obj, "pool_pass");
    if (j && bb_json_item_is_string(j)) {
        strncpy(primary.pass, bb_json_item_get_string(j), sizeof(primary.pass) - 1);
    } else {
        strncpy(primary.pass,
                config_pool_pass_idx(POOL_PRIMARY),
                sizeof(primary.pass) - 1);
    }

    /* TA-306 / TA-307 toggles. Missing fields preserve current value. */
    primary.extranonce_subscribe =
        config_pool_extranonce_subscribe_idx(POOL_PRIMARY);
    primary.decode_coinbase =
        config_pool_decode_coinbase_idx(POOL_PRIMARY);
    {
        bool b;
        if (bb_json_obj_get_bool(primary_obj, "extranonce_subscribe", &b)) {
            primary.extranonce_subscribe = b;
        }
        if (bb_json_obj_get_bool(primary_obj, "decode_coinbase", &b)) {
            primary.decode_coinbase = b;
        }
    }

    // Parse fallback pool (optional; null or missing clears it)
    pool_cfg_t fallback = {0};
    pool_cfg_t *fallback_ptr = NULL;

    bb_json_t fallback_obj = bb_json_obj_get_item(root, "fallback");
    if (fallback_obj && bb_json_item_is_object(fallback_obj)) {
        j = bb_json_obj_get_item(fallback_obj, "host");
        if (j && bb_json_item_is_string(j)) {
            strncpy(fallback.host, bb_json_item_get_string(j), sizeof(fallback.host) - 1);
        }
        if (fallback.host[0] == '\0') {
            bb_json_free(root);
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "fallback host required if provided");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }

        j = bb_json_obj_get_item(fallback_obj, "port");
        if (j && bb_json_item_is_number(j)) {
            fallback.port = (uint16_t)bb_json_item_get_double(j);
        }
        if (fallback.port == 0 || fallback.port > 65535) {
            bb_json_free(root);
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "fallback port must be in [1, 65535]");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }

        j = bb_json_obj_get_item(fallback_obj, "wallet");
        if (j && bb_json_item_is_string(j)) {
            strncpy(fallback.wallet, bb_json_item_get_string(j), sizeof(fallback.wallet) - 1);
        }
        if (fallback.wallet[0] == '\0') {
            bb_json_free(root);
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "fallback wallet required if provided");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }

        j = bb_json_obj_get_item(fallback_obj, "worker");
        if (j && bb_json_item_is_string(j)) {
            strncpy(fallback.worker, bb_json_item_get_string(j), sizeof(fallback.worker) - 1);
        }
        if (fallback.worker[0] == '\0') {
            bb_json_free(root);
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "fallback worker required if provided");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }

        j = bb_json_obj_get_item(fallback_obj, "pool_pass");
        if (j && bb_json_item_is_string(j)) {
            strncpy(fallback.pass, bb_json_item_get_string(j), sizeof(fallback.pass) - 1);
        } else {
            strncpy(fallback.pass,
                    config_pool_pass_idx(POOL_FALLBACK),
                    sizeof(fallback.pass) - 1);
        }

        fallback.extranonce_subscribe =
            config_pool_extranonce_subscribe_idx(POOL_FALLBACK);
        fallback.decode_coinbase =
            config_pool_decode_coinbase_idx(POOL_FALLBACK);
        {
            bool b;
            if (bb_json_obj_get_bool(fallback_obj, "extranonce_subscribe", &b)) {
                fallback.extranonce_subscribe = b;
            }
            if (bb_json_obj_get_bool(fallback_obj, "decode_coinbase", &b)) {
                fallback.decode_coinbase = b;
            }
        }

        fallback_ptr = &fallback;
    }

    /* Snapshot the active idx before we overwrite cache so we can detect
     * whether the edit landed on the live session's slot. */
    int active_idx = stratum_get_active_pool_idx();

    // Set pools atomically
    bb_err_t err = config_set_pools(&primary, fallback_ptr);
    if (err != BB_OK) {
        bb_json_free(root);
        bb_http_resp_set_status(req, 500);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Failed to save config");
        bb_http_resp_json_obj_end(&e);
        return err;
    }

    bb_json_free(root);

    /* If the active slot was edited (any field), force a stratum reconnect
     * so the new config takes effect on a fresh session. The UI mirrors
     * the switch-pool overlay during this window. Inactive-slot edits
     * just persist; user picks them up via Switch or auto-failover. */
    if (active_idx == POOL_PRIMARY ||
        active_idx == POOL_FALLBACK) {
        stratum_request_reconnect();
    }

    return bb_http_resp_no_content(req);
}

// ---------------------------------------------------------------------------
// /api/pool/switch — POST (TA-290/TA-202 phase C)
// Switch active pool to primary (idx=0) or fallback (idx=1).
// ---------------------------------------------------------------------------

static bb_err_t pool_switch_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    char body[64];
    int body_len = bb_http_req_body_len(req);
    if (body_len > (int)(sizeof(body) - 1)) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Body too large");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Empty body");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    body[len] = '\0';

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Invalid JSON");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    int idx = -1;
    bb_json_t j = bb_json_obj_get_item(root, "idx");
    if (j && bb_json_item_is_number(j)) {
        idx = (int)bb_json_item_get_double(j);
    }

    if (idx != 0 && idx != 1) {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "idx must be 0 or 1");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    bb_err_t err = stratum_request_switch_pool(idx);
    bb_json_free(root);

    if (err == BB_ERR_INVALID_ARG) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        char msg[64];
        snprintf(msg, sizeof(msg), "pool slot %d is not configured", idx);
        bb_http_resp_json_obj_set_str(&e, "error", msg);
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    } else if (err != BB_OK) {
        bb_http_resp_set_status(req, 500);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Failed to switch pool");
        bb_http_resp_json_obj_end(&e);
        return err;
    }

    // 204 No Content on success
    return bb_http_resp_no_content(req);
}

// ---------------------------------------------------------------------------
// /api/pool/fallback — DELETE: clear fallback slot.
// /api/pool/primary  — DELETE: promote fallback to primary, clear fallback.
//                              409 if no fallback configured.
// ---------------------------------------------------------------------------

static bb_err_t pool_delete_fallback_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    if (!config_pool_configured(POOL_FALLBACK)) {
        // Idempotent: already absent → 204.
        return bb_http_resp_no_content(req);
    }

    pool_cfg_t primary = {0};
    strncpy(primary.host,   config_pool_host_idx(POOL_PRIMARY),   sizeof(primary.host)   - 1);
    primary.port = config_pool_port_idx(POOL_PRIMARY);
    strncpy(primary.wallet, config_wallet_addr_idx(POOL_PRIMARY), sizeof(primary.wallet) - 1);
    strncpy(primary.worker, config_worker_name_idx(POOL_PRIMARY), sizeof(primary.worker) - 1);
    strncpy(primary.pass,   config_pool_pass_idx(POOL_PRIMARY),   sizeof(primary.pass)   - 1);

    bb_err_t err = config_set_pools(&primary, NULL);
    if (err != BB_OK) {
        bb_http_resp_set_status(req, 500);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "save failed");
        bb_http_resp_json_obj_end(&e);
        return err;
    }

    // If we were running on the fallback, fall back to primary.
    if (stratum_get_active_pool_idx() == POOL_FALLBACK) {
        stratum_request_switch_pool(POOL_PRIMARY);
    }

    return bb_http_resp_no_content(req);
}

static bb_err_t pool_delete_primary_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    if (!config_pool_configured(POOL_FALLBACK)) {
        bb_http_resp_set_status(req, 409);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "cannot remove primary without a fallback configured");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_STATE;
    }

    // Snapshot fallback into a struct, write it as the new primary, clear fallback.
    pool_cfg_t new_primary = {0};
    strncpy(new_primary.host,   config_pool_host_idx(POOL_FALLBACK),   sizeof(new_primary.host)   - 1);
    new_primary.port = config_pool_port_idx(POOL_FALLBACK);
    strncpy(new_primary.wallet, config_wallet_addr_idx(POOL_FALLBACK), sizeof(new_primary.wallet) - 1);
    strncpy(new_primary.worker, config_worker_name_idx(POOL_FALLBACK), sizeof(new_primary.worker) - 1);
    strncpy(new_primary.pass,   config_pool_pass_idx(POOL_FALLBACK),   sizeof(new_primary.pass)   - 1);

    bb_err_t err = config_set_pools(&new_primary, NULL);
    if (err != BB_OK) {
        bb_http_resp_set_status(req, 500);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "save failed");
        bb_http_resp_json_obj_end(&e);
        return err;
    }

    // Force a reconnect onto the new primary regardless of which slot was active.
    stratum_request_switch_pool(POOL_PRIMARY);

    return bb_http_resp_no_content(req);
}

#ifdef ASIC_CHIP
static bb_err_t power_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    power_snapshot_t s = {
        .vcore_mv      = -1,
        .icore_ma      = -1,
        .pcore_mw      = -1,
        .vin_mv        = -1,
        .asic_hashrate = 0,
        .board_temp_c  = -1.0f,
        .vr_temp_c     = -1.0f,
        .nominal_vin_mv = BOARD_NOMINAL_VIN_MV,
        .efficiency_jth_1m = -1.0,
        .efficiency_jth_10m = -1.0,
        .efficiency_jth_1h = -1.0,
        .expected_efficiency_jth = -1.0,
    };

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s.vcore_mv      = mining_stats.vcore_mv;
        s.icore_ma      = mining_stats.icore_ma;
        s.pcore_mw      = mining_stats.pcore_mw;
        s.vin_mv        = mining_stats.vin_mv;
        s.asic_hashrate = mining_stats.asic_hashrate;
        s.board_temp_c  = mining_stats.board_temp_c;
        s.vr_temp_c     = mining_stats.vr_temp_c;

        float pcore_1m = mining_stats.pcore_mw_1m;
        float pcore_10m = mining_stats.pcore_mw_10m;
        float pcore_1h = mining_stats.pcore_mw_1h;
        float ghs_1m = mining_stats.asic_total_ghs_1m;
        float ghs_10m = mining_stats.asic_total_ghs_10m;
        float ghs_1h = mining_stats.asic_total_ghs_1h;

        if (pcore_1m > 0 && ghs_1m > 0) {
            s.efficiency_jth_1m = (double)pcore_1m / (double)ghs_1m;
        }
        if (pcore_10m > 0 && ghs_10m > 0) {
            s.efficiency_jth_10m = (double)pcore_10m / (double)ghs_10m;
        }
        if (pcore_1h > 0 && ghs_1h > 0) {
            s.efficiency_jth_1h = (double)pcore_1h / (double)ghs_1h;
        }

        float asic_freq = mining_stats.asic_freq_configured_mhz;
        if (s.vcore_mv > 0 && s.icore_ma > 0 && asic_freq > 0) {
            double expected_w = (double)s.vcore_mv / 1000.0 * (double)s.icore_ma / 1000.0;
            double expected_ghs = 0.0;
            if (mining_get_expected_ghs(asic_freq, &expected_ghs) && expected_ghs > 0) {
                double expected_th = expected_ghs / 1000.0;
                s.expected_efficiency_jth = expected_w / expected_th;
            }
        }

        xSemaphoreGive(mining_stats.mutex);
    }

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;

    if (s.vcore_mv >= 0)  bb_http_resp_json_obj_set_int(&obj, "vcore_mv", (int64_t)s.vcore_mv);
    else                  bb_http_resp_json_obj_set_null(&obj, "vcore_mv");
    if (s.icore_ma >= 0)  bb_http_resp_json_obj_set_int(&obj, "icore_ma", (int64_t)s.icore_ma);
    else                  bb_http_resp_json_obj_set_null(&obj, "icore_ma");
    if (s.pcore_mw >= 0)  bb_http_resp_json_obj_set_int(&obj, "pcore_mw", (int64_t)s.pcore_mw);
    else                  bb_http_resp_json_obj_set_null(&obj, "pcore_mw");
    if (s.pcore_mw > 0 && s.asic_hashrate > 0) {
        bb_http_resp_json_obj_set_num(&obj, "efficiency_jth",
                                     (s.pcore_mw / 1000.0) / (s.asic_hashrate / 1e12));
    } else {
        bb_http_resp_json_obj_set_null(&obj, "efficiency_jth");
    }
    if (s.efficiency_jth_1m >= 0.0)          bb_http_resp_json_obj_set_num(&obj, "efficiency_jth_1m", s.efficiency_jth_1m);
    else                                      bb_http_resp_json_obj_set_null(&obj, "efficiency_jth_1m");
    if (s.efficiency_jth_10m >= 0.0)         bb_http_resp_json_obj_set_num(&obj, "efficiency_jth_10m", s.efficiency_jth_10m);
    else                                     bb_http_resp_json_obj_set_null(&obj, "efficiency_jth_10m");
    if (s.efficiency_jth_1h >= 0.0)          bb_http_resp_json_obj_set_num(&obj, "efficiency_jth_1h", s.efficiency_jth_1h);
    else                                     bb_http_resp_json_obj_set_null(&obj, "efficiency_jth_1h");
    if (s.expected_efficiency_jth >= 0.0)    bb_http_resp_json_obj_set_num(&obj, "expected_efficiency_jth", s.expected_efficiency_jth);
    else                                     bb_http_resp_json_obj_set_null(&obj, "expected_efficiency_jth");
    if (s.vin_mv >= 0) {
        bb_http_resp_json_obj_set_int(&obj, "vin_mv", (int64_t)s.vin_mv);
        bool vin_low = (s.vin_mv < (s.nominal_vin_mv + 500) * 87 / 100);
        bb_http_resp_json_obj_set_bool(&obj, "vin_low", vin_low);
    } else {
        bb_http_resp_json_obj_set_null(&obj, "vin_mv");
        bb_http_resp_json_obj_set_null(&obj, "vin_low");
    }
    if (s.board_temp_c >= 0.0f) bb_http_resp_json_obj_set_num(&obj, "board_temp_c", (double)s.board_temp_c);
    else                        bb_http_resp_json_obj_set_null(&obj, "board_temp_c");
    if (s.vr_temp_c >= 0.0f)    bb_http_resp_json_obj_set_num(&obj, "vr_temp_c", (double)s.vr_temp_c);
    else                        bb_http_resp_json_obj_set_null(&obj, "vr_temp_c");

    return bb_http_resp_json_obj_end(&obj);
}

static bb_err_t fan_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    fan_snapshot_t s = {
        .fan_rpm      = -1,
        .fan_duty_pct = -1,
        .autofan      = config_autofan_enabled(),
        .die_target_c = (int)config_die_target_c(),
        .vr_target_c  = (int)config_vr_target_c(),
        .manual_pct    = (int)config_manual_fan_pct(),
        .min_pct       = (int)config_min_fan_pct(),
        .die_ema_c     = -1.0f,
        .vr_ema_c      = -1.0f,
        .pid_input_c   = -1.0f,
        .pid_input_src = "",
    };

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s.fan_rpm      = mining_stats.fan_rpm;
        s.fan_duty_pct = mining_stats.fan_duty_pct;
        xSemaphoreGive(mining_stats.mutex);
    }

    // TA-141: Fetch autofan telemetry (die/vr EMAs, PID input source)
    asic_task_get_autofan_telemetry(&s.die_ema_c, &s.vr_ema_c, &s.pid_input_c, &s.pid_input_src);

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;

    if (s.fan_rpm >= 0)      bb_http_resp_json_obj_set_int(&obj, "rpm",       (int64_t)s.fan_rpm);
    else                     bb_http_resp_json_obj_set_null(&obj, "rpm");
    if (s.fan_duty_pct >= 0) bb_http_resp_json_obj_set_int(&obj, "duty_pct",  (int64_t)s.fan_duty_pct);
    else                     bb_http_resp_json_obj_set_null(&obj, "duty_pct");
    bb_http_resp_json_obj_set_bool(&obj, "autofan", s.autofan);
    if (s.die_target_c >= 0) bb_http_resp_json_obj_set_int(&obj, "die_target_c", (int64_t)s.die_target_c);
    else                     bb_http_resp_json_obj_set_null(&obj, "die_target_c");
    if (s.vr_target_c >= 0)  bb_http_resp_json_obj_set_int(&obj, "vr_target_c",  (int64_t)s.vr_target_c);
    else                     bb_http_resp_json_obj_set_null(&obj, "vr_target_c");
    if (s.manual_pct >= 0)   bb_http_resp_json_obj_set_int(&obj, "manual_pct",   (int64_t)s.manual_pct);
    else                     bb_http_resp_json_obj_set_null(&obj, "manual_pct");
    if (s.min_pct >= 0)      bb_http_resp_json_obj_set_int(&obj, "min_pct",       (int64_t)s.min_pct);
    else                     bb_http_resp_json_obj_set_null(&obj, "min_pct");
    if (s.die_ema_c >= 0.0f) bb_http_resp_json_obj_set_num(&obj, "die_ema_c", (double)s.die_ema_c);
    else                     bb_http_resp_json_obj_set_null(&obj, "die_ema_c");
    if (s.vr_ema_c >= 0.0f)  bb_http_resp_json_obj_set_num(&obj, "vr_ema_c",  (double)s.vr_ema_c);
    else                     bb_http_resp_json_obj_set_null(&obj, "vr_ema_c");
    if (s.pid_input_c >= 0.0f) bb_http_resp_json_obj_set_num(&obj, "pid_input_c", (double)s.pid_input_c);
    else                       bb_http_resp_json_obj_set_null(&obj, "pid_input_c");
    bb_http_resp_json_obj_set_str(&obj, "pid_input_src", s.pid_input_src);

    return bb_http_resp_json_obj_end(&obj);
}

// TA-315: POST /api/fan — update autofan config fields (form-urlencoded, partial)
static bb_err_t fan_post_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    char body[128];
    int body_len = bb_http_req_body_len(req);
    if (body_len > (int)(sizeof(body) - 1)) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Body too large");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Empty body");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    body[len] = '\0';

    // Parse optional fields; only fields present in the body are updated
    char val[16];

    bb_url_decode_field(body, "autofan", val, sizeof(val));
    if (val[0] != '\0') {
        bool autofan = false;
        if (!bb_url_parse_bool(val, &autofan)) {
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "Invalid autofan value");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }
        config_set_autofan_enabled(autofan);
    }

    bb_url_decode_field(body, "die_target_c", val, sizeof(val));
    if (val[0] != '\0') {
        unsigned long temp = 0;
        if (!bb_url_parse_uint(val, &temp)) {
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "Invalid die_target_c value");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }
        config_set_die_target_c((uint16_t)temp);
    }

    bb_url_decode_field(body, "vr_target_c", val, sizeof(val));
    if (val[0] != '\0') {
        unsigned long temp = 0;
        if (!bb_url_parse_uint(val, &temp)) {
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "Invalid vr_target_c value");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }
        config_set_vr_target_c((uint16_t)temp);
    }

    bb_url_decode_field(body, "manual_pct", val, sizeof(val));
    if (val[0] != '\0') {
        unsigned long pct = 0;
        if (!bb_url_parse_uint(val, &pct)) {
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "Invalid manual_pct value");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }
        config_set_manual_fan_pct((uint16_t)pct);
    }

    bb_url_decode_field(body, "min_pct", val, sizeof(val));
    if (val[0] != '\0') {
        unsigned long pct = 0;
        if (!bb_url_parse_uint(val, &pct)) {
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "Invalid min_pct value");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }
        config_set_min_fan_pct((uint16_t)pct);
    }

    return bb_http_resp_no_content(req);
}
#endif // ASIC_BM1370 || ASIC_BM1368

/* Context for knot_walk callback */
typedef struct {
    bb_http_json_stream_t *stream;
    int64_t now_us;
} knot_emit_ctx_t;

/* Callback for knot_walk: build one peer JSON object and emit it */
static bool knot_emit_peer_cb(const knot_peer_t *peer, void *ctx_vp)
{
    knot_emit_ctx_t *ctx = (knot_emit_ctx_t *)ctx_vp;
    bb_json_t obj = build_knot_peer_json(peer, ctx->now_us);
    bb_http_resp_json_arr_emit(ctx->stream, obj);
    bb_json_free(obj);
    return ctx->stream->_err == BB_OK;
}

static bb_err_t knot_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    bb_http_json_stream_t stream;
    bb_err_t err = bb_http_resp_json_arr_begin(req, &stream);
    if (err != BB_OK) {
        return err;
    }

    int64_t now_us = (int64_t)bb_timer_now_us();
    knot_emit_ctx_t ctx = {
        .stream = &stream,
        .now_us = now_us,
    };

    knot_walk(knot_emit_peer_cb, &ctx);
    return bb_http_resp_json_arr_end(&stream);
}

static void taipan_info_extender(bb_json_t root)
{
    bb_json_obj_set_string(root, "worker_name", config_worker_name());
    bb_json_obj_set_string(root, "hostname", config_hostname());

    const char *ssid = bb_nv_config_wifi_ssid();
    if (ssid) bb_json_obj_set_string(root, "ssid", ssid);

    bb_json_obj_set_bool(root, "validated", !bb_ota_is_pending());
    bb_json_obj_set_number(root, "wdt_resets", (double)bb_diag_abnormal_reset_count());

    // TA-339: per-device HW SHA peripheral ceiling (boot microbench).
    // Absent on boards without HW SHA microbench (e.g. D0/DPORT).
    double sha_us, sha_khs;
    if (mining_get_sha_microbench(&sha_us, &sha_khs)) {
        bb_json_obj_set_number(root, "sha_us_per_op", sha_us);
        bb_json_obj_set_number(root, "sha_khs_ceiling", sha_khs);
    }

    // TA-320a: SHA TEXT-overlap canary result. Drives the decision to
    // overlap next-pass TEXT writes during current busy-wait.
    sha_overlap_state_t ov = mining_get_sha_overlap_state();
    if (ov != SHA_OVERLAP_UNKNOWN) {
        bb_json_obj_set_string(root, "sha_overlap",
                               ov == SHA_OVERLAP_SAFE ? "safe" : "unsafe");
    }
    sha_overlap_state_t hw = mining_get_sha_hwrite_state();
    if (hw != SHA_OVERLAP_UNKNOWN) {
        bb_json_obj_set_string(root, "sha_hwrite",
                               hw == SHA_OVERLAP_SAFE ? "safe" : "unsafe");
    }

    time_t now = time(NULL);
    if (now > 1700000000) {
        int64_t uptime_s = (int64_t)bb_timer_now_us() / 1000000LL;
        bb_json_obj_set_number(root, "boot_time", (double)(now - uptime_s));
    }

    bb_json_t network = bb_json_obj_get_item(root, "network");
    if (network) {
        bb_json_obj_set_bool(network, "mdns", bb_mdns_started());
        bb_json_obj_set_bool(network, "stratum", stratum_is_connected());
        bb_json_obj_set_number(network, "stratum_reconnect_ms", stratum_get_reconnect_delay_ms());
        bb_json_obj_set_number(network, "stratum_fail_count", stratum_get_connect_fail_count());
    }
}

static void taipan_health_extender(bb_json_t root)
{
    /* Live liveness signals for the System page. /api/info still owns the
     * one-shot fields (board, MAC, IP, reset_reason, etc.) — anything that
     * changes during the session belongs here. */
    bb_json_obj_set_bool(root, "sha_self_test_failed", mining_sha_self_test_failed());
    bb_json_t network = bb_json_obj_get_item(root, "network");
    if (network) {
        bb_json_obj_set_bool(network, "stratum", stratum_is_connected());
        bb_json_obj_set_number(network, "stratum_fail_count",
                               stratum_get_connect_fail_count());
        bb_json_obj_set_bool(network, "knot", knot_is_running());
    }
}

bb_err_t webui_register_info_extender(void)
{
    bb_err_t err = bb_info_register_extender(taipan_info_extender);
    if (err != BB_OK) return err;
    return bb_health_register_extender(taipan_health_extender);
}

static bb_err_t settings_get_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    settings_snapshot_t s = {0};
    {
        const char *hn = config_hostname();
        if (hn) strncpy(s.hostname, hn, sizeof(s.hostname) - 1);
    }
    s.display_en     = bb_nv_config_display_enabled();
    s.ota_skip_check = bb_nv_config_ota_skip_check();
    s.mdns_en        = bb_nv_config_mdns_enabled();
    s.knot_en        = config_knot_enabled();
    s.provisioned    = bb_nv_config_is_provisioned();

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;
    bb_http_resp_json_obj_set_str(&obj, "hostname",       s.hostname);
    bb_http_resp_json_obj_set_bool(&obj, "display_en",    s.display_en);
    bb_http_resp_json_obj_set_bool(&obj, "ota_skip_check", s.ota_skip_check);
    bb_http_resp_json_obj_set_bool(&obj, "mdns_en",       s.mdns_en);
    bb_http_resp_json_obj_set_bool(&obj, "knot_en",       s.knot_en);
    bb_http_resp_json_obj_set_bool(&obj, "provisioned",   s.provisioned);
    return bb_http_resp_json_obj_end(&obj);
}

// Shared helper for POST (full) and PATCH (partial) settings
// Returns BB_OK on successful response send; sets *out_reboot_required to true if restart needed
static bb_err_t apply_settings(bb_http_request_t *req, bool partial, bool *out_reboot_required)
{
    *out_reboot_required = false;

    set_common_headers(req);
    char body[512];

    int body_len = bb_http_req_body_len(req);
    if (body_len > (int)(sizeof(body) - 1)) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Body too large");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Empty body");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    body[len] = '\0';

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Invalid JSON");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    // Extract fields — use current values as defaults for PATCH
    const char *pool_host = config_pool_host();
    uint16_t pool_port = config_pool_port();
    const char *wallet = config_wallet_addr();
    const char *worker = config_worker_name();
    const char *pool_pass = config_pool_pass();
    bool reboot_required = false;

    bb_json_t j;

    j = bb_json_obj_get_item(root, "pool_host");
    if (j && bb_json_item_is_string(j)) { pool_host = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e); bb_http_resp_json_obj_set_str(&e, "error", "pool_host required"); bb_http_resp_json_obj_end(&e); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "pool_port");
    if (j && bb_json_item_is_number(j)) { pool_port = (uint16_t)bb_json_item_get_double(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e); bb_http_resp_json_obj_set_str(&e, "error", "pool_port required"); bb_http_resp_json_obj_end(&e); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "wallet");
    if (j && bb_json_item_is_string(j)) { wallet = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e); bb_http_resp_json_obj_set_str(&e, "error", "wallet required"); bb_http_resp_json_obj_end(&e); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "worker");
    if (j && bb_json_item_is_string(j)) { worker = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e); bb_http_resp_json_obj_set_str(&e, "error", "worker required"); bb_http_resp_json_obj_end(&e); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "pool_pass");
    if (j && bb_json_item_is_string(j)) { pool_pass = bb_json_item_get_string(j); }
    // pool_pass is optional even for POST

    // Compare against current values to determine if reboot is needed
    if (strcmp(pool_host, config_pool_host()) != 0 ||
        pool_port != config_pool_port() ||
        strcmp(wallet, config_wallet_addr()) != 0 ||
        strcmp(worker, config_worker_name()) != 0 ||
        strcmp(pool_pass, config_pool_pass()) != 0) {
        reboot_required = true;
    }

    // Validate
    if (pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "pool_host, wallet, worker must not be empty");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    if (pool_port == 0) {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "pool_port must be > 0");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    // Save mining config if any mining field was provided
    if (reboot_required) {
        bb_err_t err = config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != BB_OK) {
            bb_json_free(root);
            bb_http_resp_set_status(req, 500);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "Failed to save config");
            bb_http_resp_json_obj_end(&e);
            return err;
        }
    }

    // Handle display_en separately (takes effect immediately, no reboot needed)
    {
        bool display_val = false;
        if (bb_json_obj_get_bool(root, "display_en", &display_val)) {
            bb_err_t err = bb_nv_config_set_display_enabled(display_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_set_status(req, 500);
                bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
                bb_http_resp_json_obj_set_str(&e, "error", "Failed to save display setting");
                bb_http_resp_json_obj_end(&e);
                return err;
            }
        }
    }

    {
        bool skip_val = false;
        if (bb_json_obj_get_bool(root, "ota_skip_check", &skip_val)) {
            bb_err_t err = bb_nv_config_set_ota_skip_check(skip_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_set_status(req, 500);
                bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
                bb_http_resp_json_obj_set_str(&e, "error", "Failed to save ota_skip_check");
                bb_http_resp_json_obj_end(&e);
                return err;
            }
        }
    }

    bb_json_free(root);

    // Response
    bb_http_json_obj_stream_t obj;
    bb_err_t send_rc = bb_http_resp_json_obj_begin(req, &obj);
    if (send_rc != BB_OK) return send_rc;
    bb_http_resp_json_obj_set_str(&obj, "status", "saved");
    bb_http_resp_json_obj_set_bool(&obj, "reboot_required", reboot_required);
    send_rc = bb_http_resp_json_obj_end(&obj);

    if (send_rc == BB_OK) {
        *out_reboot_required = reboot_required;
    }

    return send_rc;
}

static bb_err_t settings_post_handler(bb_http_request_t *req)
{
    bool reboot_required = false;
    bb_err_t rc = apply_settings(req, false, &reboot_required);

    if (rc == BB_OK && reboot_required) {
        schedule_deferred_restart();
    }

    return rc;
}

// ============================================================================
// PATCH HANDLER
// ============================================================================

static bb_err_t settings_patch_handler(bb_http_request_t *req)
{
    // Inline the apply_settings logic with bb_http_* API
    set_common_headers(req);
    char body[512];

    int body_len = bb_http_req_body_len(req);
    if (body_len > (int)(sizeof(body) - 1)) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Body too large");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Empty body");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }
    body[len] = '\0';

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "Invalid JSON");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    // TA-290 phase C: reject pool fields from PATCH (moved to /api/pool)
    if (bb_json_obj_get_item(root, "pool_host") ||
        bb_json_obj_get_item(root, "pool_port") ||
        bb_json_obj_get_item(root, "wallet") ||
        bb_json_obj_get_item(root, "worker") ||
        bb_json_obj_get_item(root, "pool_pass")) {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "pool config moved to /api/pool");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    // Extract fields — use current values as defaults for PATCH
    const char *hostname = config_hostname();
    bool reboot_required = false;

    bb_json_t j;

    j = bb_json_obj_get_item(root, "hostname");
    if (j && bb_json_item_is_string(j)) { hostname = bb_json_item_get_string(j); }

    // Check if hostname was provided and validate it
    bool hostname_changed = false;
    if (bb_json_obj_get_item(root, "hostname")) {
        if (strcmp(hostname, config_hostname()) != 0) {
            hostname_changed = true;
            if (hostname[0] != '\0' && config_set_hostname(hostname) != BB_OK) {
                bb_json_free(root);
                bb_http_resp_set_status(req, 400);
                bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
                bb_http_resp_json_obj_set_str(&e, "error", "invalid hostname");
                bb_http_resp_json_obj_end(&e);
                return BB_ERR_INVALID_ARG;
            }
        }
    }

    // Determine if reboot is needed (hostname changes require reboot)
    if (hostname_changed) {
        reboot_required = true;
    }

    // Handle display_en separately (takes effect immediately, no reboot needed)
    {
        bool display_val = false;
        if (bb_json_obj_get_bool(root, "display_en", &display_val)) {
            bb_err_t err = bb_nv_config_set_display_enabled(display_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_set_status(req, 500);
                bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
                bb_http_resp_json_obj_set_str(&e, "error", "Failed to save display setting");
                bb_http_resp_json_obj_end(&e);
                return err;
            }
        }
    }

    {
        bool skip_val = false;
        if (bb_json_obj_get_bool(root, "ota_skip_check", &skip_val)) {
            bb_err_t err = bb_nv_config_set_ota_skip_check(skip_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_set_status(req, 500);
                bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
                bb_http_resp_json_obj_set_str(&e, "error", "Failed to save ota_skip_check");
                bb_http_resp_json_obj_end(&e);
                return err;
            }
        }
    }

    // Handle mdns_en and knot_en separately (live apply, no reboot needed)
    {
        bool has_mdns = bb_json_obj_get_item(root, "mdns_en") != NULL;
        bool has_knot = bb_json_obj_get_item(root, "knot_en") != NULL;

        bool mdns_val = bb_nv_config_mdns_enabled();
        bool knot_val = config_knot_enabled();

        if (has_mdns) {
            bb_json_obj_get_bool(root, "mdns_en", &mdns_val);
        }
        if (has_knot) {
            bb_json_obj_get_bool(root, "knot_en", &knot_val);
        }

        // Cross-field validation: knot_en=true requires mdns_en=true
        if (has_knot && knot_val && !mdns_val) {
            bb_json_free(root);
            bb_http_resp_set_status(req, 400);
            bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
            bb_http_resp_json_obj_set_str(&e, "error", "mdns_en must be true to enable knot");
            bb_http_resp_json_obj_end(&e);
            return BB_ERR_INVALID_ARG;
        }

        // Local helper-style block: re-arm knot using the current
        // configured identity. Used both for the "knot_en false->true"
        // transition and the "mdns_en false->true && knot configured on"
        // path.
        #define KNOT_REARM_LIVE() do {                                     \
            knot_init();                                                   \
            char _hn[64];                                                  \
            const char *_h = config_hostname();                            \
            if (_h && _h[0]) {                                             \
                strncpy(_hn, _h, sizeof(_hn) - 1);                         \
                _hn[sizeof(_hn) - 1] = '\0';                               \
            } else {                                                       \
                bb_mdns_build_hostname(config_worker_name(), NULL, _hn,    \
                                       sizeof(_hn));                       \
            }                                                              \
            knot_set_self(_hn, _hn, NULL, config_worker_name(),            \
                          FIRMWARE_BOARD, bb_system_get_version(),         \
                          "mining");                                       \
        } while (0)

        // Handle mdns_en transition: when mdns goes off, knot must go off too
        if (has_mdns) {
            bool current_mdns = bb_nv_config_mdns_enabled();
            if (!mdns_val && current_mdns) {
                // Stopping mDNS: tear down knot first (it depends on
                // mdns browse) then stop mdns itself, then persist.
                knot_deinit();
                bb_mdns_deinit();
                bb_err_t err = bb_nv_config_set_mdns_enabled(false);
                if (err != BB_OK) {
                    bb_json_free(root);
                    bb_http_resp_set_status(req, 500); { bb_http_json_obj_stream_t _e; bb_http_resp_json_obj_begin(req, &_e); bb_http_resp_json_obj_set_str(&_e, "error", "Failed to save mdns_en"); bb_http_resp_json_obj_end(&_e); }
                    return BB_ERR_INVALID_ARG;
                }
            } else if (mdns_val && !current_mdns) {
                // Starting mDNS: persist, then re-arm the service
                // synchronously (bb_mdns_init's got-IP callback won't
                // fire on its own while wifi stays connected). Knot
                // stays off — re-enable explicitly via knot_en.
                bb_err_t err = bb_nv_config_set_mdns_enabled(true);
                if (err != BB_OK) {
                    bb_json_free(root);
                    bb_http_resp_set_status(req, 500); { bb_http_json_obj_stream_t _e; bb_http_resp_json_obj_begin(req, &_e); bb_http_resp_json_obj_set_str(&_e, "error", "Failed to save mdns_en"); bb_http_resp_json_obj_end(&_e); }
                    return BB_ERR_INVALID_ARG;
                }
                bb_mdns_start();
            }
        }

        // Handle knot_en transition: only if mdns is on. Use the
        // RUNTIME state (knot_is_running) rather than persisted config
        // because knot can be torn down by an mdns_en=false transition
        // without flipping its own persisted flag.
        if (has_knot && !has_mdns) {  // Only if mdns_en was not being set in this PATCH
            bool running = knot_is_running();
            if (!knot_val && running) {
                // Stopping knot
                knot_deinit();
                bb_err_t err = config_set_knot_enabled(false);
                if (err != BB_OK) {
                    bb_json_free(root);
                    bb_http_resp_set_status(req, 500); { bb_http_json_obj_stream_t _e; bb_http_resp_json_obj_begin(req, &_e); bb_http_resp_json_obj_set_str(&_e, "error", "Failed to save knot_en"); bb_http_resp_json_obj_end(&_e); }
                    return BB_ERR_INVALID_ARG;
                }
            } else if (knot_val && !running && bb_nv_config_mdns_enabled()) {
                // Starting knot (mdns must be on)
                bb_err_t err = config_set_knot_enabled(true);
                if (err != BB_OK) {
                    bb_json_free(root);
                    bb_http_resp_set_status(req, 500); { bb_http_json_obj_stream_t _e; bb_http_resp_json_obj_begin(req, &_e); bb_http_resp_json_obj_set_str(&_e, "error", "Failed to save knot_en"); bb_http_resp_json_obj_end(&_e); }
                    return BB_ERR_INVALID_ARG;
                }
                KNOT_REARM_LIVE();
            } else if (!knot_val && !running) {
                // Persist the off state even if already torn down so
                // boot-time gating reflects the user's intent.
                bb_err_t err = config_set_knot_enabled(false);
                if (err != BB_OK) {
                    bb_json_free(root);
                    bb_http_resp_set_status(req, 500); { bb_http_json_obj_stream_t _e; bb_http_resp_json_obj_begin(req, &_e); bb_http_resp_json_obj_set_str(&_e, "error", "Failed to save knot_en"); bb_http_resp_json_obj_end(&_e); }
                    return BB_ERR_INVALID_ARG;
                }
            }
        } else if (has_knot && has_mdns) {
            // Both mdns_en and knot_en in same request. mDNS lifecycle
            // already applied above; now apply knot consistently with
            // the (possibly just-changed) mdns state.
            bool running = knot_is_running();
            bb_err_t err = config_set_knot_enabled(knot_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_set_status(req, 500); { bb_http_json_obj_stream_t _e; bb_http_resp_json_obj_begin(req, &_e); bb_http_resp_json_obj_set_str(&_e, "error", "Failed to save knot_en"); bb_http_resp_json_obj_end(&_e); }
                return BB_ERR_INVALID_ARG;
            }
            if (knot_val && mdns_val && !running) {
                KNOT_REARM_LIVE();
            } else if (!knot_val && running) {
                knot_deinit();
            }
        }

        #undef KNOT_REARM_LIVE
    }

    bb_json_free(root);

    // Response
    bb_http_json_obj_stream_t resp_obj;
    bb_err_t send_rc = bb_http_resp_json_obj_begin(req, &resp_obj);
    if (send_rc != BB_OK) return send_rc;
    bb_http_resp_json_obj_set_str(&resp_obj, "status", "saved");
    bb_http_resp_json_obj_set_bool(&resp_obj, "reboot_required", reboot_required);
    send_rc = bb_http_resp_json_obj_end(&resp_obj);

    // schedule deferred restart if reboot is needed
    if (reboot_required) {
        schedule_deferred_restart();
    }

    return send_rc;
}

// ============================================================================
// ROUTE DESCRIPTORS
// ============================================================================

// ---------------------------------------------------------------------------
// /api/stats — GET
// ---------------------------------------------------------------------------

static const bb_route_response_t s_stats_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"hashrate\":{\"type\":\"number\",\"description\":\"ESP32-S3 HW hashrate H/s\"},"
      "\"hashrate_avg\":{\"type\":\"number\",\"description\":\"EMA hashrate H/s\"},"
      "\"temp_c\":{\"type\":\"number\"},"
      "\"shares\":{\"type\":\"integer\",\"description\":\"HW shares found\"},"
      "\"session_shares\":{\"type\":\"integer\"},"
      "\"session_rejected\":{\"type\":\"integer\"},"
      "\"rejected\":{\"type\":\"object\","
      "\"properties\":{"
      "\"total\":{\"type\":\"integer\"},"
      "\"job_not_found\":{\"type\":\"integer\"},"
      "\"low_difficulty\":{\"type\":\"integer\"},"
      "\"duplicate\":{\"type\":\"integer\"},"
      "\"stale_prevhash\":{\"type\":\"integer\"},"
      "\"other\":{\"type\":\"integer\"},"
      "\"other_last_code\":{\"type\":\"integer\"}}},"
      "\"last_share_ago_s\":{\"type\":\"integer\",\"description\":\"-1 if no share yet\"},"
      "\"best_diff\":{\"type\":\"number\"},"
      "\"uptime_s\":{\"type\":\"integer\"},"
      "\"expected_ghs\":{\"type\":\"number\","
      "\"description\":\"theoretical max GH/s for this platform\"},"
      "\"asic_chips\":{\"type\":\"array\","
      "\"items\":{\"type\":\"object\","
      "\"properties\":{"
      "\"idx\":{\"type\":\"integer\"},"
      "\"total_ghs\":{\"type\":\"number\"},"
      "\"error_ghs\":{\"type\":\"number\"},"
      "\"hw_err_pct\":{\"type\":\"number\"},"
      "\"total_raw\":{\"type\":\"integer\"},"
      "\"error_raw\":{\"type\":\"integer\"},"
      "\"total_drops\":{\"type\":\"integer\"},"
      "\"error_drops\":{\"type\":\"integer\"},"
      "\"last_drop_ago_s\":{\"type\":[\"number\",\"null\"]},"
      "\"domain_ghs\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}},"
      "\"domain_drops\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}}}}}}}",
      "mining statistics snapshot" },
    { 0 },
};

static const bb_route_t s_stats_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/stats",
    .tag          = "mining",
    .summary      = "Get mining statistics",
    .operation_id = "getStats",
    .responses    = s_stats_responses,
    .handler      = stats_handler,
};

// ---------------------------------------------------------------------------
// /api/pool — GET (TA-281, TA-286; closes TA-201)
// ---------------------------------------------------------------------------

static const bb_route_response_t s_pool_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"host\":{\"type\":\"string\"},"
      "\"port\":{\"type\":\"integer\"},"
      "\"worker\":{\"type\":\"string\"},"
      "\"wallet\":{\"type\":\"string\"},"
      "\"connected\":{\"type\":\"boolean\"},"
      "\"session_start_ago_s\":{\"type\":[\"integer\",\"null\"]},"
      "\"current_difficulty\":{\"type\":\"number\"},"
      "\"extranonce1\":{\"type\":[\"string\",\"null\"]},"
      "\"extranonce2_size\":{\"type\":[\"integer\",\"null\"]},"
      "\"version_mask\":{\"type\":[\"string\",\"null\"],"
      "\"description\":\"BIP320 mask, 8-char lowercase hex; null if not negotiated\"},"
      "\"notify\":{\"type\":[\"object\",\"null\"],"
      "\"properties\":{"
      "\"job_id\":{\"type\":\"string\"},"
      "\"prev_hash\":{\"type\":\"string\"},"
      "\"coinb1\":{\"type\":\"string\"},"
      "\"coinb2\":{\"type\":\"string\"},"
      "\"merkle_branches\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
      "\"version\":{\"type\":\"string\"},"
      "\"nbits\":{\"type\":\"string\"},"
      "\"ntime\":{\"type\":\"string\"},"
      "\"clean_jobs\":{\"type\":\"boolean\"}}}},"
      "\"stats\":{\"type\":\"array\",\"items\":{\"type\":\"object\","
      "\"properties\":{"
      "\"host\":{\"type\":\"string\"},"
      "\"port\":{\"type\":\"integer\"},"
      "\"shares\":{\"type\":\"integer\"},"
      "\"hashes\":{\"type\":\"integer\"},"
      "\"best_diff\":{\"type\":\"number\"},"
      "\"blocks_found\":{\"type\":\"integer\"},"
      "\"last_seen_s\":{\"type\":\"integer\",\"description\":\"seconds since boot\"}},"
      "\"description\":\"per-pool lifetime statistics\"}},"
      "\"required\":[\"host\",\"port\",\"worker\",\"wallet\",\"connected\","
      "\"session_start_ago_s\",\"current_difficulty\","
      "\"extranonce1\",\"extranonce2_size\",\"version_mask\",\"notify\",\"stats\"]}",
      "pool connection state and current job" },
    { 0 },
};

static const bb_route_t s_pool_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/pool",
    .tag          = "pool",
    .summary      = "Get pool connection state and current job",
    .operation_id = "getPool",
    .responses    = s_pool_responses,
    .handler      = pool_handler,
};

// ---------------------------------------------------------------------------
// /api/pool — PUT (TA-290/TA-202 phase C)
// ---------------------------------------------------------------------------

static const bb_route_response_t s_pool_put_responses[] = {
    { 204, NULL, NULL, "pool config saved" },
    { 400, "text/plain", NULL, "validation error" },
    { 500, "text/plain", NULL, "save failed" },
    { 0 },
};

static const bb_route_t s_pool_put_route = {
    .method               = BB_HTTP_PUT,
    .path                 = "/api/pool",
    .tag                  = "pool",
    .summary              = "Set primary and fallback pool config",
    .operation_id         = "putPool",
    .request_content_type = "application/json",
    .request_schema       =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"primary\":{\"type\":\"object\","
        "\"properties\":{"
        "\"host\":{\"type\":\"string\"},"
        "\"port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535},"
        "\"wallet\":{\"type\":\"string\"},"
        "\"worker\":{\"type\":\"string\"},"
        "\"pool_pass\":{\"type\":\"string\"},"
        "\"extranonce_subscribe\":{\"type\":\"boolean\"},"
        "\"decode_coinbase\":{\"type\":\"boolean\"}},"
        "\"required\":[\"host\",\"port\",\"wallet\",\"worker\"]},"
        "\"fallback\":{\"type\":[\"object\",\"null\"],"
        "\"properties\":{"
        "\"host\":{\"type\":\"string\"},"
        "\"port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535},"
        "\"wallet\":{\"type\":\"string\"},"
        "\"worker\":{\"type\":\"string\"},"
        "\"pool_pass\":{\"type\":\"string\"},"
        "\"extranonce_subscribe\":{\"type\":\"boolean\"},"
        "\"decode_coinbase\":{\"type\":\"boolean\"}},"
        "\"required\":[\"host\",\"port\",\"wallet\",\"worker\"]}},"
        "\"required\":[\"primary\"]}",
    .responses            = s_pool_put_responses,
    .handler              = pool_put_handler,
};

// ---------------------------------------------------------------------------
// /api/pool/switch — POST (TA-290/TA-202 phase C)
// ---------------------------------------------------------------------------

static const bb_route_response_t s_pool_switch_responses[] = {
    { 204, NULL, NULL, "pool switch initiated" },
    { 400, "text/plain", NULL, "validation error or pool not configured" },
    { 500, "text/plain", NULL, "switch failed" },
    { 0 },
};

static const bb_route_t s_pool_switch_route = {
    .method               = BB_HTTP_POST,
    .path                 = "/api/pool/switch",
    .tag                  = "pool",
    .summary              = "Switch active pool",
    .operation_id         = "switchPool",
    .request_content_type = "application/json",
    .request_schema       =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"idx\":{\"type\":\"integer\",\"enum\":[0,1]}},"
        "\"required\":[\"idx\"]}",
    .responses            = s_pool_switch_responses,
    .handler              = pool_switch_handler,
};

static const bb_route_response_t s_pool_delete_fallback_responses[] = {
    { 204, NULL, NULL, "fallback cleared" },
    { 500, "text/plain", NULL, "save failed" },
    { 0 },
};

static const bb_route_t s_pool_delete_fallback_route = {
    .method       = BB_HTTP_DELETE,
    .path         = "/api/pool/fallback",
    .tag          = "pool",
    .summary      = "Clear fallback pool slot",
    .operation_id = "deletePoolFallback",
    .responses    = s_pool_delete_fallback_responses,
    .handler      = pool_delete_fallback_handler,
};

static const bb_route_response_t s_pool_delete_primary_responses[] = {
    { 204, NULL, NULL, "fallback promoted to primary" },
    { 409, "text/plain", NULL, "no fallback configured" },
    { 500, "text/plain", NULL, "save failed" },
    { 0 },
};

static const bb_route_t s_pool_delete_primary_route = {
    .method       = BB_HTTP_DELETE,
    .path         = "/api/pool/primary",
    .tag          = "pool",
    .summary      = "Remove primary pool, promoting fallback",
    .operation_id = "deletePoolPrimary",
    .responses    = s_pool_delete_primary_responses,
    .handler      = pool_delete_primary_handler,
};

// ---------------------------------------------------------------------------
// /api/diag/asic — GET (TA-282, TA-287)
// Recent telemetry-drop log. On ASIC boards, calls asic_task_get_drop_log()
// and serialises up to ASIC_DROP_LOG_CAP entries as recent_drops[].
// On tdongle (no ASIC), returns { "recent_drops": [] } — keeps the webui
// path uniform with no special-casing.
// ---------------------------------------------------------------------------
static bb_err_t diag_asic_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    diag_asic_snapshot_t s = {0};

#ifdef ASIC_CHIP
    asic_drop_event_t drops[ASIC_DROP_LOG_CAP];
    size_t n = asic_task_get_drop_log(drops, ASIC_DROP_LOG_CAP);
    s.now_us  = bb_timer_now_us();
    s.n_drops = n < ROUTES_JSON_DROP_LOG_CAP ? n : ROUTES_JSON_DROP_LOG_CAP;
    for (size_t i = 0; i < s.n_drops; i++) {
        s.drops[i].ts_us      = drops[i].ts_us;
        s.drops[i].chip_idx   = drops[i].chip_idx;
        s.drops[i].kind       = drops[i].kind;
        s.drops[i].domain_idx = drops[i].domain_idx;
        s.drops[i].asic_addr  = drops[i].asic_addr;
        s.drops[i].ghs        = drops[i].ghs;
        s.drops[i].delta      = drops[i].delta;
        s.drops[i].elapsed_s  = drops[i].elapsed_s;
    }
#endif /* ASIC_CHIP */

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;

    bb_http_resp_json_obj_set_arr_begin(&obj, "recent_drops");
    for (size_t i = 0; i < s.n_drops; i++) {
        const routes_json_drop_event_t *d = &s.drops[i];
        uint64_t age_us = (d->ts_us <= s.now_us) ? (s.now_us - d->ts_us) : 0;
        const char *kind_str;
        switch ((routes_json_drop_kind_t)d->kind) {
            case ROUTES_JSON_DROP_KIND_ERROR:  kind_str = "error";  break;
            case ROUTES_JSON_DROP_KIND_DOMAIN: kind_str = "domain"; break;
            default:                           kind_str = "total";  break;
        }
        bb_http_resp_json_obj_set_obj_begin(&obj, NULL);
        bb_http_resp_json_obj_set_num(&obj, "ts_ago_s",  (double)(age_us / 1000000ULL));
        bb_http_resp_json_obj_set_int(&obj, "chip",      (int64_t)d->chip_idx);
        bb_http_resp_json_obj_set_str(&obj, "kind",      kind_str);
        bb_http_resp_json_obj_set_int(&obj, "domain",    (int64_t)d->domain_idx);
        bb_http_resp_json_obj_set_int(&obj, "addr",      (int64_t)d->asic_addr);
        bb_http_resp_json_obj_set_num(&obj, "ghs",       (double)d->ghs);
        bb_http_resp_json_obj_set_int(&obj, "delta",     (int64_t)d->delta);
        bb_http_resp_json_obj_set_num(&obj, "elapsed_s", (double)d->elapsed_s);
        bb_http_resp_json_obj_set_obj_end(&obj);
    }
    bb_http_resp_json_obj_set_arr_end(&obj);

    return bb_http_resp_json_obj_end(&obj);
}

static const bb_route_response_t s_diag_asic_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"recent_drops\":{\"type\":\"array\","
      "\"items\":{\"type\":\"object\","
      "\"properties\":{"
      "\"ts_ago_s\":{\"type\":\"number\",\"description\":\"seconds since drop event\"},"
      "\"chip\":{\"type\":\"integer\"},"
      "\"kind\":{\"type\":\"string\",\"enum\":[\"total\",\"error\",\"domain\"]},"
      "\"domain\":{\"type\":\"integer\"},"
      "\"addr\":{\"type\":\"integer\"},"
      "\"ghs\":{\"type\":\"number\"},"
      "\"delta\":{\"type\":\"integer\"},"
      "\"elapsed_s\":{\"type\":\"number\"}},"
      "\"required\":[\"ts_ago_s\",\"chip\",\"kind\",\"domain\","
      "\"addr\",\"ghs\",\"delta\",\"elapsed_s\"]}}}}",
      "recent ASIC telemetry drop log" },
    { 0 },
};

static const bb_route_t s_diag_asic_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/diag/asic",
    .tag          = "diag",
    .summary      = "Get recent ASIC telemetry drop log",
    .operation_id = "getDiagAsic",
    .responses    = s_diag_asic_responses,
    .handler      = diag_asic_handler,
};

// ---------------------------------------------------------------------------
// /api/knot — GET
// ---------------------------------------------------------------------------

static const bb_route_response_t s_knot_responses[] = {
    { 200, "application/json",
      "{\"type\":\"array\","
      "\"items\":{"
      "\"type\":\"object\","
      "\"properties\":{"
      "\"instance\":{\"type\":\"string\"},"
      "\"hostname\":{\"type\":\"string\"},"
      "\"ip\":{\"type\":\"string\"},"
      "\"worker\":{\"type\":\"string\"},"
      "\"board\":{\"type\":\"string\"},"
      "\"version\":{\"type\":\"string\"},"
      "\"state\":{\"type\":\"string\"},"
      "\"seen_ago_s\":{\"type\":\"integer\"}},"
      "\"required\":[\"instance\",\"hostname\",\"ip\","
      "\"worker\",\"board\",\"version\",\"state\",\"seen_ago_s\"]}}",
      "mDNS-discovered TaipanMiner peer table snapshot" },
    { 0 },
};

static const bb_route_t s_knot_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/knot",
    .tag          = "knot",
    .summary      = "Get peer table snapshot",
    .operation_id = "getKnot",
    .responses    = s_knot_responses,
    .handler      = knot_handler,
};

// ---------------------------------------------------------------------------
// /api/settings — GET, POST, PATCH
// ---------------------------------------------------------------------------

static const bb_route_response_t s_settings_get_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"hostname\":{\"type\":\"string\"},"
      "\"display_en\":{\"type\":\"boolean\"},"
      "\"ota_skip_check\":{\"type\":\"boolean\"},"
      "\"mdns_en\":{\"type\":\"boolean\"},"
      "\"knot_en\":{\"type\":\"boolean\"},"
      "\"provisioned\":{\"type\":\"boolean\","
      "\"description\":\"read-only mirror of the bb_cfg NVS 'provisioned' u8 key; "
      "true after first successful provisioning, false on factory/post-reset. "
      "Settable only via direct NVS write (e.g. taipan-cli flash pre-seed), not HTTP.\"}},"
      "\"required\":[\"hostname\",\"display_en\",\"ota_skip_check\",\"mdns_en\","
      "\"knot_en\",\"provisioned\"]}",
      "current persisted settings" },
    { 0 },
};

static const bb_route_t s_settings_get_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/settings",
    .tag          = "config",
    .summary      = "Get current settings",
    .operation_id = "getSettings",
    .responses    = s_settings_get_responses,
    .handler      = settings_get_handler,
};

static const bb_route_response_t s_settings_write_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"status\":{\"type\":\"string\",\"enum\":[\"saved\"]},"
      "\"reboot_required\":{\"type\":\"boolean\"}},"
      "\"required\":[\"status\",\"reboot_required\"]}",
      "settings saved" },
    { 400, "text/plain", NULL, "validation error" },
    { 500, "text/plain", NULL, "save failed" },
    { 0 },
};

static const bb_route_t s_settings_post_route = {
    .method               = BB_HTTP_POST,
    .path                 = "/api/settings",
    .tag                  = "config",
    .summary              = "Replace settings (full update)",
    .operation_id         = "postSettings",
    .request_content_type = "application/json",
    .request_schema       =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"pool_host\":{\"type\":\"string\"},"
        "\"pool_port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535},"
        "\"wallet\":{\"type\":\"string\"},"
        "\"worker\":{\"type\":\"string\"},"
        "\"pool_pass\":{\"type\":\"string\"},"
        "\"display_en\":{\"type\":\"boolean\"},"
        "\"ota_skip_check\":{\"type\":\"boolean\"}},"
        "\"required\":[\"pool_host\",\"pool_port\",\"wallet\",\"worker\"]}",
    .responses            = s_settings_write_responses,
    .handler              = settings_post_handler,
};

static const bb_route_t s_settings_patch_route = {
    .method               = BB_HTTP_PATCH,
    .path                 = "/api/settings",
    .tag                  = "config",
    .summary              = "Partial settings update",
    .operation_id         = "patchSettings",
    .request_content_type = "application/json",
    .request_schema       =
        "{\"type\":\"object\","
        "\"properties\":{"
        "\"pool_host\":{\"type\":\"string\"},"
        "\"pool_port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65535},"
        "\"wallet\":{\"type\":\"string\"},"
        "\"worker\":{\"type\":\"string\"},"
        "\"pool_pass\":{\"type\":\"string\"},"
        "\"hostname\":{\"type\":\"string\"},"
        "\"display_en\":{\"type\":\"boolean\"},"
        "\"ota_skip_check\":{\"type\":\"boolean\"},"
        "\"mdns_en\":{\"type\":\"boolean\"},"
        "\"knot_en\":{\"type\":\"boolean\"}}}",
    .responses            = s_settings_write_responses,
    .handler              = settings_patch_handler,
};

#ifdef ASIC_CHIP

// ---------------------------------------------------------------------------
// /api/power — GET (ASIC boards only)
// ---------------------------------------------------------------------------

static const bb_route_response_t s_power_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"vcore_mv\":{\"type\":[\"integer\",\"null\"]},"
      "\"icore_ma\":{\"type\":[\"integer\",\"null\"]},"
      "\"pcore_mw\":{\"type\":[\"integer\",\"null\"]},"
      "\"efficiency_jth\":{\"type\":[\"number\",\"null\"],"
      "\"description\":\"J/TH; null until ASIC hashrate and power both available\"},"
      "\"vin_mv\":{\"type\":[\"integer\",\"null\"]},"
      "\"vin_low\":{\"type\":[\"boolean\",\"null\"]},"
      "\"board_temp_c\":{\"type\":[\"number\",\"null\"]},"
      "\"vr_temp_c\":{\"type\":[\"number\",\"null\"]}}}",
      "ASIC power and thermal telemetry" },
    { 0 },
};

static const bb_route_t s_power_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/power",
    .tag          = "mining",
    .summary      = "Get ASIC power and thermal telemetry",
    .operation_id = "getPower",
    .responses    = s_power_responses,
    .handler      = power_handler,
};

// ---------------------------------------------------------------------------
// /api/fan — GET (ASIC boards only)
// ---------------------------------------------------------------------------

static const bb_route_response_t s_fan_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{"
      "\"rpm\":{\"type\":[\"integer\",\"null\"]},"
      "\"duty_pct\":{\"type\":[\"integer\",\"null\"],"
      "\"description\":\"curve-controlled duty %; null until first telemetry tick\"},"
      "\"autofan\":{\"type\":\"boolean\",\"description\":\"autofan enabled\"},"
      "\"die_target_c\":{\"type\":[\"integer\",\"null\"],\"description\":\"ASIC die target temperature\"},"
      "\"vr_target_c\":{\"type\":[\"integer\",\"null\"],\"description\":\"VR target temperature\"},"
      "\"manual_pct\":{\"type\":[\"integer\",\"null\"],\"description\":\"manual duty % when autofan disabled\"},"
      "\"min_pct\":{\"type\":[\"integer\",\"null\"],\"description\":\"minimum fan duty %\"},"
      "\"die_ema_c\":{\"type\":[\"number\",\"null\"],\"description\":\"filtered ASIC die temperature\"},"
      "\"vr_ema_c\":{\"type\":[\"number\",\"null\"],\"description\":\"filtered VR temperature\"},"
      "\"pid_input_c\":{\"type\":[\"number\",\"null\"],\"description\":\"PID input selected by max(err/target) ratio\"},"
      "\"pid_input_src\":{\"type\":\"string\",\"description\":\"which sensor is driving PID: 'die' or 'vr'\"}"
      "}}",
      "fan telemetry" },
    { 0 },
};

static const bb_route_t s_fan_route = {
    .method       = BB_HTTP_GET,
    .path         = "/api/fan",
    .tag          = "mining",
    .summary      = "Get fan speed and duty cycle",
    .operation_id = "getFan",
    .responses    = s_fan_responses,
    .handler      = fan_handler,
};

// TA-315: POST /api/fan — update autofan config (form-urlencoded, all fields optional)
static const bb_route_response_t s_fan_post_responses[] = {
    { 204, NULL, NULL, "Fan config updated" },
    { 0 },
};

static const bb_route_t s_fan_post_route = {
    .method       = BB_HTTP_POST,
    .path         = "/api/fan",
    .tag          = "mining",
    .summary      = "Update autofan configuration",
    .operation_id = "postFan",
    .responses    = s_fan_post_responses,
    .handler      = fan_post_handler,
};

#endif /* ASIC_CHIP */

// ============================================================================
// /api/diag/benchmark — POST (TA-33)
// ============================================================================

static bb_err_t diag_benchmark_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    /* Read body */
    char body[128] = {0};
    int body_len = bb_http_req_body_len(req);
    if (body_len > (int)(sizeof(body) - 1)) body_len = (int)(sizeof(body) - 1);
    if (body_len > 0) {
        int n = bb_http_req_recv(req, body, sizeof(body) - 1);
        if (n > 0) body[n] = '\0';
        else        body[0] = '\0';
        body_len = (n > 0) ? n : 0;
    }

    /* Parse and validate iters */
    uint32_t iters = DIAG_BENCH_ITERS_DEFAULT;
    bb_err_t rc = diag_bench_parse_request(body, body_len, &iters);
    if (rc == BB_ERR_INVALID_ARG) {
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "invalid body");
        bb_http_resp_json_obj_end(&e);
        return rc;
    }
    if (rc == BB_ERR_NO_SPACE) {  /* out-of-range sentinel */
        bb_http_resp_set_status(req, 400);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "iters out of range");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_ARG;
    }

    /* Pause mining */
    bool paused = mining_pause();

    /* Run bench on the active backend */
    sha_bench_result_t bench = {0};

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3
    sha256_hw_acquire();
    sha256_hw_bench_pass2(iters, &bench);
    sha256_hw_release();
    const char *backend = "ahb";
#elif CONFIG_IDF_TARGET_ESP32
    sha256_hw_dport_acquire();
    sha256_hw_dport_bench_pass2(iters, &bench);
    sha256_hw_dport_release();
    const char *backend = "dport";
#else
    sha256_sw_bench_pass2(iters, &bench);
    const char *backend = "sw";
#endif

    /* Read TA-320 canaries (tristate enum: UNKNOWN | SAFE | UNSAFE) */
    sha_overlap_state_t text_overlap = mining_get_sha_overlap_state();
    sha_overlap_state_t hwrite       = mining_get_sha_hwrite_state();

    /* Resume mining */
    if (paused) mining_resume();

    /* Map tristate to JSON string */
    const char *overlap_str;
    switch (text_overlap) {
        case SHA_OVERLAP_SAFE:   overlap_str = "safe";   break;
        case SHA_OVERLAP_UNSAFE: overlap_str = "unsafe"; break;
        default:                 overlap_str = "unknown"; break;
    }

    const char *hwrite_str;
    switch (hwrite) {
        case SHA_OVERLAP_SAFE:   hwrite_str = "safe";   break;
        case SHA_OVERLAP_UNSAFE: hwrite_str = "unsafe"; break;
        default:                 hwrite_str = "unknown"; break;
    }

    /* Build and stream response.
     * khs: use settled portion when settled; fall back to full-window iters/duration_us. */
    double khs;
    if (bench.settled && bench.settled_total_us > 0) {
        khs = (double)bench.settled_iters * 1000.0 / (double)bench.settled_total_us;
    } else {
        khs = (bench.total_us > 0) ? ((double)iters * 1000.0 / (double)bench.total_us) : 0.0;
    }
    /* sha_ops_per_sec: per-SHA-block-op rate (ops/s = 1e6 / us_per_op); us_per_op is steady-state */
    double sha_ops_per_sec = (bench.us_per_op > 0.0) ? (1000000.0 / bench.us_per_op) : 0.0;

    bb_http_json_obj_stream_t obj;
    rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;

    bb_http_resp_json_obj_set_int(&obj, "iters",               (int64_t)iters);
    bb_http_resp_json_obj_set_int(&obj, "duration_us",         bench.total_us);
    bb_http_resp_json_obj_set_num(&obj, "us_per_op",           bench.us_per_op);
    bb_http_resp_json_obj_set_num(&obj, "khs",                 khs);
    bb_http_resp_json_obj_set_num(&obj, "sha_ops_per_sec",    sha_ops_per_sec);
    bb_http_resp_json_obj_set_str(&obj, "backend",             backend);
    bb_http_resp_json_obj_set_bool(&obj, "settled",            bench.settled);
    bb_http_resp_json_obj_set_int(&obj, "settled_after_iters", (int64_t)bench.settled_after_iters);
    bb_http_resp_json_obj_set_int(&obj, "settled_iters",       (int64_t)bench.settled_iters);
    bb_http_resp_json_obj_set_int(&obj, "settled_total_us",    bench.settled_total_us);

    bb_http_resp_json_obj_set_obj_begin(&obj, "canary");
    bb_http_resp_json_obj_set_str(&obj, "text_overlap", overlap_str);
    bb_http_resp_json_obj_set_str(&obj, "h_write",      hwrite_str);
    bb_http_resp_json_obj_set_obj_end(&obj);

#ifdef ASIC_CHIP
    bb_http_resp_json_obj_set_bool(&obj, "asic_active", true);
#endif

    return bb_http_resp_json_obj_end(&obj);
}

static const bb_route_response_t s_diag_benchmark_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"required\":[\"iters\",\"duration_us\",\"us_per_op\",\"khs\",\"sha_ops_per_sec\",\"backend\",\"canary\"],"
      "\"properties\":{"
      "\"iters\":{\"type\":\"integer\"},"
      "\"duration_us\":{\"type\":\"integer\"},"
      "\"us_per_op\":{\"type\":\"number\",\"description\":\"per-SHA-block-op latency\"},"
      "\"khs\":{\"type\":\"number\",\"description\":\"nonce-domain kH/s: iters*1000/duration_us\"},"
      "\"sha_ops_per_sec\":{\"type\":\"number\",\"description\":\"SHA-block-op rate: 1e6/us_per_op\"},"
      "\"backend\":{\"type\":\"string\",\"enum\":[\"sw\",\"ahb\",\"dport\"]},"
      "\"canary\":{\"type\":\"object\","
      "\"properties\":{"
      "\"text_overlap\":{\"type\":\"string\",\"enum\":[\"safe\",\"unsafe\",\"unknown\"]},"
      "\"h_write\":{\"type\":\"string\",\"enum\":[\"safe\",\"unsafe\",\"unknown\"]}}},"
      "\"asic_active\":{\"type\":\"boolean\","
      "\"description\":\"present only on ASIC boards; ASIC keeps hashing during bench\"}}}",
      "benchmark result" },
    { 400, "application/json", NULL, "iters out of range or invalid body" },
    { 0 },
};

static const bb_route_t s_diag_benchmark_route = {
    .method       = BB_HTTP_POST,
    .path         = "/api/diag/benchmark",
    .tag          = "diag",
    .summary      = "Run on-demand SHA throughput benchmark",
    .operation_id = "postDiagBenchmark",
    .responses    = s_diag_benchmark_responses,
    .handler      = diag_benchmark_handler,
};

// ============================================================================
// REGISTRATION
// ============================================================================

void webui_install_prov_save_cb(void)
{
    bb_prov_set_save_callback(taipan_prov_save_cb);
}

static bb_http_asset_t s_prov_assets[] = {
    { "/",                  "text/html",              "gzip", NULL, 0 },
    { "/assets/index.js",   "application/javascript", "gzip", NULL, 0 },
    { "/assets/index.css",  "text/css",               "gzip", NULL, 0 },
    { "/logo.svg",          "image/svg+xml",          "gzip", NULL, 0 },
    { "/favicon.svg",       "image/svg+xml",          "gzip", NULL, 0 },
};

const bb_http_asset_t *webui_prov_assets(size_t *n)
{
    // Initialize asset data on first call
    static bool initialized = false;
    if (!initialized) {
        s_prov_assets[0].data = prov_index_html_gz;
        s_prov_assets[0].len = prov_index_html_gz_len;
        s_prov_assets[1].data = prov_index_js_gz;
        s_prov_assets[1].len = prov_index_js_gz_len;
        s_prov_assets[2].data = prov_index_css_gz;
        s_prov_assets[2].len = prov_index_css_gz_len;
        s_prov_assets[3].data = logo_svg_gz;
        s_prov_assets[3].len = logo_svg_gz_len;
        s_prov_assets[4].data = favicon_svg_gz;
        s_prov_assets[4].len = favicon_svg_gz_len;
        initialized = true;
    }
    *n = sizeof(s_prov_assets) / sizeof(s_prov_assets[0]);
    return s_prov_assets;
}

static bb_http_asset_t s_mining_assets[] = {
    { "/",                       "text/html",              "gzip", NULL, 0 },
    { "/assets/index.js",        "application/javascript", "gzip", NULL, 0 },
    { "/assets/index.css",       "text/css",               "gzip", NULL, 0 },
    { "/assets/runtime.js",      "application/javascript", "gzip", NULL, 0 },
    { "/assets/vendor.js",       "application/javascript", "gzip", NULL, 0 },
    { "/assets/index2.js",       "application/javascript", "gzip", NULL, 0 },
    { "/assets/index3.js",       "application/javascript", "gzip", NULL, 0 },
    { "/assets/Pool.js",         "application/javascript", "gzip", NULL, 0 },
    { "/assets/Update.js",       "application/javascript", "gzip", NULL, 0 },
    { "/assets/Diagnostics.js",  "application/javascript", "gzip", NULL, 0 },
    { "/assets/Settings.js",     "application/javascript", "gzip", NULL, 0 },
    { "/assets/History.js",      "application/javascript", "gzip", NULL, 0 },
    { "/assets/Knot.js",         "application/javascript", "gzip", NULL, 0 },
    { "/logo.svg",               "image/svg+xml",          "gzip", NULL, 0 },
    { "/favicon.svg",            "image/svg+xml",          "gzip", NULL, 0 },
};

static void init_mining_assets(void)
{
    static bool initialized = false;
    if (!initialized) {
        s_mining_assets[0].data  = index_html_gz;
        s_mining_assets[0].len  = index_html_gz_len;
        s_mining_assets[1].data  = index_js_gz;
        s_mining_assets[1].len  = index_js_gz_len;
        s_mining_assets[2].data  = index_css_gz;
        s_mining_assets[2].len  = index_css_gz_len;
        s_mining_assets[3].data  = runtime_js_gz;
        s_mining_assets[3].len  = runtime_js_gz_len;
        s_mining_assets[4].data  = vendor_js_gz;
        s_mining_assets[4].len  = vendor_js_gz_len;
        s_mining_assets[5].data  = index2_js_gz;
        s_mining_assets[5].len  = index2_js_gz_len;
        s_mining_assets[6].data  = index3_js_gz;
        s_mining_assets[6].len  = index3_js_gz_len;
        s_mining_assets[7].data  = pool_js_gz;
        s_mining_assets[7].len  = pool_js_gz_len;
        s_mining_assets[8].data  = update_js_gz;
        s_mining_assets[8].len  = update_js_gz_len;
        s_mining_assets[9].data  = diagnostics_js_gz;
        s_mining_assets[9].len  = diagnostics_js_gz_len;
        s_mining_assets[10].data = settings_js_gz;
        s_mining_assets[10].len = settings_js_gz_len;
        s_mining_assets[11].data = history_js_gz;
        s_mining_assets[11].len = history_js_gz_len;
        s_mining_assets[12].data = knot_js_gz;
        s_mining_assets[12].len = knot_js_gz_len;
        s_mining_assets[13].data = logo_svg_gz;
        s_mining_assets[13].len = logo_svg_gz_len;
        s_mining_assets[14].data = favicon_svg_gz;
        s_mining_assets[14].len = favicon_svg_gz_len;
        initialized = true;
    }
}

// Mining-mode dynamic route table. sizeof-derived count keeps
// webui_reserve_mining_routes in sync with the actual registrations.
static const bb_route_t * const s_mining_routes[] = {
    &s_stats_route,
    &s_pool_route,
    &s_pool_put_route,
    &s_pool_switch_route,
    &s_pool_delete_primary_route,
    &s_pool_delete_fallback_route,
    &s_diag_asic_route,
    &s_diag_benchmark_route,
    &s_knot_route,
    &s_settings_get_route,
    &s_settings_post_route,
#ifdef ASIC_CHIP
    &s_power_route,
    &s_fan_route,
    &s_fan_post_route,
#endif
    &s_settings_patch_route,
};

void webui_reserve_mining_routes(void)
{
    size_t n = sizeof(s_mining_routes) / sizeof(s_mining_routes[0]);
#if CONFIG_WEBUI_MINING_UI
    n += sizeof(s_mining_assets) / sizeof(s_mining_assets[0]);
#endif
    bb_http_reserve_routes((int)n);
}

bb_err_t webui_register_mining_routes(bb_http_handle_t server)
{
    bb_err_t rc;
#if CONFIG_WEBUI_MINING_UI
    // Static SPA assets (index.html/js/css/svg). Skipped on single-core (S2/C3)
    // boards: serving the bundle needs more contiguous heap than these no-PSRAM
    // parts have, and it can't load there anyway. The /api/* routes below stay
    // registered, so the dashboard data (and hashrate) remain available headless.
    init_mining_assets();
    rc = bb_http_register_assets(server, s_mining_assets,
                                 sizeof(s_mining_assets) / sizeof(s_mining_assets[0]));
    if (rc != BB_OK) return rc;
#endif

    // Register dynamic handlers with OpenAPI descriptors
    rc = bb_http_register_route_table(server, s_mining_routes,
                                      sizeof(s_mining_routes) / sizeof(s_mining_routes[0]));
    if (rc != BB_OK) return rc;

    bb_log_i(TAG, "mining routes registered");
    return BB_OK;
}
