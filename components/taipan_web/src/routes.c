#include "taipan_web.h"
#include "bb_http.h"
#include "bb_info.h"
#include "bb_json.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_timer.h"
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
#include "taipan_config.h"
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

extern const uint8_t prov_form_html_gz[];
extern const size_t prov_form_html_gz_len;
extern const uint8_t theme_css_gz[];
extern const size_t theme_css_gz_len;
extern const uint8_t index_html_gz[];
extern const size_t index_html_gz_len;
extern const uint8_t index_js_gz[];
extern const size_t index_js_gz_len;
extern const uint8_t index_css_gz[];
extern const size_t index_css_gz_len;
extern const uint8_t prov_save_html_gz[];
extern const size_t prov_save_html_gz_len;
extern const uint8_t logo_svg_gz[];
extern const size_t logo_svg_gz_len;
extern const uint8_t favicon_svg_gz[];
extern const size_t favicon_svg_gz_len;

static uint32_t s_wdt_resets = 0;

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
        bb_http_resp_send_err(req, 400, "All fields required");
        return BB_ERR_INVALID_ARG;
    }
    uint16_t port = (uint16_t)strtoul(port_str, NULL, 10);
    if (port == 0) {
        bb_http_resp_send_err(req, 400, "Valid port required");
        return BB_ERR_INVALID_ARG;
    }

    // Derive hostname from worker if not provided
    if (hostname[0] == '\0') {
        bb_mdns_build_hostname(worker, NULL, hostname, sizeof(hostname));
    }

    // Validate and save hostname
    if (taipan_config_set_hostname(hostname) != BB_OK) {
        bb_http_resp_send_err(req, 400, "Invalid hostname");
        return BB_ERR_INVALID_ARG;
    }

    if (taipan_config_set_pool(pool_host, port, wallet, worker, pool_pass) != BB_OK) {
        bb_http_resp_send_err(req, 500, "Failed to save config");
        return BB_ERR_INVALID_ARG;
    }

    bb_http_resp_set_header(req, "Connection", "close");
    bb_http_resp_set_header(req, "Access-Control-Allow-Origin", "*");
    bb_http_resp_set_header(req, "Access-Control-Allow-Private-Network", "true");
    bb_http_resp_set_header(req, "Content-Encoding", "gzip");
    bb_http_resp_set_header(req, "Content-Type", "text/html");
    bb_http_resp_send(req, (const char *)prov_save_html_gz, prov_save_html_gz_len);

    // schedule deferred restart so config changes apply
    schedule_deferred_restart();
    return BB_OK;
}

// ============================================================================
// PORTABLE HANDLERS (migrated to bb_http API)
// ============================================================================

static bb_err_t stats_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    double hw_rate = 0, hw_ema = 0;
    double best_diff = 0;
    uint32_t hw_shares = 0;
    float temp = 0;
    uint32_t session_shares = 0, session_rejected = 0;
    uint32_t session_rejected_job_not_found = 0;
    uint32_t session_rejected_low_difficulty = 0;
    uint32_t session_rejected_duplicate = 0;
    uint32_t session_rejected_stale_prevhash = 0;
    uint32_t session_rejected_other = 0;
    int32_t  session_rejected_other_last_code = -1;
    int64_t last_share_us = 0, session_start_us = 0;
    mining_lifetime_t lifetime = {0};
#ifdef ASIC_CHIP
    double asic_rate = 0, asic_ema = 0;
    uint32_t asic_shares = 0;
    float asic_temp = 0;
    float asic_freq_cfg = -1.0f, asic_freq_eff = -1.0f;
    float asic_total_ghs = 0.0f;
    float asic_hw_error_pct = 0.0f;
    float asic_total_ghs_1m = 0.0f, asic_total_ghs_10m = 0.0f, asic_total_ghs_1h = 0.0f;
    float asic_hw_error_pct_1m = 0.0f, asic_hw_error_pct_10m = 0.0f, asic_hw_error_pct_1h = 0.0f;
    bool  asic_total_valid = false;
#endif

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        hw_rate = mining_stats.hw_hashrate;
        hw_ema = mining_stats.hw_ema.value;
        hw_shares = mining_stats.hw_shares;
        temp = mining_stats.temp_c;
        session_shares = mining_stats.session.shares;
        session_rejected = mining_stats.session.rejected;
        session_rejected_job_not_found = mining_stats.session.rejected_job_not_found;
        session_rejected_low_difficulty = mining_stats.session.rejected_low_difficulty;
        session_rejected_duplicate = mining_stats.session.rejected_duplicate;
        session_rejected_stale_prevhash = mining_stats.session.rejected_stale_prevhash;
        session_rejected_other = mining_stats.session.rejected_other;
        session_rejected_other_last_code = mining_stats.session.rejected_other_last_code;
        last_share_us = mining_stats.session.last_share_us;
        session_start_us = mining_stats.session.start_us;
        best_diff = mining_stats.session.best_diff;
        lifetime = mining_stats.lifetime;
