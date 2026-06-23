#include "webui.h"
#include "webui_ids.h"
#include "bb_http.h"
#include "bb_info.h"
#include "bb_health.h"
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
#include "led.h"
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
#if CONFIG_KNOT_ENABLED
#include "knot.h"
#endif
#include "routes_json.h"
#include "bb_fan_routes.h"
#include "bb_fan.h"
#include "bb_power.h"
#include "bb_sensors.h"
#include "mining_pool_stats.h"
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

extern const bb_http_asset_t *webui_miner_assets_get(size_t *n);
extern const size_t webui_miner_assets_count;
extern const bb_http_asset_t *webui_prov_site_get(size_t *n);

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
        /* asic_temp_c removed — now in /api/thermal (asic die source) */
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

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;
    emit_stats_json(&obj, &s);
    return bb_http_resp_json_obj_end(&obj);
}

// ----------------------------------------------------------------------------
// /api/stats/reset — POST
// Zero all persisted lifetime + per-pool + session mining stats.
// Intended for recovery from a corrupt best_diff or phantom block.
// No request body. Returns 204 on success.
// ----------------------------------------------------------------------------
static bb_err_t stats_reset_handler(bb_http_request_t *req)
{
    set_common_headers(req);
    mining_stats_session_reset();
    mining_pool_stats_reset();
    return bb_http_resp_no_content(req);
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
        /* TA-413: pool_stats is no longer embedded in mining_stats_t.
         * Use the mining_pool_stats_*() API; the mutex still guards access. */
        xSemaphoreTake(mining_stats.mutex, portMAX_DELAY);
        for (int i = 0; i < MINING_POOL_STATS_MAX; i++) {
            const mining_pool_stat_t *src = mining_pool_stats_slot(i);
            if (!src || src->last_seen_us == 0) continue;
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
        s->lifetime_blocks_total    = mining_pool_stats_lifetime_blocks();
        s->lifetime_last_block_ts   = mining_pool_stats_lifetime_last_block_ts();
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
    if (rc != BB_OK) { free(s); free(stats_arr); return rc; }

    emit_pool_json(&obj, s, stats_arr, stats_count);

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

// ============================================================================
// BB ROUTE EXTENDERS (P4b)
// /api/sensors (GET+PATCH) is owned by BB. TM injects mining-specific fields
// into the "miner" section via extenders registered at order 0 (before BB
// routes init at order 1). There are no standalone /api/power, /api/fan, or
// /api/thermal routes; all power/fan/thermal data is consolidated under
// /api/sensors.
// ============================================================================

#ifdef ASIC_CHIP

// ---------------------------------------------------------------------------
// Power extender: TM-domain fields + efficiency/vin_low
//
// TM-domain fields emitted:
//   vcore_mv  — VR output voltage (mirrors BB's vout_mv; sourced from bb_power_snapshot)
//   icore_ma  — VR output current (mirrors BB's iout_ma; sourced from bb_power_snapshot)
//   pcore_mw  — TOTAL board power WITH BOARD_POWER_OFFSET_MW (mining_stats.pcore_mw);
//               BB's pout_mw is VR-only (vout*iout/1000) with no board offset.
//   vr_temp_c — VR temperature (mirrors BB's temp_c; sourced from bb_power_snapshot)
//   board_temp_c — NOT emitted here; frontend reads it from /api/thermal "board".
//
// Efficiency/vin_low remain as-is.
// ---------------------------------------------------------------------------

static void taipan_power_extender(bb_json_t root, void *ctx)
{
    (void)ctx;
    int    pcore_mw             = -1;
    double asic_hashrate        = 0.0;
    float  pcore_1m             = -1.0f;
    float  pcore_10m            = -1.0f;
    float  pcore_1h             = -1.0f;
    float  ghs_1m               = -1.0f;
    float  ghs_10m              = -1.0f;
    float  ghs_1h               = -1.0f;
    float  asic_freq            = 0.0f;
    int    vin_mv               = -1;

    if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        pcore_mw      = mining_stats.pcore_mw;
        asic_hashrate = mining_stats.asic_hashrate;
        pcore_1m      = mining_stats.pcore_mw_1m;
        pcore_10m     = mining_stats.pcore_mw_10m;
        pcore_1h      = mining_stats.pcore_mw_1h;
        ghs_1m        = mining_stats.asic_total_ghs_1m;
        ghs_10m       = mining_stats.asic_total_ghs_10m;
        ghs_1h        = mining_stats.asic_total_ghs_1h;
        asic_freq     = mining_stats.asic_freq_configured_mhz;
        vin_mv        = mining_stats.vin_mv;
        xSemaphoreGive(mining_stats.mutex);
    }

    /* TM-domain power fields for the frontend Power card.
     * vcore_mv/icore_ma/vr_temp_c mirror BB's vout_mv/iout_ma/temp_c but
     * use the TM field names the UI expects.  We read the primary bb_power
     * snapshot directly so we don't duplicate the polling logic. */
    {
        bb_power_snapshot_t psnap;
        bb_power_snapshot(bb_power_primary(), &psnap);

        if (psnap.vout_mv >= 0)
            bb_json_obj_set_number(root, "vcore_mv", (double)psnap.vout_mv);
        else
            bb_json_obj_set_null(root, "vcore_mv");

        if (psnap.iout_ma >= 0)
            bb_json_obj_set_number(root, "icore_ma", (double)psnap.iout_ma);
        else
            bb_json_obj_set_null(root, "icore_ma");

        if (psnap.temp_c >= 0)
            bb_json_obj_set_number(root, "vr_temp_c", (double)psnap.temp_c);
        else
            bb_json_obj_set_null(root, "vr_temp_c");
    }

    /* pcore_mw — TOTAL board power WITH BOARD_POWER_OFFSET_MW.
     * BB's pout_mw is raw VR only; this value includes the board offset and
     * is the canonical power figure used by the efficiency calculation. */
    if (pcore_mw >= 0)
        bb_json_obj_set_number(root, "pcore_mw", (double)pcore_mw);
    else
        bb_json_obj_set_null(root, "pcore_mw");

    /* efficiency_jth: instantaneous (pcore_mw + live hashrate) */
    {
        /* asic_hashrate is H/s; divide by 1e9 to get GH/s */
        double eff = mining_efficiency_jth((double)pcore_mw, asic_hashrate / 1e9);
        if (eff >= 0.0) bb_json_obj_set_number(root, "efficiency_jth", eff);
        else            bb_json_obj_set_null(root, "efficiency_jth");
    }

    /* rolling efficiency windows */
    {
        double e;
        e = mining_efficiency_jth((double)pcore_1m, (double)ghs_1m);
        if (e >= 0.0) bb_json_obj_set_number(root, "efficiency_jth_1m", e);
        else          bb_json_obj_set_null(root, "efficiency_jth_1m");
        e = mining_efficiency_jth((double)pcore_10m, (double)ghs_10m);
        if (e >= 0.0) bb_json_obj_set_number(root, "efficiency_jth_10m", e);
        else          bb_json_obj_set_null(root, "efficiency_jth_10m");
        e = mining_efficiency_jth((double)pcore_1h, (double)ghs_1h);
        if (e >= 0.0) bb_json_obj_set_number(root, "efficiency_jth_1h", e);
        else          bb_json_obj_set_null(root, "efficiency_jth_1h");
    }

    /* expected_efficiency_jth: from configured freq + board TDP model */
    {
        double expected_ghs = 0.0;
        if (pcore_mw > 0 && asic_freq > 0 &&
            mining_get_expected_ghs(asic_freq, &expected_ghs) && expected_ghs > 0) {
            double eff = mining_efficiency_jth((double)pcore_mw, expected_ghs);
            if (eff >= 0.0) bb_json_obj_set_number(root, "expected_efficiency_jth", eff);
            else            bb_json_obj_set_null(root, "expected_efficiency_jth");
        } else {
            bb_json_obj_set_null(root, "expected_efficiency_jth");
        }
    }

    /* vin_low: true when vin_mv < 87% of (nominal + 500 mV) */
    if (vin_mv >= 0) {
        bool vin_low = (vin_mv < (BOARD_NOMINAL_VIN_MV + 500) * 87 / 100);
        bb_json_obj_set_bool(root, "vin_low", vin_low);
    } else {
        bb_json_obj_set_null(root, "vin_low");
    }

    /* TA-318: vcore_restart_count — NVS-persisted count of vcore-collapse
     * auto-restarts. Useful for diagnosing power supply issues after the fact.
     * Always 0 when CONFIG_TM_VCORE_WATCHDOG is not set. */
    bb_json_obj_set_number(root, "vcore_restart_count",
                           (double)asic_task_get_vcore_restart_count());
    /* TA-435: vcore_fault_held — true when the watchdog has latched FAULT_HOLD
     * due to an OC hardware fault. Cleared only by explicit UI action (TA-436). */
    bb_json_obj_set_bool(root, "vcore_fault_held", asic_task_get_vcore_fault_held());
}

// Schema property fragments for OpenAPI (comma-separated properties, no braces)
static const char k_power_extender_schema[] =
    "\"vcore_mv\":{\"type\":[\"number\",\"null\"],"
    "\"description\":\"VR output voltage in mV (TM alias for vout_mv)\"},"
    "\"icore_ma\":{\"type\":[\"number\",\"null\"],"
    "\"description\":\"VR output current in mA (TM alias for iout_ma)\"},"
    "\"pcore_mw\":{\"type\":[\"number\",\"null\"],"
    "\"description\":\"Total board power in mW including BOARD_POWER_OFFSET_MW\"},"
    "\"vr_temp_c\":{\"type\":[\"number\",\"null\"],"
    "\"description\":\"VR temperature in °C (TM alias for temp_c)\"},"
    "\"efficiency_jth\":{\"type\":[\"number\",\"null\"],"
    "\"description\":\"J/TH; null until ASIC hashrate and power both available\"},"
    "\"efficiency_jth_1m\":{\"type\":[\"number\",\"null\"]},"
    "\"efficiency_jth_10m\":{\"type\":[\"number\",\"null\"]},"
    "\"efficiency_jth_1h\":{\"type\":[\"number\",\"null\"]},"
    "\"expected_efficiency_jth\":{\"type\":[\"number\",\"null\"]},"
    "\"vin_low\":{\"type\":[\"boolean\",\"null\"],"
    "\"description\":\"true when VIN is below 87% of (nominal+500mV) threshold\"},"
    "\"vcore_restart_count\":{\"type\":\"number\","
    "\"description\":\"NVS-persisted count of vcore-collapse auto-restarts (TA-318); resets after 300s healthy\"}";

#endif /* ASIC_CHIP */

// ---------------------------------------------------------------------------
// Autofan persist callback — invoked by BB's PATCH /api/sensors handler after a
// successful fan config change. Writes the new cfg to TM NVS so it survives reboot.
// Registered via bb_fan_routes_set_autofan_persist_cb() in webui_register_power_fan_extenders.
// ---------------------------------------------------------------------------
#if defined(ASIC_CHIP) && defined(CONFIG_BB_FAN_AUTOFAN)
static void taipan_autofan_persist_cb(void *ctx, const bb_fan_autofan_cfg_t *cfg)
{
    (void)ctx;
    config_set_autofan_enabled(cfg->enabled);
    config_set_die_target_c((uint16_t)cfg->die_target_c);
    config_set_vr_target_c((uint16_t)cfg->aux_target_c);
    config_set_min_fan_pct((uint16_t)cfg->min_pct);
    config_set_manual_fan_pct((uint16_t)cfg->manual_pct);
}
#endif /* ASIC_CHIP && CONFIG_BB_FAN_AUTOFAN */

#if CONFIG_KNOT_ENABLED
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
#endif // CONFIG_KNOT_ENABLED

// B1-269: migrated from bb_info_register_extender (flat merge into root) to
// bb_info_register_section (writes into a "mining" child object).
// The "network" and "asic" sub-objects are kept as nested objects within the
// "mining" section rather than merging into BB's own sections.
static void mining_info_section_get(bb_json_t section, void *ctx)
{
    (void)ctx;
    bb_json_obj_set_string(section, "worker_name", config_worker_name());
    // wdt_resets removed: bb_diag now owns it in the diag section of /api/info (B1-269)
    // hostname/ssid/validated/boot_time removed: bb owns these at top-level /api/info

    // TA-339 / TA-320a: SHA microbench and overlap canary results.
    // Grouped under "sha" sub-object; absent on boards without HW SHA microbench.
    {
        bb_json_t sha = bb_json_obj_new();
        bool sha_has_fields = false;

        double sha_us, sha_khs;
        if (mining_get_sha_microbench(&sha_us, &sha_khs)) {
            bb_json_obj_set_number(sha, "us_per_op", sha_us);
            bb_json_obj_set_number(sha, "khs_ceiling", sha_khs);
            sha_has_fields = true;
        }

        sha_overlap_state_t ov = mining_get_sha_overlap_state();
        if (ov != SHA_OVERLAP_UNKNOWN) {
            bb_json_obj_set_string(sha, "overlap",
                                   ov == SHA_OVERLAP_SAFE ? "safe" : "unsafe");
            sha_has_fields = true;
        }
        sha_overlap_state_t hw = mining_get_sha_hwrite_state();
        if (hw != SHA_OVERLAP_UNKNOWN) {
            bb_json_obj_set_string(sha, "hwrite",
                                   hw == SHA_OVERLAP_SAFE ? "safe" : "unsafe");
            sha_has_fields = true;
        }

        if (sha_has_fields) {
            bb_json_obj_set_obj(section, "sha", sha);
        } else {
            bb_json_free(sha);
        }
    }

#ifdef ASIC_CHIP
    // ASIC descriptor: model, chip count, and small cores per chip.
    // Field names: "model" (chip designation), "chips" (count in chain),
    // "small_cores_per_chip" (per-chip parallelism). Frontend consumes these
    // directly without hardcoding the board→chip mapping.
    {
        bb_json_t asic = bb_json_obj_new();
#if defined(ASIC_BM1370)
        bb_json_obj_set_string(asic, "model", "BM1370");
#elif defined(ASIC_BM1368)
        bb_json_obj_set_string(asic, "model", "BM1368");
#else
        bb_json_obj_set_string(asic, "model", "unknown");
#endif
        bb_json_obj_set_number(asic, "chips", BOARD_ASIC_COUNT);
        bb_json_obj_set_number(asic, "small_cores_per_chip",
                               BOARD_SMALL_CORES / BOARD_ASIC_COUNT);
        bb_json_obj_set_obj(section, "asic", asic);
    }
#endif /* ASIC_CHIP */
}

// B1-269: migrated from bb_health_register_extender (flat merge into root) to
// bb_health_register_section (writes into a "mining" child object).
static void mining_health_section_get(bb_json_t section, void *ctx)
{
    (void)ctx;
    /* Live liveness signals for the System page. /api/info still owns the
     * one-shot fields (board, MAC, IP, reset_reason, etc.) — anything that
     * changes during the session belongs here. */
    bb_json_obj_set_bool(section, "sha_self_test_failed", mining_sha_self_test_failed());
    /* TA-435: OC hardware fault latch — true when vcore watchdog is in FAULT_HOLD. */
    bb_json_obj_set_bool(section, "vcore_fault_held", asic_task_get_vcore_fault_held());
}

static void pool_health_section_get(bb_json_t section, void *ctx)
{
    (void)ctx;
    bb_json_obj_set_bool(section, "stratum", stratum_is_connected());
    bb_json_obj_set_number(section, "fail_count",
                           stratum_get_connect_fail_count());
    bb_json_obj_set_number(section, "reconnect_ms",
                           stratum_get_reconnect_delay_ms());
}

#if CONFIG_KNOT_ENABLED
static void knot_health_section_get(bb_json_t section, void *ctx)
{
    (void)ctx;
    bb_json_obj_set_bool(section, "running", knot_is_running());
}
#endif /* CONFIG_KNOT_ENABLED */

bb_err_t webui_register_info_extender(void)
{
    // B1-269: extender API deleted; use bb_info_register_section / bb_health_register_section.
    bb_err_t err = bb_info_register_section(WEBUI_SECTION_MINING, mining_info_section_get, NULL, NULL);
    if (err != BB_OK) return err;
    err = bb_health_register_section(WEBUI_SECTION_MINING, mining_health_section_get, NULL, NULL);
    if (err != BB_OK) return err;
    err = bb_health_register_section(WEBUI_SECTION_POOL, pool_health_section_get, NULL, NULL);
    if (err != BB_OK) return err;
#if CONFIG_KNOT_ENABLED
    err = bb_health_register_section(WEBUI_SECTION_KNOT, knot_health_section_get, NULL, NULL);
    if (err != BB_OK) return err;
#endif

    // Capability flags: advertise which optional hardware/features this build has.
    // Gating mirrors the existing route-registration conditions so the frontend
    // can infer which API endpoints are present without probing 404s.
#ifdef ASIC_CHIP
    bb_info_register_capability(WEBUI_CAP_ASIC);
#endif
#if defined(ASIC_BM1370) || defined(ASIC_BM1368)
    bb_info_register_capability(WEBUI_CAP_FAN);
    bb_info_register_capability(WEBUI_CAP_POWER);
#endif
#if CONFIG_WEBUI_MINING_UI
    bb_info_register_capability(WEBUI_CAP_UI);
#endif
#if CONFIG_KNOT_ENABLED
    bb_info_register_capability(WEBUI_CAP_KNOT);
#endif

    return BB_OK;
}

// Register TM-specific "miner" section into /api/sensors (via bb_sensors_register_section).
// The deleted bb_http_register_route_extender("power",...) API is replaced by this sectioned
// registration (B1-269 migration). Section name "miner" avoids collision with bb_sensors
// built-in sections ("fan", "power", "thermal").
// BB's autofan (CONFIG_BB_FAN_AUTOFAN) owns fan telemetry and config via
// /api/sensors — no TM fan extender needed.
// Register the persist callback so PATCH /api/sensors fan changes survive reboot.
bb_err_t webui_register_power_fan_extenders(void)
{
#ifdef ASIC_CHIP
    bb_err_t err;
    err = bb_sensors_register_section("miner", taipan_power_extender, NULL, NULL, k_power_extender_schema);
    if (err != BB_OK) return err;
#ifdef CONFIG_BB_FAN_AUTOFAN
    bb_fan_routes_set_autofan_persist_cb(taipan_autofan_persist_cb, NULL);
#endif
#endif
    return BB_OK;
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
#if CONFIG_KNOT_ENABLED
    s.knot_en        = config_knot_enabled();
#endif
    s.led_heartbeat_en = config_led_heartbeat_enabled();
    s.provisioned    = bb_nv_config_is_provisioned();

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;
    emit_settings_json(&obj, &s);
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

        bool mdns_val = bb_nv_config_mdns_enabled();

        if (has_mdns) {
            bb_json_obj_get_bool(root, "mdns_en", &mdns_val);
        }

#if CONFIG_KNOT_ENABLED
        bool has_knot = bb_json_obj_get_item(root, "knot_en") != NULL;
        bool knot_val = config_knot_enabled();

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
#endif // CONFIG_KNOT_ENABLED

        // Handle mdns_en transition: when mdns goes off, knot must go off too
        if (has_mdns) {
            bool current_mdns = bb_nv_config_mdns_enabled();
            if (!mdns_val && current_mdns) {
                // Stopping mDNS: tear down knot first (it depends on
                // mdns browse) then stop mdns itself, then persist.
#if CONFIG_KNOT_ENABLED
                knot_deinit();
#endif
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

#if CONFIG_KNOT_ENABLED
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
#endif // CONFIG_KNOT_ENABLED

        #undef KNOT_REARM_LIVE
    }

    // Handle led_heartbeat_en (live apply, no reboot): persist + reflect on the
    // LED immediately. The settings page is only served while mining, so toggling
    // it on resumes the breathe and off darkens the LED. No-op on LED-less boards.
    if (bb_json_obj_get_item(root, "led_heartbeat_en") != NULL) {
        bool hb_val = config_led_heartbeat_enabled();
        bb_json_obj_get_bool(root, "led_heartbeat_en", &hb_val);
        bb_err_t err = config_set_led_heartbeat_enabled(hb_val);
        if (err != BB_OK) {
            bb_json_free(root);
            bb_http_resp_set_status(req, 500); { bb_http_json_obj_stream_t _e; bb_http_resp_json_obj_begin(req, &_e); bb_http_resp_json_obj_set_str(&_e, "error", "Failed to save led_heartbeat_en"); bb_http_resp_json_obj_end(&_e); }
            return err;
        }
        if (hb_val) led_set_mining(true); else led_off();
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
// /api/stats/reset — POST
// ---------------------------------------------------------------------------
static const bb_route_response_t s_stats_reset_responses[] = {
    { 204, NULL, NULL, "Stats reset" },
    { 0 },
};

static const bb_route_t s_stats_reset_route = {
    .method       = BB_HTTP_POST,
    .path         = "/api/stats/reset",
    .tag          = "mining",
    .summary      = "Reset all mining statistics",
    .operation_id = "postStatsReset",
    .responses    = s_stats_reset_responses,
    .handler      = stats_reset_handler,
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

#ifdef TAIPANMINER_DEBUG
// ---------------------------------------------------------------------------
// /api/diag/vcore-drop — POST (TA-318 test hook, DEBUG ONLY)
// Forces OPERATION=OFF on the TPS546 to collapse vcore to ~0V so the
// vcore watchdog end-to-end path can be validated on real hardware.
// NEVER compiled into production firmware — gated by TAIPANMINER_DEBUG.
// ---------------------------------------------------------------------------
#include "bb_power_tps546.h"

static bb_err_t diag_vcore_drop_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    bb_log_w(TAG, "DEBUG: forcing vcore drop (OPERATION=OFF)");

    bb_power_handle_t h = bb_power_primary();
    if (!h) {
        bb_http_resp_set_status(req, 503);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "no primary power handle");
        bb_http_resp_json_obj_end(&e);
        return BB_ERR_INVALID_STATE;
    }

    bb_err_t rc = bb_power_tps546_debug_force_off(h);
    if (rc != BB_OK) {
        bb_http_resp_set_status(req, 500);
        bb_http_json_obj_stream_t e; bb_http_resp_json_obj_begin(req, &e);
        bb_http_resp_json_obj_set_str(&e, "error", "OPERATION_OFF failed");
        bb_http_resp_json_obj_end(&e);
        return rc;
    }

    bb_http_json_obj_stream_t obj;
    rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;
    bb_http_resp_json_obj_set_str(&obj, "status", "vcore dropped");
    return bb_http_resp_json_obj_end(&obj);
}

static const bb_route_response_t s_diag_vcore_drop_responses[] = {
    { 200, "application/json",
      "{\"type\":\"object\","
      "\"properties\":{\"status\":{\"type\":\"string\"}},"
      "\"required\":[\"status\"]}",
      "vcore dropped (OPERATION=OFF written)" },
    { 503, "application/json", NULL, "no primary power handle" },
    { 500, "application/json", NULL, "PMBus write failed" },
    { 0 },
};

static const bb_route_t s_diag_vcore_drop_route = {
    .method       = BB_HTTP_POST,
    .path         = "/api/diag/vcore-drop",
    .tag          = "diag",
    .summary      = "DEBUG: force TPS546 OPERATION=OFF to collapse vcore (TA-318 test hook)",
    .operation_id = "postDiagVcoreDrop",
    .responses    = s_diag_vcore_drop_responses,
    .handler      = diag_vcore_drop_handler,
};
#endif /* TAIPANMINER_DEBUG */

#ifdef ASIC_CHIP
// ---------------------------------------------------------------------------
// /api/diag/asic — GET (TA-282, TA-287)
// Recent telemetry-drop log. Calls asic_task_get_drop_log() and serialises
// up to ASIC_DROP_LOG_CAP entries as recent_drops[].
// ASIC boards only — absent on CPU-SHA boards (S2/C3/wroom32/tdongle).
// ---------------------------------------------------------------------------
static bb_err_t diag_asic_handler(bb_http_request_t *req)
{
    set_common_headers(req);

    diag_asic_snapshot_t s = {0};

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

    bb_http_json_obj_stream_t obj;
    bb_err_t rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;

    emit_diag_asic_json(&obj, &s);

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
#endif /* ASIC_CHIP */

// ---------------------------------------------------------------------------
// /api/knot — GET
// ---------------------------------------------------------------------------

#if CONFIG_KNOT_ENABLED
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
#endif // CONFIG_KNOT_ENABLED

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
      "\"led_heartbeat_en\":{\"type\":\"boolean\","
      "\"description\":\"mining-heartbeat status LED (the dim breathe while hashing); default on\"},"
      "\"provisioned\":{\"type\":\"boolean\","
      "\"description\":\"read-only mirror of the bb_cfg NVS 'provisioned' u8 key; "
      "true after first successful provisioning, false on factory/post-reset. "
      "Settable only via direct NVS write (e.g. taipan-cli flash pre-seed), not HTTP.\"}},"
      "\"required\":[\"hostname\",\"display_en\",\"ota_skip_check\",\"mdns_en\","
      "\"knot_en\",\"led_heartbeat_en\",\"provisioned\"]}",
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
        "\"knot_en\":{\"type\":\"boolean\"},"
        "\"led_heartbeat_en\":{\"type\":\"boolean\"}}}",
    .responses            = s_settings_write_responses,
    .handler              = settings_patch_handler,
};

// Fan config changes (via PATCH /api/sensors) are persisted to NVS via the
// autofan persist callback registered below so settings survive reboot.

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

    diag_bench_snapshot_t snap = {
        .iters              = iters,
        .duration_us        = bench.total_us,
        .us_per_op          = bench.us_per_op,
        .khs                = khs,
        .sha_ops_per_sec    = sha_ops_per_sec,
        .backend            = backend,
        .text_overlap_state = text_overlap,
        .h_write_state      = hwrite,
        .settled            = bench.settled,
        .settled_after_iters = bench.settled_after_iters,
        .settled_iters      = bench.settled_iters,
        .settled_total_us   = bench.settled_total_us,
#ifdef ASIC_CHIP
        .asic_active        = true,
        .has_asic_active    = true,
#else
        .asic_active        = false,
        .has_asic_active    = false,
#endif
    };

    bb_http_json_obj_stream_t obj;
    rc = bb_http_resp_json_obj_begin(req, &obj);
    if (rc != BB_OK) return rc;

    emit_diag_bench_json(&obj, &snap);

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

// Thin wrapper — preserves the public webui_prov_assets(size_t *n) API used by
// main.c while delegating to the generated accessor from bb_embed_site.
// TABLE is named webui_prov_site (not webui_prov_assets) to avoid a symbol
// collision between the generated bb_http_asset_t[] array and this function.
const bb_http_asset_t *webui_prov_assets(size_t *n)
{
    return webui_prov_site_get(n);
}

// Mining-mode dynamic route table. sizeof-derived count keeps
// webui_reserve_mining_routes in sync with the actual registrations.
static const bb_route_t * const s_mining_routes[] = {
    &s_stats_route,
    &s_stats_reset_route,
    &s_pool_route,
    &s_pool_put_route,
    &s_pool_switch_route,
    &s_pool_delete_primary_route,
    &s_pool_delete_fallback_route,
    &s_diag_benchmark_route,
#ifdef ASIC_CHIP
    &s_diag_asic_route,
#endif
#ifdef TAIPANMINER_DEBUG
    &s_diag_vcore_drop_route,
#endif
#if CONFIG_KNOT_ENABLED
    &s_knot_route,
#endif
    &s_settings_get_route,
    &s_settings_post_route,
    &s_settings_patch_route,
};

void webui_reserve_mining_routes(void)
{
    size_t n = sizeof(s_mining_routes) / sizeof(s_mining_routes[0]);
#if CONFIG_WEBUI_MINING_UI
    n += 1;  // assets now use a single "/*" wildcard handler instead of N per-asset handlers
#endif
    bb_http_reserve_routes((int)n);
}

bb_err_t webui_register_mining_routes(bb_http_handle_t server)
{
    bb_err_t rc;

    // Register dynamic /api/* handlers first so they win first-match over the
    // "/*" asset wildcard registered below.
    rc = bb_http_register_route_table(server, s_mining_routes,
                                      sizeof(s_mining_routes) / sizeof(s_mining_routes[0]));
    if (rc != BB_OK) return rc;

#if CONFIG_WEBUI_MINING_UI
    // Static SPA assets (index.html/js/css/svg) — registered LAST via a single
    // "/*" wildcard handler so specific /api/* routes above win first-match.
    // Skipped on single-core (S2/C3) boards: serving the bundle needs more
    // contiguous heap than these no-PSRAM parts have, and it can't load there
    // anyway.  The /api/* routes above stay registered for headless access.
    {
        size_t _n;
        const bb_http_asset_t *_a = webui_miner_assets_get(&_n);
        rc = bb_http_register_assets(server, _a, _n);
    }
    if (rc != BB_OK) return rc;
#endif

    bb_log_i(TAG, "mining routes registered");
    return BB_OK;
}