#ifdef ASIC_CHIP
        asic_rate = mining_stats.asic_hashrate;
        asic_ema = mining_stats.asic_ema.value;
        asic_shares = mining_stats.asic_shares;
        asic_temp = mining_stats.asic_temp_c;
        asic_freq_cfg = mining_stats.asic_freq_configured_mhz;
        asic_freq_eff = mining_stats.asic_freq_effective_mhz;
        asic_total_ghs = mining_stats.asic_total_ghs;
        asic_hw_error_pct = mining_stats.asic_hw_error_pct;
        asic_total_ghs_1m = mining_stats.asic_total_ghs_1m;
        asic_total_ghs_10m = mining_stats.asic_total_ghs_10m;
        asic_total_ghs_1h = mining_stats.asic_total_ghs_1h;
        asic_hw_error_pct_1m = mining_stats.asic_hw_error_pct_1m;
        asic_hw_error_pct_10m = mining_stats.asic_hw_error_pct_10m;
        asic_hw_error_pct_1h = mining_stats.asic_hw_error_pct_1h;
        asic_total_valid = (asic_total_ghs > 0.001f);
#endif
        xSemaphoreGive(mining_stats.mutex);
    }

    int64_t now_us = esp_timer_get_time();
    int64_t uptime_s = (session_start_us > 0) ? (now_us - session_start_us) / 1000000 : 0;
    int64_t last_share_ago_s = (last_share_us > 0) ? (now_us - last_share_us) / 1000000 : -1;

    bb_json_t root = bb_json_obj_new();
    bb_json_obj_set_number(root, "hashrate", hw_rate);
    bb_json_obj_set_number(root, "hashrate_avg", hw_ema);
    bb_json_obj_set_number(root, "temp_c", (double)temp);
    bb_json_obj_set_number(root, "shares", hw_shares);
    bb_json_obj_set_number(root, "session_shares", session_shares);
    bb_json_obj_set_number(root, "session_rejected", session_rejected);
    bb_json_t rejected = bb_json_obj_new();
    bb_json_obj_set_number(rejected, "total", (double)session_rejected);
    bb_json_obj_set_number(rejected, "job_not_found", (double)session_rejected_job_not_found);
    bb_json_obj_set_number(rejected, "low_difficulty", (double)session_rejected_low_difficulty);
    bb_json_obj_set_number(rejected, "duplicate", (double)session_rejected_duplicate);
    bb_json_obj_set_number(rejected, "stale_prevhash", (double)session_rejected_stale_prevhash);
    bb_json_obj_set_number(rejected, "other", (double)session_rejected_other);
    bb_json_obj_set_number(rejected, "other_last_code", (double)session_rejected_other_last_code);
    bb_json_obj_set_obj(root, "rejected", rejected);
    bb_json_obj_set_number(root, "last_share_ago_s", (double)last_share_ago_s);
    bb_json_obj_set_number(root, "lifetime_shares", lifetime.total_shares);
    bb_json_obj_set_number(root, "best_diff", best_diff);
    bb_json_obj_set_number(root, "uptime_s", (double)uptime_s);

    /* expected_ghs: theoretical max hashrate for the running platform, in GH/s.
     * Uniform across boards so the UI doesn't need per-board fallbacks (TA-211).
     * - ASIC: freq_cfg(MHz) * small_cores * chip_count / 1000
     * - tdongle: 0.000223 GH/s (=223 kH/s) — ESP32-S3 HW-SHA peripheral ceiling,
     *   MMIO-bound on the 80 MHz APB bus. */
#ifdef ASIC_CHIP
    if (asic_freq_cfg > 0) {
        double expected_ghs = (double)asic_freq_cfg
                              * (double)BOARD_SMALL_CORES
                              * (double)BOARD_ASIC_COUNT / 1000.0;
        bb_json_obj_set_number(root, "expected_ghs", expected_ghs);
    } else {
        bb_json_obj_set_null(root, "expected_ghs");
    }
#else
    bb_json_obj_set_number(root, "expected_ghs", 0.000223);
#endif
#ifdef ASIC_CHIP
    bb_json_obj_set_number(root, "asic_hashrate", asic_rate);
    bb_json_obj_set_number(root, "asic_hashrate_avg", asic_ema);
    bb_json_obj_set_number(root, "asic_shares", asic_shares);
    bb_json_obj_set_number(root, "asic_temp_c", (double)asic_temp);
    if (asic_freq_cfg >= 0) {
        bb_json_obj_set_number(root, "asic_freq_configured_mhz", (double)asic_freq_cfg);
    } else {
        bb_json_obj_set_null(root, "asic_freq_configured_mhz");
    }
    if (asic_freq_eff >= 0) {
        bb_json_obj_set_number(root, "asic_freq_effective_mhz", (double)asic_freq_eff);
    } else {
        bb_json_obj_set_null(root, "asic_freq_effective_mhz");
    }
    bb_json_obj_set_number(root, "asic_small_cores", BOARD_SMALL_CORES);
    bb_json_obj_set_number(root, "asic_count", BOARD_ASIC_COUNT);
    if (asic_total_valid) {
        bb_json_obj_set_number(root, "asic_total_ghs", (double)asic_total_ghs);
        bb_json_obj_set_number(root, "asic_hw_error_pct", (double)asic_hw_error_pct);
        bb_json_obj_set_number(root, "asic_total_ghs_1m", (double)asic_total_ghs_1m);
        bb_json_obj_set_number(root, "asic_total_ghs_10m", (double)asic_total_ghs_10m);
        bb_json_obj_set_number(root, "asic_total_ghs_1h", (double)asic_total_ghs_1h);
        bb_json_obj_set_number(root, "asic_hw_error_pct_1m", (double)asic_hw_error_pct_1m);
        bb_json_obj_set_number(root, "asic_hw_error_pct_10m", (double)asic_hw_error_pct_10m);
        bb_json_obj_set_number(root, "asic_hw_error_pct_1h", (double)asic_hw_error_pct_1h);
    } else {
        bb_json_obj_set_null(root, "asic_total_ghs");
        bb_json_obj_set_null(root, "asic_hw_error_pct");
        bb_json_obj_set_null(root, "asic_total_ghs_1m");
        bb_json_obj_set_null(root, "asic_total_ghs_10m");
        bb_json_obj_set_null(root, "asic_total_ghs_1h");
        bb_json_obj_set_null(root, "asic_hw_error_pct_1m");
        bb_json_obj_set_null(root, "asic_hw_error_pct_10m");
        bb_json_obj_set_null(root, "asic_hw_error_pct_1h");
    }

    // Per-chip telemetry (TA-192 phase 2)
    asic_chip_telemetry_t chip_tel[BOARD_ASIC_COUNT];
    int n_chips = asic_task_get_chip_telemetry(chip_tel, BOARD_ASIC_COUNT);
    uint64_t now_us_for_drops = (uint64_t)esp_timer_get_time();

    bb_json_t chips_arr = bb_json_arr_new();
    for (int c = 0; c < n_chips; c++) {
        bb_json_t chip_obj = bb_json_obj_new();
        bb_json_obj_set_number(chip_obj, "idx", c);
        bb_json_obj_set_number(chip_obj, "total_ghs", (double)chip_tel[c].total_ghs);
        bb_json_obj_set_number(chip_obj, "error_ghs", (double)chip_tel[c].error_ghs);
        bb_json_obj_set_number(chip_obj, "hw_err_pct", (double)chip_tel[c].hw_err_pct);
        bb_json_obj_set_number(chip_obj, "total_raw", (double)chip_tel[c].total_raw);
        bb_json_obj_set_number(chip_obj, "error_raw", (double)chip_tel[c].error_raw);
        bb_json_obj_set_number(chip_obj, "total_drops", chip_tel[c].total_drops);
        bb_json_obj_set_number(chip_obj, "error_drops", chip_tel[c].error_drops);

        // TA-237: time since most-recent drop, drives UI self-heal of corrupt badge.
        if (chip_tel[c].last_drop_us == 0 || now_us_for_drops < chip_tel[c].last_drop_us) {
            bb_json_obj_set_null(chip_obj, "last_drop_ago_s");
        } else {
            uint64_t ago_us = now_us_for_drops - chip_tel[c].last_drop_us;
            bb_json_obj_set_number(chip_obj, "last_drop_ago_s", (double)(ago_us / 1000000ULL));
        }

        bb_json_t domains_arr = bb_json_arr_new();
        for (int d = 0; d < 4; d++) {
            bb_json_arr_append_number(domains_arr, (double)chip_tel[c].domain_ghs[d]);
        }
        bb_json_obj_set_arr(chip_obj, "domain_ghs", domains_arr);

        bb_json_t domains_drops_arr = bb_json_arr_new();
        for (int d = 0; d < 4; d++) {
            bb_json_arr_append_number(domains_drops_arr, chip_tel[c].domain_drops[d]);
        }
        bb_json_obj_set_arr(chip_obj, "domain_drops", domains_drops_arr);

        bb_json_arr_append_obj(chips_arr, chip_obj);
    }
    bb_json_obj_set_arr(root, "asic_chips", chips_arr);
#endif

    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

// ----------------------------------------------------------------------------
// /api/pool — TA-281/TA-286
// Locked shape: pool config (always populated from taipan_config_*) +
// session-scoped negotiated values (extranonce1, extranonce2_size,
// version_mask) + most-recent stratum mining.notify exposed as a `notify`
// sub-object. Pre-stratum-connect, connected=false, session_start_ago_s/
// extranonce*/version_mask/notify are all null; current_difficulty defaults
// to stratum_state.difficulty (512.0).
// ----------------------------------------------------------------------------
static bb_err_t pool_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    bb_json_t root = bb_json_obj_new();

    // Pool config — always populated from NVS-backed accessors.
    bb_json_obj_set_string(root, "host",   taipan_config_pool_host());
    bb_json_obj_set_number(root, "port",   (double)taipan_config_pool_port());
    bb_json_obj_set_string(root, "worker", taipan_config_worker_name());
    bb_json_obj_set_string(root, "wallet", taipan_config_wallet_addr());

    bool connected = stratum_is_connected();
    bb_json_obj_set_bool(root, "connected", connected);

    // session_start_ago_s — null pre-connect; wrap-safe diff in ms then /1000.
    // 32-bit ms wraps at ~49.7 days; sessions never run that long without
    // reconnect (job-drought watchdog at 5 min).
    uint32_t start_ms = stratum_get_session_start_ms();
    if (start_ms == 0) {
        bb_json_obj_set_null(root, "session_start_ago_s");
    } else {
        uint32_t now_ms   = pdTICKS_TO_MS(xTaskGetTickCount());
        uint32_t delta_ms = now_ms - start_ms;  // unsigned wrap-safe
        bb_json_obj_set_number(root, "session_start_ago_s",
                               (double)(delta_ms / 1000U));
    }

    bb_json_obj_set_number(root, "current_difficulty", stratum_get_difficulty());

    // Negotiated session params — null until subscribe response received.
    stratum_session_snapshot_t sess;
    if (stratum_get_session_snapshot(&sess) && sess.extranonce1_len > 0) {
        char en1_hex[2 * MAX_EXTRANONCE1_SIZE + 1];
        bytes_to_hex(sess.extranonce1, sess.extranonce1_len, en1_hex);
        bb_json_obj_set_string(root, "extranonce1", en1_hex);
        bb_json_obj_set_number(root, "extranonce2_size", (double)sess.extranonce2_size);
        if (sess.version_mask != 0) {
            char vm_hex[9];
            snprintf(vm_hex, sizeof(vm_hex), "%08lx", (unsigned long)sess.version_mask);
            bb_json_obj_set_string(root, "version_mask", vm_hex);
        } else {
            bb_json_obj_set_null(root, "version_mask");
        }
    } else {
        bb_json_obj_set_null(root, "extranonce1");
        bb_json_obj_set_null(root, "extranonce2_size");
        bb_json_obj_set_null(root, "version_mask");
    }

    // notify sub-object — most recent mining.notify (TA-201).
    const stratum_job_t *job = NULL;
    if (stratum_get_job_snapshot(&job) && job) {
        bb_json_t nobj = bb_json_obj_new();
        bb_json_obj_set_string(nobj, "job_id", job->job_id);

        char prevhash_hex[65];
        bytes_to_hex(job->prevhash, 32, prevhash_hex);
        bb_json_obj_set_string(nobj, "prev_hash", prevhash_hex);

        char coinb1_hex[2 * MAX_COINB1_SIZE + 1];
        bytes_to_hex(job->coinb1, job->coinb1_len, coinb1_hex);
        bb_json_obj_set_string(nobj, "coinb1", coinb1_hex);

        char coinb2_hex[2 * MAX_COINB2_SIZE + 1];
        bytes_to_hex(job->coinb2, job->coinb2_len, coinb2_hex);
        bb_json_obj_set_string(nobj, "coinb2", coinb2_hex);

        bb_json_t mb = bb_json_arr_new();
        for (size_t i = 0; i < job->merkle_count; i++) {
            char br_hex[65];
            bytes_to_hex(job->merkle_branches[i], 32, br_hex);
            bb_json_arr_append_string(mb, br_hex);
        }
        bb_json_obj_set_arr(nobj, "merkle_branches", mb);

        char hex8[9];
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)job->version);
        bb_json_obj_set_string(nobj, "version", hex8);
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)job->nbits);
        bb_json_obj_set_string(nobj, "nbits", hex8);
        snprintf(hex8, sizeof(hex8), "%08lx", (unsigned long)job->ntime);
        bb_json_obj_set_string(nobj, "ntime", hex8);

        bb_json_obj_set_bool(nobj, "clean_jobs", job->clean_jobs);
        bb_json_obj_set_obj(root, "notify", nobj);
    } else {
        bb_json_obj_set_null(root, "notify");
    }

    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

#ifdef ASIC_CHIP
static bb_err_t power_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    int vcore_mv = -1, icore_ma = -1, pcore_mw = -1;
    int vin_mv = -1;
    float board_temp_c = -1.0f, vr_temp_c = -1.0f;
    double asic_hashrate = 0;

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        vcore_mv = mining_stats.vcore_mv;
        icore_ma = mining_stats.icore_ma;
        pcore_mw = mining_stats.pcore_mw;
        vin_mv = mining_stats.vin_mv;
        asic_hashrate = mining_stats.asic_hashrate;
        board_temp_c = mining_stats.board_temp_c;
        vr_temp_c = mining_stats.vr_temp_c;
        xSemaphoreGive(mining_stats.mutex);
    }

    bb_json_t root = bb_json_obj_new();
    if (vcore_mv >= 0) {
        bb_json_obj_set_number(root, "vcore_mv", vcore_mv);
    } else {
        bb_json_obj_set_null(root, "vcore_mv");
    }
    if (icore_ma >= 0) {
        bb_json_obj_set_number(root, "icore_ma", icore_ma);
    } else {
        bb_json_obj_set_null(root, "icore_ma");
    }
    if (pcore_mw >= 0) {
        bb_json_obj_set_number(root, "pcore_mw", pcore_mw);
    } else {
        bb_json_obj_set_null(root, "pcore_mw");
    }
    if (pcore_mw > 0 && asic_hashrate > 0) {
        bb_json_obj_set_number(root, "efficiency_jth", (pcore_mw / 1000.0) / (asic_hashrate / 1e12));
    } else {
        bb_json_obj_set_null(root, "efficiency_jth");
    }
    if (vin_mv >= 0) {
        bb_json_obj_set_number(root, "vin_mv", vin_mv);
    } else {
        bb_json_obj_set_null(root, "vin_mv");
    }
    if (vin_mv >= 0) {
        bool vin_low = (vin_mv < (BOARD_NOMINAL_VIN_MV + 500) * 87 / 100);
        bb_json_obj_set_bool(root, "vin_low", vin_low);
    } else {
        bb_json_obj_set_null(root, "vin_low");
    }
    if (board_temp_c >= 0.0f) {
        bb_json_obj_set_number(root, "board_temp_c", (double)board_temp_c);
    } else {
        bb_json_obj_set_null(root, "board_temp_c");
    }
    if (vr_temp_c >= 0.0f) {
        bb_json_obj_set_number(root, "vr_temp_c", (double)vr_temp_c);
    } else {
        bb_json_obj_set_null(root, "vr_temp_c");
    }

    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

static bb_err_t fan_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    int fan_rpm = -1;
    int fan_duty_pct = -1;

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        fan_rpm = mining_stats.fan_rpm;
        fan_duty_pct = mining_stats.fan_duty_pct;
        xSemaphoreGive(mining_stats.mutex);
    }

    bb_json_t root = bb_json_obj_new();
    if (fan_rpm >= 0) {
        bb_json_obj_set_number(root, "rpm", fan_rpm);
    } else {
        bb_json_obj_set_null(root, "rpm");
    }
    if (fan_duty_pct >= 0) {
        bb_json_obj_set_number(root, "duty_pct", fan_duty_pct);
    } else {
        bb_json_obj_set_null(root, "duty_pct");
    }

    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}
#endif // ASIC_BM1370 || ASIC_BM1368

static bb_err_t knot_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    /* Off-stack: 32 * sizeof(knot_peer_t) ≈ 9 KB blows the httpd task stack.
     * Heap-allocate the snapshot buffer; httpd serializes per-handler so a
     * static would also be safe, but heap keeps the lifetime explicit. */
    knot_peer_t *peers = malloc(sizeof(knot_peer_t) * 32);
    if (!peers) {
        bb_http_resp_send_err(req, 500, "out of memory");
        return BB_ERR_INVALID_ARG;
    }
    size_t peer_count = knot_snapshot(peers, 32);

    int64_t now_us = esp_timer_get_time();

    bb_json_t root = bb_json_arr_new();
    for (size_t i = 0; i < peer_count; i++) {
        bb_json_t peer_obj = bb_json_obj_new();
        bb_json_obj_set_string(peer_obj, "instance", peers[i].instance_name);
        bb_json_obj_set_string(peer_obj, "hostname", peers[i].hostname);
        bb_json_obj_set_string(peer_obj, "ip", peers[i].ip4);
        bb_json_obj_set_string(peer_obj, "worker", peers[i].worker);
        bb_json_obj_set_string(peer_obj, "board", peers[i].board);
        bb_json_obj_set_string(peer_obj, "version", peers[i].version);
        bb_json_obj_set_string(peer_obj, "state", peers[i].state);

        int64_t seen_ago_us = now_us - peers[i].last_seen_us;
        int64_t seen_ago_s = seen_ago_us / 1000000;
        bb_json_obj_set_number(peer_obj, "seen_ago_s", (double)seen_ago_s);

        bb_json_arr_append_obj(root, peer_obj);
    }

    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    free(peers);
    return rc;
}

static void taipan_info_extender(bb_json_t root)
{
    bb_json_obj_set_string(root, "worker_name", taipan_config_worker_name());
    bb_json_obj_set_string(root, "hostname", taipan_config_hostname());

    const char *ssid = bb_nv_config_wifi_ssid();
    if (ssid) bb_json_obj_set_string(root, "ssid", ssid);

    bb_json_obj_set_bool(root, "validated", !bb_ota_is_pending());
    bb_json_obj_set_number(root, "wdt_resets", s_wdt_resets);

    time_t now = time(NULL);
    if (now > 1700000000) {
        int64_t uptime_s = esp_timer_get_time() / 1000000LL;
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

bb_err_t taipan_web_register_info_extender(void)
{
    return bb_info_register_extender(taipan_info_extender);
}

typedef struct {
    const char *pool_host;
    uint16_t    pool_port;
    const char *wallet;
    const char *worker;
    const char *pool_pass;
    const char *hostname;
} taipan_config_snap_t;

static bb_err_t settings_get_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    taipan_config_snap_t snap = {
        .pool_host = taipan_config_pool_host(),
        .pool_port = taipan_config_pool_port(),
        .wallet    = taipan_config_wallet_addr(),
        .worker    = taipan_config_worker_name(),
        .pool_pass = taipan_config_pool_pass(),
        .hostname  = taipan_config_hostname(),
    };

    bb_json_t root = bb_json_obj_new();
    bb_json_obj_set_string(root, "pool_host", snap.pool_host);
    bb_json_obj_set_number(root, "pool_port", snap.pool_port);
    bb_json_obj_set_string(root, "wallet",    snap.wallet);
    bb_json_obj_set_string(root, "worker",    snap.worker);
    bb_json_obj_set_string(root, "pool_pass", snap.pool_pass);
    bb_json_obj_set_string(root, "hostname",  snap.hostname);
    bb_json_obj_set_bool(root, "display_en", bb_nv_config_display_enabled());
    bb_json_obj_set_bool(root, "ota_skip_check", bb_nv_config_ota_skip_check());

    char *json = bb_json_serialize(root);
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t rc = bb_http_resp_send(req, json, strlen(json));
    bb_json_free_str(json);
    bb_json_free(root);
    return rc;
}

// Shared helper for POST (full) and PATCH (partial) settings
// Returns BB_OK on successful response send; sets *out_reboot_required to true if restart needed
static bb_err_t apply_settings(bb_http_request_t *req, bool partial, bool *out_reboot_required)
{
    *out_reboot_required = false;

    set_common_headers(req);
    char body[512];

    int body_len = bb_http_req_body_len(req);
    if (body_len > sizeof(body) - 1) {
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "Body too large";
        return bb_http_resp_send(req, msg, strlen(msg));
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "Empty body";
        return bb_http_resp_send(req, msg, strlen(msg));
    }
    body[len] = '\0';

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) {
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "Invalid JSON";
        return bb_http_resp_send(req, msg, strlen(msg));
    }

    // Extract fields — use current values as defaults for PATCH
    const char *pool_host = taipan_config_pool_host();
    uint16_t pool_port = taipan_config_pool_port();
    const char *wallet = taipan_config_wallet_addr();
    const char *worker = taipan_config_worker_name();
    const char *pool_pass = taipan_config_pool_pass();
    bool reboot_required = false;

    bb_json_t j;

    j = bb_json_obj_get_item(root, "pool_host");
    if (j && bb_json_item_is_string(j)) { pool_host = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "pool_host required", 18); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "pool_port");
    if (j && bb_json_item_is_number(j)) { pool_port = (uint16_t)bb_json_item_get_double(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "pool_port required", 18); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "wallet");
    if (j && bb_json_item_is_string(j)) { wallet = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "wallet required", 15); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "worker");
    if (j && bb_json_item_is_string(j)) { worker = bb_json_item_get_string(j); }
    else if (!partial) { if (!j) { bb_json_free(root); bb_http_resp_set_status(req, 400); bb_http_resp_set_header(req, "Content-Type", "text/plain"); bb_http_resp_send(req, "worker required", 15); return BB_ERR_INVALID_ARG; } }

    j = bb_json_obj_get_item(root, "pool_pass");
    if (j && bb_json_item_is_string(j)) { pool_pass = bb_json_item_get_string(j); }
    // pool_pass is optional even for POST

    // Compare against current values to determine if reboot is needed
    if (strcmp(pool_host, taipan_config_pool_host()) != 0 ||
        pool_port != taipan_config_pool_port() ||
        strcmp(wallet, taipan_config_wallet_addr()) != 0 ||
        strcmp(worker, taipan_config_worker_name()) != 0 ||
        strcmp(pool_pass, taipan_config_pool_pass()) != 0) {
        reboot_required = true;
    }

    // Validate
    if (pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "pool_host, wallet, worker must not be empty";
        return bb_http_resp_send(req, msg, strlen(msg));
    }
    if (pool_port == 0) {
        bb_json_free(root);
        bb_http_resp_set_status(req, 400);
        bb_http_resp_set_header(req, "Content-Type", "text/plain");
        static const char *msg = "pool_port must be > 0";
        return bb_http_resp_send(req, msg, strlen(msg));
    }

    // Save mining config if any mining field was provided
    if (reboot_required) {
        bb_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != BB_OK) {
            bb_json_free(root);
            bb_http_resp_set_status(req, 500);
            bb_http_resp_set_header(req, "Content-Type", "text/plain");
            static const char *msg = "Failed to save config";
            return bb_http_resp_send(req, msg, strlen(msg));
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
                bb_http_resp_set_header(req, "Content-Type", "text/plain");
                static const char *msg = "Failed to save display setting";
                return bb_http_resp_send(req, msg, strlen(msg));
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
                bb_http_resp_set_header(req, "Content-Type", "text/plain");
                static const char *msg = "Failed to save ota_skip_check";
                return bb_http_resp_send(req, msg, strlen(msg));
            }
        }
    }

    bb_json_free(root);

    // Response
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"saved\",\"reboot_required\":%s}",
             reboot_required ? "true" : "false");
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t send_rc = bb_http_resp_send(req, resp, strlen(resp));

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
    if (body_len > sizeof(body) - 1) {
        bb_http_resp_send_err(req, 400, "Body too large");
        return BB_ERR_INVALID_ARG;
    }
    int len = bb_http_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        bb_http_resp_send_err(req, 400, "Empty body");
        return BB_ERR_INVALID_ARG;
    }
    body[len] = '\0';

    bb_json_t root = bb_json_parse(body, 0);
    if (!root) {
        bb_http_resp_send_err(req, 400, "Invalid JSON");
        return BB_ERR_INVALID_ARG;
    }

    // Extract fields — use current values as defaults for PATCH
    const char *pool_host = taipan_config_pool_host();
    uint16_t pool_port = taipan_config_pool_port();
    const char *wallet = taipan_config_wallet_addr();
    const char *worker = taipan_config_worker_name();
    const char *pool_pass = taipan_config_pool_pass();
    const char *hostname = taipan_config_hostname();
    bool reboot_required = false;

    bb_json_t j;

    j = bb_json_obj_get_item(root, "pool_host");
    if (j && bb_json_item_is_string(j)) { pool_host = bb_json_item_get_string(j); }

    j = bb_json_obj_get_item(root, "pool_port");
    if (j && bb_json_item_is_number(j)) { pool_port = (uint16_t)bb_json_item_get_double(j); }

    j = bb_json_obj_get_item(root, "wallet");
    if (j && bb_json_item_is_string(j)) { wallet = bb_json_item_get_string(j); }

    j = bb_json_obj_get_item(root, "worker");
    if (j && bb_json_item_is_string(j)) { worker = bb_json_item_get_string(j); }

    j = bb_json_obj_get_item(root, "pool_pass");
    if (j && bb_json_item_is_string(j)) { pool_pass = bb_json_item_get_string(j); }

    j = bb_json_obj_get_item(root, "hostname");
    if (j && bb_json_item_is_string(j)) { hostname = bb_json_item_get_string(j); }

    // Check if hostname was provided and validate it
    bool hostname_changed = false;
    if (bb_json_obj_get_item(root, "hostname")) {
        if (strcmp(hostname, taipan_config_hostname()) != 0) {
            hostname_changed = true;
            if (hostname[0] != '\0' && taipan_config_set_hostname(hostname) != BB_OK) {
                bb_json_free(root);
                bb_http_resp_send_err(req, 400, "invalid hostname");
                return BB_ERR_INVALID_ARG;
            }
        }
    }

    // Compare against current values to determine if reboot is needed
    if (strcmp(pool_host, taipan_config_pool_host()) != 0 ||
        pool_port != taipan_config_pool_port() ||
        strcmp(wallet, taipan_config_wallet_addr()) != 0 ||
        strcmp(worker, taipan_config_worker_name()) != 0 ||
        strcmp(pool_pass, taipan_config_pool_pass()) != 0 ||
        hostname_changed) {
        reboot_required = true;
    }

    // Validate
    if (pool_host[0] == '\0' || wallet[0] == '\0' || worker[0] == '\0') {
        bb_json_free(root);
        bb_http_resp_send_err(req, 400, "pool_host, wallet, worker must not be empty");
        return BB_ERR_INVALID_ARG;
    }
    if (pool_port == 0) {
        bb_json_free(root);
        bb_http_resp_send_err(req, 400, "pool_port must be > 0");
        return BB_ERR_INVALID_ARG;
    }

    // Save mining config if any mining field was provided
    if (reboot_required && !hostname_changed) {
        bb_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != BB_OK) {
            bb_json_free(root);
            bb_http_resp_send_err(req, 500, "Failed to save config");
            return BB_ERR_INVALID_ARG;
        }
    } else if (reboot_required && hostname_changed) {
        bb_err_t err = taipan_config_set_pool(pool_host, pool_port, wallet, worker, pool_pass);
        if (err != BB_OK) {
            bb_json_free(root);
            bb_http_resp_send_err(req, 500, "Failed to save config");
            return BB_ERR_INVALID_ARG;
        }
    }

    // Handle display_en separately (takes effect immediately, no reboot needed)
    {
        bool display_val = false;
        if (bb_json_obj_get_bool(root, "display_en", &display_val)) {
            bb_err_t err = bb_nv_config_set_display_enabled(display_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_send_err(req, 500, "Failed to save display setting");
                return BB_ERR_INVALID_ARG;
            }
        }
    }

    {
        bool skip_val = false;
        if (bb_json_obj_get_bool(root, "ota_skip_check", &skip_val)) {
            bb_err_t err = bb_nv_config_set_ota_skip_check(skip_val);
            if (err != BB_OK) {
                bb_json_free(root);
                bb_http_resp_send_err(req, 500, "Failed to save ota_skip_check");
                return BB_ERR_INVALID_ARG;
            }
        }
    }

    bb_json_free(root);

    // Response
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"status\":\"saved\",\"reboot_required\":%s}",
             reboot_required ? "true" : "false");
    bb_http_resp_set_header(req, "Content-Type", "application/json");
    bb_err_t send_rc = bb_http_resp_send(req, resp, strlen(resp));

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
      "\"lifetime_shares\":{\"type\":\"integer\"},"
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
      "\"required\":[\"host\",\"port\",\"worker\",\"wallet\",\"connected\","
      "\"session_start_ago_s\",\"current_difficulty\","
      "\"extranonce1\",\"extranonce2_size\",\"version_mask\",\"notify\"]}",
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
      "\"pool_host\":{\"type\":\"string\"},"
      "\"pool_port\":{\"type\":\"integer\"},"
      "\"wallet\":{\"type\":\"string\"},"
      "\"worker\":{\"type\":\"string\"},"
      "\"pool_pass\":{\"type\":\"string\"},"
      "\"hostname\":{\"type\":\"string\"},"
      "\"display_en\":{\"type\":\"boolean\"},"
      "\"ota_skip_check\":{\"type\":\"boolean\"}},"
      "\"required\":[\"pool_host\",\"pool_port\",\"wallet\",\"worker\","
      "\"pool_pass\",\"hostname\",\"display_en\",\"ota_skip_check\"]}",
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
        "\"ota_skip_check\":{\"type\":\"boolean\"}}}",
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
      "\"description\":\"curve-controlled duty %; null until first telemetry tick\"}}}",
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

#endif /* ASIC_CHIP */

// ============================================================================
// REGISTRATION
// ============================================================================

void taipan_web_install_prov_save_cb(void)
{
    bb_prov_set_save_callback(taipan_prov_save_cb);
}

static bb_http_asset_t s_prov_assets[] = {
    { "/",            "text/html",     "gzip", NULL, 0  },
    { "/theme.css",   "text/css",      "gzip", NULL, 0  },
    { "/logo.svg",    "image/svg+xml", "gzip", NULL, 0  },
    { "/favicon.ico", "image/svg+xml", "gzip", NULL, 0  },
};

const bb_http_asset_t *taipan_web_prov_assets(size_t *n)
{
    // Initialize asset data on first call
    static bool initialized = false;
    if (!initialized) {
        s_prov_assets[0].data = prov_form_html_gz;
        s_prov_assets[0].len = prov_form_html_gz_len;
        s_prov_assets[1].data = theme_css_gz;
        s_prov_assets[1].len = theme_css_gz_len;
        s_prov_assets[2].data = logo_svg_gz;
        s_prov_assets[2].len = logo_svg_gz_len;
        s_prov_assets[3].data = favicon_svg_gz;
        s_prov_assets[3].len = favicon_svg_gz_len;
        initialized = true;
    }
    *n = sizeof(s_prov_assets) / sizeof(s_prov_assets[0]);
    return s_prov_assets;
}

static bb_http_asset_t s_mining_assets[] = {
    { "/",                  "text/html",              "gzip", NULL, 0 },
    { "/assets/index.js",   "application/javascript", "gzip", NULL, 0 },
    { "/assets/index.css",  "text/css",               "gzip", NULL, 0 },
    { "/logo.svg",          "image/svg+xml",          "gzip", NULL, 0 },
    { "/favicon.svg",       "image/svg+xml",          "gzip", NULL, 0 },
};

static void init_mining_assets(void)
{
    static bool initialized = false;
    if (!initialized) {
        s_mining_assets[0].data = index_html_gz;
        s_mining_assets[0].len = index_html_gz_len;
        s_mining_assets[1].data = index_js_gz;
        s_mining_assets[1].len = index_js_gz_len;
        s_mining_assets[2].data = index_css_gz;
        s_mining_assets[2].len = index_css_gz_len;
        s_mining_assets[3].data = logo_svg_gz;
        s_mining_assets[3].len = logo_svg_gz_len;
        s_mining_assets[4].data = favicon_svg_gz;
        s_mining_assets[4].len = favicon_svg_gz_len;
        initialized = true;
    }
}

bb_err_t taipan_web_register_mining_routes(bb_http_handle_t server)
{
    // Cache WDT reset count — only changes on boot
    bb_nv_get_u32("taipanminer", "wdt_resets", &s_wdt_resets, 0);

    // Initialize and register static assets
    init_mining_assets();
    bb_http_register_assets(server, s_mining_assets, sizeof(s_mining_assets)/sizeof(s_mining_assets[0]));

    // Initialize breadboard's OTA validator (registers /api/ota/mark-valid handler)
    bb_ota_validator_init(server);

    // Register dynamic handlers with OpenAPI descriptors
    bb_err_t rc;
    rc = bb_http_register_described_route(server, &s_stats_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_pool_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_knot_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_settings_get_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_settings_post_route);
    if (rc != BB_OK) return rc;

#ifdef ASIC_CHIP
    rc = bb_http_register_described_route(server, &s_power_route);
    if (rc != BB_OK) return rc;

    rc = bb_http_register_described_route(server, &s_fan_route);
    if (rc != BB_OK) return rc;
#endif

    rc = bb_http_register_described_route(server, &s_settings_patch_route);
    if (rc != BB_OK) return rc;

    bb_ota_pull_register_handler(server);
    bb_ota_push_register_handler(server);

    bb_http_register_common_routes(server);
    bb_info_register_routes(server);
    bb_wifi_register_routes(server);
    bb_board_register_routes(server);
    bb_log_stream_register_routes(server);
    bb_log_register_routes(server);

    bb_log_i(TAG, "mining routes registered");
    return BB_OK;
}
