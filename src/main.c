#include <stdio.h>
#include <inttypes.h>
#include "bb_wifi.h"
#include "bb_prov.h"
#include "bb_mdns.h"
#include "bb_http.h"
#include "bb_ntp.h"
#include "bb_system.h"
#include "mining.h"
#include "mining_pool_stats.h"
#include "mining_pause_io.h"
#include "work.h"
#include "stratum.h"
#include "bb_nv.h"
#include "config.h"
#include "webui.h"
#include "routes_json.h"
#include "ui.h"
#include "bb_display.h"
#include "bb_hw.h"  // pulls in board header → defines BOARD_HAS_DISPLAY + panel/bus flags
#ifdef BOARD_DISPLAY_PANEL_SSD1306
#include "bb_display_ssd1306.h"
#endif
#ifdef BOARD_DISPLAY_PANEL_ILI9341
#include "bb_display_ili9341.h"
#endif
#include "led.h"
#include "esp_ota_ops.h"
#include "bb_timer.h"
#include "bb_log.h"
#include "bb_ota_pull.h"
#include "bb_ota_push.h"
#include "bb_ota_boot.h"
#include "bb_ota_led.h"
#include "bb_ota_hooks.h"
#include "bb_update_check.h"
#include "bb_pub.h"
#include "bb_cache.h"
#include "bb_clock.h"
#include "bb_mqtt.h"
#include "bb_manifest.h"
#include "bb_registry.h"
#include "bb_event.h"
#include "bb_event_routes.h"
#include "bb_sink_event.h"
#if CONFIG_KNOT_ENABLED
#include "knot.h"
#endif
#include "ota_validator_io.h"
#include "bb_ota_validator.h"
#include "boot_fallback_decision.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#endif
#include "asic_chip.h"
#ifdef ASIC_CHIP
#include "asic.h"
#include "bb_power_tps546.h"
#include "tps546_decode.h"
#include <limits.h>
#endif
#include "bb_display_info.h"
#include "bb_led_info.h"
#include "bb_net_health.h"
#include "bb_power.h"

#if CONFIG_WEBUI_MINING_UI
#define MINER_UI_TXT "1"
#else
#define MINER_UI_TXT "0"
#endif

static const char *TAG = "taipanminer";

// Mining task handles (for suspend/resume during OTA verification)
#ifdef ASIC_CHIP
TaskHandle_t asic_task_handle = NULL;
#else
TaskHandle_t mining_hw_task_handle = NULL;
#endif

static bb_periodic_timer_t s_stats_timer = NULL;
static TaskHandle_t s_stats_save_task = NULL;

// B1-352: health.alerts topic handle — file-scope so asic_task can post via getter.
static bb_event_topic_t s_health_alerts_topic = NULL;

bb_event_topic_t tm_health_alerts_topic(void) { return s_health_alerts_topic; }

// Set while an OTA transfer (push/pull) holds the device: the display status
// task stops blitting (frees the SPI bus) and the panel is blanked.
static volatile bool s_display_quiesced = false;

// Gate: bb_sink_event_seed_all() runs before start_mining(), so sample_fns must
// emit sentinel values until all mining/pool/stratum state is initialized.
static volatile bool s_telemetry_ready = false;

// ---------------------------------------------------------------------------
// Telemetry sampler — publishes mining stats via bb_pub on the "mining" topic
// ---------------------------------------------------------------------------

static bool tm_mining_sample(bb_json_t obj, void *ctx)
{
    (void)ctx;

    // Gate: seed runs before start_mining(); emit sentinels until telemetry is ready.
    if (!s_telemetry_ready) {
        bb_json_obj_set_number(obj, "hashrate_hs", 0.0);
        bb_json_obj_set_number(obj, "shares",      0.0);
        bb_json_obj_set_number(obj, "rejected",    0.0);
        return true;
    }

    // Snapshot under mutex; do NOT hold it across bb_json calls.
    double   hashrate = 0.0;
    uint32_t shares   = 0;
    uint32_t rejected = 0;

    if (mining_stats.mutex != NULL && xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
#ifdef ASIC_CHIP
        hashrate = mining_stats.asic_ema.value;
#else
        hashrate = mining_stats.hw_ema.value;
#endif
        shares   = mining_stats.session.shares;
        rejected = mining_stats.session.rejected;
        xSemaphoreGive(mining_stats.mutex);
    }

    bb_json_obj_set_number(obj, "hashrate_hs", hashrate);
    bb_json_obj_set_number(obj, "shares",      (double)shares);
    bb_json_obj_set_number(obj, "rejected",    (double)rejected);
    return true;
}

// ---------------------------------------------------------------------------
// B1-352 / SSOT: mining_rates gather+serialize (bb_pub_register_telemetry)
// ---------------------------------------------------------------------------
static bool tm_mining_rates_gather(void *snap_buf, void *ctx)
{
    (void)ctx;
    mining_rates_snapshot_t *snap = snap_buf;
    snap->hashrate_hs       = -1.0;
    snap->shares            = -1.0;
    snap->rejected          = -1.0;
    snap->pool_effective_hs = -1.0;
#ifdef ASIC_CHIP
    snap->asic_hashrate_hs  = -1.0;
    snap->asic_total_ghs    = -1.0;
#endif

    if (s_telemetry_ready && mining_stats.mutex != NULL &&
        xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
#ifdef ASIC_CHIP
        snap->hashrate_hs      = mining_stats.asic_ema.value;
        snap->asic_hashrate_hs = mining_stats.asic_hashrate;
        snap->asic_total_ghs   = (double)mining_stats.asic_total_ghs;
#else
        snap->hashrate_hs = mining_stats.hw_ema.value;
#endif
        snap->shares   = (double)mining_stats.session.shares;
        snap->rejected = (double)mining_stats.session.rejected;
        xSemaphoreGive(mining_stats.mutex);
    }

    if (s_telemetry_ready) {
        double pool_eff = mining_get_pool_effective_hashrate();
        snap->pool_effective_hs = (pool_eff > 0.0) ? pool_eff : -1.0;
    }

    snap->ts_ms = (int64_t)bb_clock_now_ms64();
    return true;
}

static void tm_mining_rates_serialize(bb_json_t obj, const void *snap_raw)
{
    emit_mining_rates_json(obj, (const mining_rates_snapshot_t *)snap_raw);
}

// ---------------------------------------------------------------------------
// B1-352 / SSOT: pool_pub gather+serialize (bb_pub_register_telemetry)
// ---------------------------------------------------------------------------
static bool tm_pool_pub_gather(void *snap_buf, void *ctx)
{
    (void)ctx;
    pool_pub_snapshot_t *snap = snap_buf;
    snap->connected             = false;
    snap->current_difficulty    = -1.0;
    snap->latency_ms            = -1.0;
    snap->active_pool_idx       = -1;
    snap->pool_effective_hs     = -1.0;
    snap->pool_effective_hs_1m  = -1.0;
    snap->pool_effective_hs_10m = -1.0;
    snap->pool_effective_hs_1h  = -1.0;

    if (s_telemetry_ready) {
        snap->connected          = stratum_is_connected();
        snap->current_difficulty = stratum_get_difficulty();
        {
            int rtt = stratum_get_pool_rtt_ms();
            snap->latency_ms = (rtt >= 0) ? (double)rtt : -1.0;
        }
        snap->active_pool_idx = stratum_get_active_pool_idx();

        double pool_eff = mining_get_pool_effective_hashrate();
        snap->pool_effective_hs = (pool_eff > 0.0) ? pool_eff : -1.0;

        {
            double v;
            v = mining_get_pool_effective_1m();
            snap->pool_effective_hs_1m  = (v > 0.0) ? v : -1.0;
            v = mining_get_pool_effective_10m();
            snap->pool_effective_hs_10m = (v > 0.0) ? v : -1.0;
            v = mining_get_pool_effective_1h();
            snap->pool_effective_hs_1h  = (v > 0.0) ? v : -1.0;
        }
    }

    snap->ts_ms = (int64_t)bb_clock_now_ms64();
    return true;
}

static void tm_pool_pub_serialize(bb_json_t obj, const void *snap_raw)
{
    emit_pool_pub_json(obj, (const pool_pub_snapshot_t *)snap_raw);
}

#ifdef ASIC_CHIP
// ---------------------------------------------------------------------------
// B1-352 / SSOT: sensors_miner gather+serialize (bb_pub_register_telemetry)
// Gathers all fields including the 3 divergent ones for /api/sensors miner.
// ---------------------------------------------------------------------------
static bool tm_sensors_miner_gather(void *snap_buf, void *ctx)
{
    (void)ctx;
    sensors_miner_snapshot_t *snap = snap_buf;
    snap->vcore_mv              = -1.0;
    snap->icore_ma              = -1.0;
    snap->pcore_mw              = -1.0;
    snap->vr_temp_c             = -1.0;
    snap->efficiency_jth        = -1.0;
    snap->efficiency_jth_1m     = -1.0;
    snap->efficiency_jth_10m    = -1.0;
    snap->efficiency_jth_1h     = -1.0;
    snap->vin_mv                = -1.0;
    snap->vin_low               = false;
    snap->vin_low_valid         = false;
    snap->sag_count             = 0;
    snap->vin_min_mv            = INT_MAX;
    snap->vin_uv_latched        = false;
    snap->last_sag_ms           = 0;
    snap->vcore_last_restart_ms = 0;
    snap->expected_efficiency_jth = -1.0;
    snap->vcore_restart_count   = 0;
    snap->vcore_fault_held      = false;

    if (!s_telemetry_ready) {
        snap->ts_ms = (int64_t)bb_clock_now_ms64();
        return true;
    }

    int    pcore_mw  = -1;
    double asic_hs   = 0.0;
    float  pcore_1m  = -1.0f;
    float  pcore_10m = -1.0f;
    float  pcore_1h  = -1.0f;
    float  ghs_1m    = -1.0f;
    float  ghs_10m   = -1.0f;
    float  ghs_1h    = -1.0f;
    float  asic_freq = 0.0f;
    int    vin_mv    = -1;

    if (mining_stats.mutex != NULL && xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        pcore_mw    = mining_stats.pcore_mw;
        asic_hs     = mining_stats.asic_hashrate;
        pcore_1m    = mining_stats.pcore_mw_1m;
        pcore_10m   = mining_stats.pcore_mw_10m;
        pcore_1h    = mining_stats.pcore_mw_1h;
        ghs_1m      = mining_stats.asic_total_ghs_1m;
        ghs_10m     = mining_stats.asic_total_ghs_10m;
        ghs_1h      = mining_stats.asic_total_ghs_1h;
        asic_freq   = mining_stats.asic_freq_configured_mhz;
        vin_mv      = mining_stats.vin_mv;
        xSemaphoreGive(mining_stats.mutex);
    }

    /* Read bb_power snapshot for vcore/icore/vr_temp */
    {
        bb_power_snapshot_t psnap;
        bb_power_snapshot(bb_power_primary(), &psnap);
        snap->vcore_mv  = (psnap.vout_mv >= 0) ? (double)psnap.vout_mv : -1.0;
        snap->icore_ma  = (psnap.iout_ma >= 0) ? (double)psnap.iout_ma : -1.0;
        snap->vr_temp_c = (psnap.temp_c  >= 0) ? (double)psnap.temp_c  : -1.0;
    }

    if (pcore_mw >= 0) snap->pcore_mw = (double)pcore_mw;

    /* efficiency_jth: instantaneous */
    if (pcore_mw >= 0 && asic_hs > 0.0) {
        double eff = mining_efficiency_jth((double)pcore_mw, asic_hs / 1e9);
        snap->efficiency_jth = (eff >= 0.0) ? eff : -1.0;
    }

    /* rolling efficiency windows */
    {
        double e;
        e = mining_efficiency_jth((double)pcore_1m, (double)ghs_1m);
        snap->efficiency_jth_1m  = (e >= 0.0) ? e : -1.0;
        e = mining_efficiency_jth((double)pcore_10m, (double)ghs_10m);
        snap->efficiency_jth_10m = (e >= 0.0) ? e : -1.0;
        e = mining_efficiency_jth((double)pcore_1h, (double)ghs_1h);
        snap->efficiency_jth_1h  = (e >= 0.0) ? e : -1.0;
    }

    /* vin_low */
    if (vin_mv >= 0) {
        snap->vin_mv        = (double)vin_mv;
        snap->vin_low       = (vin_mv < (BOARD_NOMINAL_VIN_MV + 500) * 87 / 100);
        snap->vin_low_valid = true;
    }

    /* VIN-sag fields from TPS546 status latch */
    {
        bb_power_tps546_status_t st;
        if (bb_power_tps546_read_status(bb_power_primary(), &st) == BB_OK) {
            snap->sag_count             = st.sag_count;
            snap->vin_min_mv            = st.vin_min_mv;
            snap->vin_uv_latched        = (st.fault_bits & TPS546_FAULT_VIN_UV) != 0;
            snap->last_sag_ms           = st.last_sag_ms;
        }
        snap->vcore_last_restart_ms = asic_task_get_vcore_last_restart_ms();
    }

    /* divergent fields for /api/sensors miner section */
    {
        double expected_ghs = 0.0;
        double eff_expected = -1.0;
        if (snap->pcore_mw > 0 && asic_freq > 0 &&
            mining_get_expected_ghs(asic_freq, &expected_ghs) && expected_ghs > 0) {
            double e = mining_efficiency_jth(snap->pcore_mw, expected_ghs);
            eff_expected = (e >= 0.0) ? e : -1.0;
        }
        snap->expected_efficiency_jth = eff_expected;
    }
    snap->vcore_restart_count = asic_task_get_vcore_restart_count();
    snap->vcore_fault_held    = asic_task_get_vcore_fault_held();

    snap->ts_ms = (int64_t)bb_clock_now_ms64();
    return true;
}

static void tm_sensors_miner_serialize(bb_json_t obj, const void *snap_raw)
{
    emit_sensors_miner_json(obj, (const sensors_miner_snapshot_t *)snap_raw);
}
#endif /* ASIC_CHIP */

// ---------------------------------------------------------------------------
// SSOT: stats gather+serialize — cache topic for /api/stats (no re-gather in handler)
// ---------------------------------------------------------------------------
static bool tm_stats_gather(void *snap_buf, void *ctx)
{
    (void)ctx;
    stats_snapshot_t *s = snap_buf;

    s->session_rejected_other_last_code = -1;
#ifndef ASIC_CHIP
    s->hashrate_1m  = -1.0;
    s->hashrate_10m = -1.0;
    s->hashrate_1h  = -1.0;
#endif
#ifdef ASIC_CHIP
    s->asic_freq_cfg = -1.0f;
    s->asic_freq_eff = -1.0f;
#endif

    if (mining_stats.mutex != NULL && xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s->hw_rate    = mining_stats.hw_hashrate;
        s->hw_ema     = mining_stats.hw_ema.value;
        s->hw_shares  = mining_stats.hw_shares;
        s->session_shares                   = mining_stats.session.shares;
        s->session_rejected                 = mining_stats.session.rejected;
        s->session_rejected_job_not_found   = mining_stats.session.rejected_job_not_found;
        s->session_rejected_low_difficulty  = mining_stats.session.rejected_low_difficulty;
        s->session_rejected_duplicate       = mining_stats.session.rejected_duplicate;
        s->session_rejected_stale_prevhash  = mining_stats.session.rejected_stale_prevhash;
        s->session_rejected_other           = mining_stats.session.rejected_other;
        s->session_rejected_other_last_code = mining_stats.session.rejected_other_last_code;
        s->session_blocks_found             = mining_stats.session.blocks_found;
        s->session_best_diff_ts             = mining_stats.session.best_diff_ts;
        s->session_last_block_ts            = mining_stats.session.last_block_ts;
        s->last_share_us    = mining_stats.session.last_share_us;
        s->session_start_us = mining_stats.session.start_us;
        s->best_diff        = mining_stats.session.best_diff;
#ifndef ASIC_CHIP
        s->hashrate_1m       = (double)mining_stats.hashrate_1m;
        s->hashrate_10m      = (double)mining_stats.hashrate_10m;
        s->hashrate_1h       = (double)mining_stats.hashrate_1h;
#endif
#ifdef ASIC_CHIP
        s->asic_rate         = mining_stats.asic_hashrate;
        s->asic_ema          = mining_stats.asic_ema.value;
        s->asic_shares       = mining_stats.asic_shares;
        s->asic_freq_cfg     = mining_stats.asic_freq_configured_mhz;
        s->asic_freq_eff     = mining_stats.asic_freq_effective_mhz;
        s->asic_total_ghs    = mining_stats.asic_total_ghs;
        s->asic_hw_error_pct = mining_stats.asic_hw_error_pct;
        s->asic_total_ghs_1m       = mining_stats.asic_total_ghs_1m;
        s->asic_total_ghs_10m      = mining_stats.asic_total_ghs_10m;
        s->asic_total_ghs_1h       = mining_stats.asic_total_ghs_1h;
        s->asic_hw_error_pct_1m    = mining_stats.asic_hw_error_pct_1m;
        s->asic_hw_error_pct_10m   = mining_stats.asic_hw_error_pct_10m;
        s->asic_hw_error_pct_1h    = mining_stats.asic_hw_error_pct_1h;
        s->asic_total_valid  = (s->asic_total_ghs > 0.001f);
        s->asic_small_cores  = BOARD_SMALL_CORES;
        s->asic_count        = BOARD_ASIC_COUNT;
#endif
        xSemaphoreGive(mining_stats.mutex);
    }

    s->now_us = (int64_t)bb_timer_now_us();

#ifdef ASIC_CHIP
    {
        asic_chip_telemetry_t chip_tel[BOARD_ASIC_COUNT];
        int n_chips = asic_task_get_chip_telemetry(chip_tel, BOARD_ASIC_COUNT);
        s->n_chips = (n_chips <= ROUTES_JSON_MAX_CHIPS) ? n_chips : ROUTES_JSON_MAX_CHIPS;
        for (int c = 0; c < s->n_chips; c++) {
            s->chips[c].total_ghs   = chip_tel[c].total_ghs;
            s->chips[c].error_ghs   = chip_tel[c].error_ghs;
            s->chips[c].hw_err_pct  = chip_tel[c].hw_err_pct;
            s->chips[c].total_raw   = chip_tel[c].total_raw;
            s->chips[c].error_raw   = chip_tel[c].error_raw;
            s->chips[c].total_drops = chip_tel[c].total_drops;
            s->chips[c].error_drops = chip_tel[c].error_drops;
            s->chips[c].last_drop_us = chip_tel[c].last_drop_us;
            for (int d = 0; d < 4; d++) {
                s->chips[c].domain_ghs[d]   = chip_tel[c].domain_ghs[d];
                s->chips[c].domain_drops[d] = chip_tel[c].domain_drops[d];
            }
        }
        /* re-read after chip telemetry fetch for accurate last_drop_ago_s */
        s->now_us = (int64_t)bb_timer_now_us();
    }
#endif

    s->ts_ms = (int64_t)bb_clock_now_ms64();
    return true;
}

/* tm_stats_serialize uses the streaming JSON API via a host-capture shim.
 * emit_stats_json takes bb_http_json_obj_stream_t* (streaming), not bb_json_t (tree).
 * bb_cache_serialize_fn takes bb_json_t, so we build the output using the tree API
 * directly — same fields as emit_stats_json but via bb_json_obj_set_* calls.
 * This is the serialize-once path; REST reads memoized bytes via bb_cache_get_serialized. */
static void tm_stats_serialize(bb_json_t obj, const void *snap_raw)
{
    const stats_snapshot_t *snap = snap_raw;
    int64_t uptime_s = (snap->session_start_us > 0)
                       ? (snap->now_us - snap->session_start_us) / 1000000
                       : 0;
    int64_t last_share_ago_s = (snap->last_share_us > 0)
                               ? (snap->now_us - snap->last_share_us) / 1000000
                               : -1;

    bb_json_obj_set_number(obj, "hashrate",        snap->hw_rate);
    bb_json_obj_set_number(obj, "hashrate_avg",    snap->hw_ema);
    bb_json_obj_set_int   (obj, "shares",          (int64_t)snap->hw_shares);
    bb_json_obj_set_int   (obj, "session_shares",  (int64_t)snap->session_shares);
    bb_json_obj_set_int   (obj, "session_blocks_found",  (int64_t)snap->session_blocks_found);
    bb_json_obj_set_int   (obj, "session_best_diff_ts",  snap->session_best_diff_ts);
    bb_json_obj_set_int   (obj, "session_last_block_ts", snap->session_last_block_ts);

    {
        bb_json_t rej = bb_json_obj_new();
        bb_json_obj_set_int(rej, "total",           (int64_t)snap->session_rejected);
        bb_json_obj_set_int(rej, "job_not_found",   (int64_t)snap->session_rejected_job_not_found);
        bb_json_obj_set_int(rej, "low_difficulty",  (int64_t)snap->session_rejected_low_difficulty);
        bb_json_obj_set_int(rej, "duplicate",       (int64_t)snap->session_rejected_duplicate);
        bb_json_obj_set_int(rej, "stale_prevhash",  (int64_t)snap->session_rejected_stale_prevhash);
        bb_json_obj_set_int(rej, "other",           (int64_t)snap->session_rejected_other);
        bb_json_obj_set_int(rej, "other_last_code", (int64_t)snap->session_rejected_other_last_code);
        bb_json_obj_set_obj(obj, "rejected", rej);
    }

    bb_json_obj_set_int   (obj, "last_share_ago_s", last_share_ago_s);
    bb_json_obj_set_number(obj, "best_diff",         snap->best_diff);
    bb_json_obj_set_int   (obj, "uptime_s",          uptime_s);

#ifndef ASIC_CHIP
    if (snap->hashrate_1m >= 0.0)  bb_json_obj_set_number(obj, "hashrate_1m",  snap->hashrate_1m);
    else                           bb_json_obj_set_null  (obj, "hashrate_1m");
    if (snap->hashrate_10m >= 0.0) bb_json_obj_set_number(obj, "hashrate_10m", snap->hashrate_10m);
    else                           bb_json_obj_set_null  (obj, "hashrate_10m");
    if (snap->hashrate_1h >= 0.0)  bb_json_obj_set_number(obj, "hashrate_1h",  snap->hashrate_1h);
    else                           bb_json_obj_set_null  (obj, "hashrate_1h");
#endif

#ifdef ASIC_CHIP
    bb_json_obj_set_number(obj, "asic_hashrate",     snap->asic_rate);
    bb_json_obj_set_number(obj, "asic_hashrate_avg", snap->asic_ema);
    bb_json_obj_set_int   (obj, "asic_shares",       (int64_t)snap->asic_shares);
    if (snap->asic_freq_cfg >= 0.0f) bb_json_obj_set_number(obj, "asic_freq_configured_mhz", (double)snap->asic_freq_cfg);
    else                             bb_json_obj_set_null  (obj, "asic_freq_configured_mhz");
    if (snap->asic_freq_eff >= 0.0f) bb_json_obj_set_number(obj, "asic_freq_effective_mhz", (double)snap->asic_freq_eff);
    else                             bb_json_obj_set_null  (obj, "asic_freq_effective_mhz");
    bb_json_obj_set_int   (obj, "asic_small_cores", (int64_t)snap->asic_small_cores);
    bb_json_obj_set_int   (obj, "asic_count",       (int64_t)snap->asic_count);
    if (snap->asic_total_valid) {
        bb_json_obj_set_number(obj, "asic_total_ghs",    (double)snap->asic_total_ghs);
        bb_json_obj_set_number(obj, "asic_hw_error_pct", (double)snap->asic_hw_error_pct);
        if (snap->asic_total_ghs_1m >= 0.0f)   bb_json_obj_set_number(obj, "asic_total_ghs_1m",   (double)snap->asic_total_ghs_1m);
        else                                    bb_json_obj_set_null  (obj, "asic_total_ghs_1m");
        if (snap->asic_total_ghs_10m >= 0.0f)  bb_json_obj_set_number(obj, "asic_total_ghs_10m",  (double)snap->asic_total_ghs_10m);
        else                                    bb_json_obj_set_null  (obj, "asic_total_ghs_10m");
        if (snap->asic_total_ghs_1h >= 0.0f)   bb_json_obj_set_number(obj, "asic_total_ghs_1h",   (double)snap->asic_total_ghs_1h);
        else                                    bb_json_obj_set_null  (obj, "asic_total_ghs_1h");
        if (snap->asic_hw_error_pct_1m >= 0.0f)  bb_json_obj_set_number(obj, "asic_hw_error_pct_1m",  (double)snap->asic_hw_error_pct_1m);
        else                                      bb_json_obj_set_null  (obj, "asic_hw_error_pct_1m");
        if (snap->asic_hw_error_pct_10m >= 0.0f) bb_json_obj_set_number(obj, "asic_hw_error_pct_10m", (double)snap->asic_hw_error_pct_10m);
        else                                      bb_json_obj_set_null  (obj, "asic_hw_error_pct_10m");
        if (snap->asic_hw_error_pct_1h >= 0.0f)  bb_json_obj_set_number(obj, "asic_hw_error_pct_1h",  (double)snap->asic_hw_error_pct_1h);
        else                                      bb_json_obj_set_null  (obj, "asic_hw_error_pct_1h");
    } else {
        bb_json_obj_set_null(obj, "asic_total_ghs");
        bb_json_obj_set_null(obj, "asic_hw_error_pct");
        bb_json_obj_set_null(obj, "asic_total_ghs_1m");
        bb_json_obj_set_null(obj, "asic_total_ghs_10m");
        bb_json_obj_set_null(obj, "asic_total_ghs_1h");
        bb_json_obj_set_null(obj, "asic_hw_error_pct_1m");
        bb_json_obj_set_null(obj, "asic_hw_error_pct_10m");
        bb_json_obj_set_null(obj, "asic_hw_error_pct_1h");
    }
    {
        bb_json_t chips_arr = bb_json_arr_new();
        for (int c = 0; c < snap->n_chips; c++) {
            bb_json_t chip = bb_json_obj_new();
            bb_json_obj_set_int   (chip, "idx",         (int64_t)c);
            bb_json_obj_set_number(chip, "total_ghs",   (double)snap->chips[c].total_ghs);
            bb_json_obj_set_number(chip, "error_ghs",   (double)snap->chips[c].error_ghs);
            bb_json_obj_set_number(chip, "hw_err_pct",  (double)snap->chips[c].hw_err_pct);
            bb_json_obj_set_int   (chip, "total_raw",   (int64_t)snap->chips[c].total_raw);
            bb_json_obj_set_int   (chip, "error_raw",   (int64_t)snap->chips[c].error_raw);
            bb_json_obj_set_number(chip, "total_drops", (double)snap->chips[c].total_drops);
            bb_json_obj_set_number(chip, "error_drops", (double)snap->chips[c].error_drops);
            if (snap->chips[c].last_drop_us == 0 || snap->now_us < (int64_t)snap->chips[c].last_drop_us) {
                bb_json_obj_set_null(chip, "last_drop_ago_s");
            } else {
                uint64_t ago_us = (uint64_t)snap->now_us - snap->chips[c].last_drop_us;
                bb_json_obj_set_number(chip, "last_drop_ago_s", (double)(ago_us / 1000000ULL));
            }
            {
                bb_json_t dghs = bb_json_arr_new();
                for (int d = 0; d < 4; d++) bb_json_arr_append_number(dghs, (double)snap->chips[c].domain_ghs[d]);
                bb_json_obj_set_arr(chip, "domain_ghs", dghs);
            }
            {
                bb_json_t ddrop = bb_json_arr_new();
                for (int d = 0; d < 4; d++) bb_json_arr_append_number(ddrop, (double)snap->chips[c].domain_drops[d]);
                bb_json_obj_set_arr(chip, "domain_drops", ddrop);
            }
            bb_json_arr_append_obj(chips_arr, chip);
        }
        bb_json_obj_set_arr(obj, "asic_chips", chips_arr);
    }
#endif
    bb_json_obj_set_int(obj, "ts_ms", snap->ts_ms);
}

static void stats_save_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        mining_pool_stats_save();
    }
}

static void stats_save_timer_cb(void *arg)
{
    (void)arg;
    if (s_stats_save_task) {
        xTaskNotifyGive(s_stats_save_task);
    }
}

// OTA LED actions for any path (pull / push / boot-mode). bb_ota_led owns the
// lifecycle and guarantees `restore` runs on every terminal-non-reboot outcome
// (FAIL/abort), so the heartbeat is never left latched on a fail color while the
// miner keeps running. Rendering is ours (board-specific bb_led); the revert
// contract is breadboard's. No-op on boards whose led component stubs.
static void tm_ota_led_updating(void *ctx, int pct) { (void)ctx; (void)pct; led_blink(25, 500); }   // blink: updating
static void tm_ota_led_success(void *ctx)           { (void)ctx; led_set_color(0, 38, 0); }          // green: done (reboot imminent)
static void tm_ota_led_restore(void *ctx)
{
    (void)ctx;
    // OTA ended without a reboot: return to the steady mining heartbeat (or off if disabled).
    if (config_led_heartbeat_enabled()) led_set_mining(true);
    else led_off();
}
static const bb_ota_led_ops_t s_tm_ota_led_ops = {
    .updating = tm_ota_led_updating,
    .success  = tm_ota_led_success,
    .restore  = tm_ota_led_restore,
};

// OTA work+display pause hook (wired into bb_ota_push/pull). The consumer decides
// WHAT to pause: mining (free heap/CPU for the transfer + erase) AND the display
// (stop SPI blits, go dark). Returns mining_pause()'s result so bb_ota_push only
// resumes if we actually paused.
static bool tm_ota_pause(void)
{
    bool paused = mining_pause();
    bb_pub_pause();
    bb_mqtt_suspend_default();   // drop the mqtt connection: frees ~11KB heap + socket
                                 // the TLS handshake needs (B1-281). reconnects on resume.
    s_display_quiesced = true;
    ui_display_off();
    return paused;
}

static void tm_ota_resume(void)
{
    s_display_quiesced = false;
    // only re-light the panel if the user hasn't disabled the display
    if (bb_nv_config_display_enabled()) ui_display_on();
    bb_mqtt_resume_default();
    bb_pub_resume();
    mining_resume();
}

#ifndef ASIC_CHIP
// Update-check pause hook: suspend mining + telemetry publishing to free heap
// for the manifest-fetch TLS handshake (B1-280/281). No display-off (brief check).
static bool tm_update_pause(void)
{
    bool paused = mining_pause();
    bb_pub_pause();
    bb_mqtt_suspend_default();   // drop the mqtt connection so the GitHub update-check
                                 // TLS handshake has the heap it had pre-telemetry (B1-281)
    return paused;
}
static void tm_update_resume(void)
{
    bb_mqtt_resume_default();
    bb_pub_resume();
    mining_resume();
}
#endif

// TA-122: gold LED flash on block found. Fire-and-forget via led_flash_block_found()
// (sets a one-shot PULSE; heartbeat resumes on the next led_set_mining() call).
static void on_block_found(void)
{
    led_flash_block_found();
}

static void start_mining(void)
{
    // Create inter-task queues
    work_queue = xQueueCreate(1, sizeof(mining_work_t));
    result_queue = xQueueCreate(16, sizeof(mining_result_t));

    // Initialize mining stats
    mining_stats_init();
    mining_pool_stats_init();

    mining_pause_init(&g_mining_pause_sync_ops_default);

    // TA-122: fire a gold LED flash when a block is found (fire-and-forget).
    mining_set_block_found_cb(on_block_found);

    // Create task for NVS stats save (low priority, blocks on notification)
    xTaskCreate(stats_save_task, "nvs_save", 4096, NULL, 1, &s_stats_save_task);

    // Start periodic stats save timer (10 minutes)
    BB_ERROR_CHECK(bb_timer_periodic_create(stats_save_timer_cb, NULL, "stats_save", &s_stats_timer));
    BB_ERROR_CHECK(bb_timer_periodic_start(s_stats_timer, 10ULL * 60 * 1000000));

    // TA-341: run SHA self-tests synchronously before any task starts
    // so the failure flag is committed before stratum / mining can read it.
    mining_run_self_tests();

    if (mining_sha_self_test_failed()) {
        // No mining → no shares → no point holding a pool socket open.
        // HTTP/UI/OTA tasks (already started above) stay up so the failure
        // surfaces on the dashboard and the device can be recovered.
        bb_log_w(TAG, "SHA self-test failed: stratum and mining tasks not started");
    } else {
        // Start stratum task on Core 0. Single-core (S2/C3) has no PSRAM and
        // tight, fragmented heap after WiFi/lwIP/mDNS init — an 8KB stack can't
        // find a contiguous block. 6KB fits and is ample for plain-TCP stratum.
#if CONFIG_FREERTOS_UNICORE
        const uint32_t stratum_stack = 6144;
#else
        // 6144 keeps >=2560 B margin (HWM showed ~3300 B free at 8192); recouped 2048 B.
        const uint32_t stratum_stack = 6144;
#endif
        xTaskCreatePinnedToCore(stratum_task, "stratum", stratum_stack, NULL, 5, NULL, 0);

        // Start miner task (board-specific config from g_miner_config)
        xTaskCreatePinnedToCore(g_miner_config.task_fn, g_miner_config.name,
                                g_miner_config.stack_size, NULL, g_miner_config.priority,
#ifdef ASIC_CHIP
                                &asic_task_handle,
#else
                                &mining_hw_task_handle,
#endif
                                g_miner_config.core);
    }

    bb_log_i(TAG, "all tasks started");

    // All mining/pool/stratum state is initialized; sample_fns may now read live data.
    s_telemetry_ready = true;
}

#ifdef BOARD_HAS_DISPLAY
static void display_status_task(void *arg)
{
    (void)arg;
    display_status_t status = {0};
    int tick = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));

        if (s_display_quiesced) continue;  // OTA in progress: stop blitting, panel is dark

        if (tick % 100 == 0) {
            if (mining_stats.mutex != NULL && xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
#ifdef ASIC_CHIP
                status.hashrate = mining_stats.asic_ema.value;
                status.temp_c = mining_stats.asic_temp_c;
#else
                status.hashrate = mining_stats.hw_ema.value;
                status.temp_c = mining_stats.temp_c;
#endif
                status.shares = mining_stats.session.shares;
                status.rejected = mining_stats.session.rejected;
                status.uptime_us = (int64_t)bb_timer_now_us() - mining_stats.session.start_us;
                xSemaphoreGive(mining_stats.mutex);
            }

            // Populate network diagnostics (lock-free getters)
            int8_t rssi = 0;
            bb_wifi_get_rssi(&rssi);
            status.rssi = rssi;
            bb_wifi_get_ip_str(status.ip, sizeof(status.ip));
            int64_t age_us = 0;
            bb_wifi_get_disconnect(&status.wifi_disc_reason, &age_us);
            status.wifi_disc_age_s = (uint32_t)(age_us / 1000000);
            status.wifi_retry_count = bb_wifi_get_retry_count();
            status.mdns_ok = bb_mdns_started();
            status.stratum_ok = stratum_is_connected();
            status.stratum_reconnect_ms = stratum_get_reconnect_delay_ms();
            status.stratum_fail_count = stratum_get_connect_fail_count();
        }
        tick++;

        ui_show_status(&status);
    }
}
#endif

static void log_reset_reason(void)
{
    const char *reason_str = bb_system_reset_reason_str(bb_system_get_reset_reason());
    bb_log_i(TAG, "reset reason: %s", reason_str);

    if (bb_system_is_abnormal_reset()) {
        bb_log_w(TAG, "abnormal reset detected (%s)", reason_str);
    }
}

// cppcheck-suppress unusedFunction
void app_main(void)
{
    bb_log_i(TAG, "%s v%s (%s %s, IDF %s) starting...",
             bb_system_get_project_name(), bb_system_get_version(),
             bb_system_get_build_date(), bb_system_get_build_time(),
             bb_system_get_idf_version());

    // Suppress noisy framework log tags (before wifi_init)
    bb_log_level_set("wifi", BB_LOG_LEVEL_WARN);
    bb_log_level_set("wifi_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("net80211", BB_LOG_LEVEL_WARN);
    bb_log_level_set("pp", BB_LOG_LEVEL_WARN);
    bb_log_level_set("phy_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_handlers", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_lwip", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp-x509-crt-bundle", BB_LOG_LEVEL_WARN);
    bb_log_level_set("httpd_uri", BB_LOG_LEVEL_WARN);
    bb_log_level_set("httpd_txrx", BB_LOG_LEVEL_WARN);
    bb_log_level_set("httpd", BB_LOG_LEVEL_WARN);

    // Register TM-owned tags so they appear in GET /api/log/level discovery
    bb_log_tag_register("taipanminer", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("stratum", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("mining", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("sha256_hw", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("display", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("led", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("ota_validator", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("config", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("web", BB_LOG_LEVEL_INFO);
    // "diag" — opt-in instrumentation (per-job hashrate, tier1_dwell, job_swap,
    // share-ack latency, asic periodic stats). Default WARN suppresses info-level
    // probes; bump to DEBUG/INFO via /api/log/level when investigating.
    bb_log_tag_register("diag", BB_LOG_LEVEL_WARN);
#ifdef ASIC_CHIP
    bb_log_tag_register("asic", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("emc2101", BB_LOG_LEVEL_INFO);
    bb_log_tag_register("tps546", BB_LOG_LEVEL_INFO);
#endif
#ifdef ASIC_BM1370
    bb_log_tag_register("bm1370", BB_LOG_LEVEL_INFO);
#endif
#ifdef ASIC_BM1368
    bb_log_tag_register("bm1368", BB_LOG_LEVEL_INFO);
#endif

    // Initialize early-tier registry (log_stream, nv_flash, nv_config)
    BB_ERROR_CHECK(bb_registry_init_early());
    BB_ERROR_CHECK(config_init());
    // Register manifest so /api/manifest exposes the NVS keyspace
    BB_ERROR_CHECK(config_register_manifest());
    log_reset_reason();
    BB_ERROR_CHECK(led_init());
    bb_led_register_info();

    // Register OTA LED actions before any OTA path can fire (boot-mode / pull / push).
    bb_ota_led_init(&s_tm_ota_led_ops, NULL);

#ifdef CONFIG_BB_OTA_STRATEGY_BOOT
    // OTA-only boot mode (non-PSRAM boards): if armed via
    // POST /api/update/apply, pull the new firmware at FULL early-boot heap
    // before any subsystem allocates, then reboot into it. Never returns when
    // armed; returns immediately otherwise. WiFi STA was started by
    // bb_registry_init_early(); bb_ota_boot waits for the link + NTP internally
    // and broadcasts its trace over the bb_log UDP sink (headless observability).
    bb_ota_set_progress_cb(bb_ota_led_progress);
    // Advertise the same mDNS identity the device uses in normal mining mode so
    // the fleet-update UI can find it during the boot-OTA download window.
    // config_init() has already run; config_hostname() is available here.
    {
        char hn[64];
        const char *hostname = config_hostname();
        if (hostname && hostname[0]) {
            strncpy(hn, hostname, sizeof(hn) - 1);
            hn[sizeof(hn) - 1] = '\0';
        } else {
            bb_mdns_build_hostname(config_worker_name(), NULL, hn, sizeof(hn));
        }
        bb_ota_boot_set_mdns_service(hn, "_taipanminer", "_tcp", 80);
    }
    bb_ota_boot_run_if_pending(
        "https://api.github.com/repos/dangernoodle-io/TaipanMiner/releases/latest",
        "taipanminer-" FIRMWARE_BOARD);
#endif

    // Boot failure counter — incremented only on WiFi timeout restart (wifi_prov.c),
    // not on every boot, so flash/power-cycle doesn't trigger AP fallback.
    uint8_t boot_cnt = bb_nv_config_boot_count();

    // Register TaipanMiner-specific info extender for breadboard's /api/info endpoint
    BB_ERROR_CHECK(webui_register_info_extender());
    // Register TM extenders for BB-owned /api/power and /api/fan routes (P4b)
    BB_ERROR_CHECK(webui_register_power_fan_extenders());

    if (should_fall_back_to_ap(boot_cnt, bb_nv_config_is_provisioned(), bb_ota_is_validated())) {
        bb_log_w(TAG, "boot_count=%" PRIu8 " >= %d: clearing provisioning for AP fallback",
                 boot_cnt, BB_NV_CONFIG_BOOT_FAIL_THRESHOLD);
        bb_nv_config_clear_wifi();
        bb_nv_config_clear_provisioned();
        bb_nv_config_reset_boot_count();
    } else if (boot_cnt > 1) {
        const char *validated_note = bb_ota_is_validated() ? " (validated, will not fall back)" : "";
        bb_log_w(TAG, "boot_count=%" PRIu8 " (%d until AP fallback)%s",
                 boot_cnt, BB_NV_CONFIG_BOOT_FAIL_THRESHOLD - boot_cnt, validated_note);
    }

#ifdef ASIC_CHIP
    // Initialize ASIC before WiFi — freq ramp takes ~8s, runs while WiFi isn't needed yet.
    // Skip if not provisioned (will enter AP mode instead).
    if (bb_nv_config_is_provisioned()) {
        BB_ERROR_CHECK(asic_init());
    }
#endif

#if defined(BOARD_HAS_DISPLAY) && !defined(TM_BENCH_QUIET)
    // Initialize display early so splash is visible during boot.
    // On Bitaxe, hand the ASIC's shared I2C bus to bb_display_ssd1306
    // before bb_display_init so we don't open a second bus instance.
#if defined(BOARD_DISPLAY_SHARES_ASIC_I2C) && defined(ASIC_CHIP)
    bb_display_ssd1306_set_i2c_bus(asic_get_i2c_bus());
#endif
    // Display is optional hardware: a probe/init failure must never brick a
    // serial-less miner. Degrade to headless (matches bb_display_register_info's
    // present:false fallback) instead of aborting at boot.
    if (bb_display_init() == BB_OK) {
        ui_show_splash();
        vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
        bb_log_w(TAG, "display init failed; continuing headless");
    }
#endif
#ifdef TM_BENCH_QUIET
    bb_log_w(TAG, "TM_BENCH_QUIET: display disabled");
#endif
    // Register /api/info and /api/health satellite extenders. All three degrade
    // to present:false gracefully when hardware is absent (no display on wroom32,
    // no LED on bitaxe, no SoC temp sensor on classic ESP32). Must run before
    // bb_registry_init() which starts the HTTP server and freezes the extender table.
    bb_display_register_info();
    bb_net_health_register_health();

    if (!bb_nv_config_is_provisioned()) {
        bb_log_i(TAG, "entering provisioning mode");
        // Configure provisioning before AP start
        bb_prov_set_ap_ssid_prefix("TaipanMiner-");
        bb_prov_set_ap_password("taipanminer");
        bb_mdns_set_service_type("_taipanminer");
        {
            char hn[64];
            const char *hostname = config_hostname();
            if (hostname && hostname[0]) {
                strncpy(hn, hostname, sizeof(hn) - 1);
                hn[sizeof(hn) - 1] = '\0';
            } else {
                /* First-boot edge before migration runs (un-provisioned device). Fall
                 * back to a sanitized worker so mDNS still has *something* to announce
                 * during the AP/prov flow. */
                bb_mdns_build_hostname(config_worker_name(), NULL, hn, sizeof(hn));
            }
            /* Instance name == hostname so dns-sd browses surface a useful identifier
             * (e.g. "tdongles3-1") instead of the generic "TaipanMiner". */
            bb_mdns_set_instance_name(hn);
            bb_mdns_set_hostname(hn);
        }

        bool mdns_en = bb_nv_config_mdns_enabled();
        if (mdns_en) {
            bb_mdns_init();
            bb_mdns_set_txt("worker", "");
            bb_mdns_set_txt("board", FIRMWARE_BOARD);
            bb_mdns_set_txt("version", bb_system_get_version());
            bb_mdns_set_txt("state", "provisioning");
            bb_mdns_set_txt("ui", MINER_UI_TXT);
        }
        BB_ERROR_CHECK(bb_prov_start_ap());
        webui_install_prov_save_cb();
        {
            size_t n;
            const bb_http_asset_t *assets = webui_prov_assets(&n);
            BB_ERROR_CHECK(bb_prov_start(assets, n, NULL));
        }

        // Show provisioning info on display + solid blue LED
        char ap_ssid[32];
        bb_prov_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
#ifdef BOARD_HAS_DISPLAY
        ui_show_prov(ap_ssid, "taipanminer");
#endif
        BB_ERROR_CHECK(led_set_color(0, 0, 38));

        bool connected = false;
        // cppcheck-suppress knownConditionTrueFalse
        // Loop exits via esp_restart() in the success branch; the failure
        // branch re-enters provisioning indefinitely. `connected` stays false
        // by design — terminating the loop normally would skip the reboot
        // that TA-225 documented as required for clean asic_init.
        while (!connected) {
            bb_prov_wait_done(UINT32_MAX);

            bb_prov_stop_ap();

            bb_err_t err = bb_wifi_init_sta();
            if (err == BB_OK) {
                BB_ERROR_CHECK(led_off());
                bb_log_i(TAG, "provisioning complete; restarting into mining mode");
                bb_nv_config_set_provisioned();
                bb_nv_config_reset_boot_count();
                // Reboot so mining init follows the same path as every subsequent boot:
                // asic_init() runs before tasks start. In-process transition skipped
                // asic_init, leaving UART/I2C drivers uninstalled and the ASIC task
                // spinning on ESP_ERR_INVALID_STATE until a manual reboot. (TA-225)
                vTaskDelay(pdMS_TO_TICKS(100));  // let the log flush
                bb_system_restart();
            } else {
                bb_log_w(TAG, "STA connect failed, re-entering provisioning");
                // Reinitialize AP for retry
                BB_ERROR_CHECK(bb_prov_start_ap());
            }
        }
    } else {
        // Normal boot: connect to saved WiFi
#ifdef TM_BENCH_QUIET
        bb_log_w(TAG, "TM_BENCH_QUIET: skipping mDNS/HTTP/knot/webui — WiFi managed by BB EARLY tier");
        goto bench_quiet_skip_net;
#endif
        bb_mdns_set_service_type("_taipanminer");
        {
            char hn[64];
            const char *hostname = config_hostname();
            if (hostname && hostname[0]) {
                strncpy(hn, hostname, sizeof(hn) - 1);
                hn[sizeof(hn) - 1] = '\0';
            } else {
                /* First-boot edge before migration runs (un-provisioned device). Fall
                 * back to a sanitized worker so mDNS still has *something* to announce
                 * during the AP/prov flow. */
                bb_mdns_build_hostname(config_worker_name(), NULL, hn, sizeof(hn));
                /* TA-405: persist the derived value to NVS so subsequent reads
                 * (/api/info, knot self-peer at lines ~577-583, taipan-cli filtered
                 * queries) see a real hostname instead of empty. Pre-fix, the fallback
                 * only seeded the live mDNS announce; NVS stayed empty across boots. */
                bb_err_t rc = bb_nv_config_set_hostname(hn);
                if (rc != BB_OK) {
                    bb_log_w(TAG, "TA-405: failed to persist derived hostname '%s': %d", hn, rc);
                }
            }
            /* Instance name == hostname so dns-sd browses surface a useful identifier. */
            bb_mdns_set_instance_name(hn);
            bb_mdns_set_hostname(hn);
        }
        bool mdns_en = bb_nv_config_mdns_enabled();
#if CONFIG_KNOT_ENABLED
        bool knot_en = config_knot_enabled() && mdns_en;  // dependency
#endif
        if (mdns_en) {
            bb_mdns_init();
            bb_mdns_set_txt("worker", config_worker_name());
            bb_mdns_set_txt("board", FIRMWARE_BOARD);
            bb_mdns_set_txt("version", bb_system_get_version());
            bb_mdns_set_txt("state", "mining");
            bb_mdns_set_txt("ui", MINER_UI_TXT);
        }
#if CONFIG_KNOT_ENABLED
        if (knot_en) {
            BB_ERROR_CHECK(knot_init());
            {
                /* Inject self into the peer table — mdns_browse never reports the
                 * local device, so without this the device wouldn't show in its
                 * own /api/knot. Use the same identity we just announced. */
                char hn[64];
                const char *hostname = config_hostname();
                if (hostname && hostname[0]) {
                    strncpy(hn, hostname, sizeof(hn) - 1);
                    hn[sizeof(hn) - 1] = '\0';
                } else {
                    bb_mdns_build_hostname(config_worker_name(), NULL, hn, sizeof(hn));
                }
                knot_set_self(hn, hn, NULL,
                              config_worker_name(),
                              FIRMWARE_BOARD,
                              bb_system_get_version(),
                              "mining");
            }
        }
#endif
        // task_core / task_priority MUST be set BEFORE bb_registry_init — they're
        // sampled at xTaskCreatePinnedToCore time inside the registry walk.
        // Pin the upd_check worker to Core 1. Core 0 already carries httpd + lwip +
        // wifi + stratum; a Core-0-bound mbedTLS handshake starves IDLE0 past the 60s
        // task watchdog (BB B1-217).
        bb_update_check_set_task_core(1);
#ifndef ASIC_CHIP
        // Tdongle: mining_hw task runs on Core 1 at prio 20 (CPU-bound SHA hot-loop).
        // Worker at default prio 1 would never get CPU to call mining_pause(). Raise
        // above mining so the kick actually preempts and calls the pause hook.
        bb_update_check_set_task_priority(21);
#endif
#ifdef ASIC_CHIP
        bb_ota_pull_set_task_core(1);
#else
        // Tdongle (no ASIC): Core 1 runs idle task; pinning there hits an esp-idf
        // DVFS race (esp_cpu_unstall during flash-op stall) — let scheduler pick.
        bb_ota_pull_set_task_core(tskNO_AFFINITY);
        // Like the update-check worker above: the mining_hw task runs at prio 20.
        // The pull-download worker defaults to prio 3, so on single-core boards
        // (S2/C3) it can never preempt mining to call the pause hook — the
        // download then starves the idle task and trips the WDT (the extended OTA
        // WDT only delays it). Raise above mining so the worker pauses mining,
        // gets the core to itself, and the download-loop yield can feed idle.
        bb_ota_pull_set_task_priority(21);
#endif

        // Initialize registry: walks PRE_HTTP tier (CORS, OpenAPI meta, route-reserve),
        // auto-starts HTTP server (CONFIG_BB_HTTP_AUTOSTART=y), then walks regular tier
        // (auto-registers all breadboard routes and endpoints). Creates the
        // bb_update_check + bb_ota_pull worker tasks using the affinity/priority above.
        BB_ERROR_CHECK(bb_registry_init());

        // Register mining telemetry source — publishes hashrate/shares/rejected
        // on the "mining" MQTT topic each bb_pub tick.
        bb_pub_register_source("mining", tm_mining_sample, NULL);
        // B1-352 / SSOT: register telemetry sources via bb_pub_register_telemetry.
        // gather+serialize split: gather stamps ts_ms and fills the snapshot struct;
        // serialize writes JSON from the struct into bb_cache (tree API).
        // BB_PUB_TELEM_SSE fans out SSE via bb_cache; no manual bb_sink_event needed.
        {
            static const bb_pub_telemetry_cfg_t k_mining_rates_telem = {
                .topic     = "mining_rates",
                .gather    = tm_mining_rates_gather,
                .serialize = tm_mining_rates_serialize,
                .snap_size = sizeof(mining_rates_snapshot_t),
                .flags     = BB_PUB_TELEM_SSE | BB_PUB_TELEM_SINKS,
                .ctx       = NULL,
            };
            bb_pub_register_telemetry(&k_mining_rates_telem);
        }
        {
            static const bb_pub_telemetry_cfg_t k_pool_telem = {
                .topic     = "pool",
                .gather    = tm_pool_pub_gather,
                .serialize = tm_pool_pub_serialize,
                .snap_size = sizeof(pool_pub_snapshot_t),
                .flags     = BB_PUB_TELEM_SSE | BB_PUB_TELEM_SINKS,
                .ctx       = NULL,
            };
            bb_pub_register_telemetry(&k_pool_telem);
        }
#ifdef ASIC_CHIP
        {
            static const bb_pub_telemetry_cfg_t k_sensors_miner_telem = {
                .topic     = "sensors_miner",
                .gather    = tm_sensors_miner_gather,
                .serialize = tm_sensors_miner_serialize,
                .snap_size = sizeof(sensors_miner_snapshot_t),
                .flags     = BB_PUB_TELEM_SSE | BB_PUB_TELEM_SINKS,
                .ctx       = NULL,
            };
            bb_pub_register_telemetry(&k_sensors_miner_telem);
        }
#endif
        {
            static const bb_pub_telemetry_cfg_t k_stats_telem = {
                .topic     = "stats",
                .gather    = tm_stats_gather,
                .serialize = tm_stats_serialize,
                .snap_size = sizeof(stats_snapshot_t),
                .flags     = BB_PUB_TELEM_SSE | BB_PUB_TELEM_SINKS,
                .ctx       = NULL,
            };
            bb_pub_register_telemetry(&k_stats_telem);
        }

        // Register "block.found" SSE topic and hand the handle to mining_pool_stats
        // so record_block() can post events. Must run after bb_registry_init() so
        // bb_event_routes is already initialized.
        //
        // Non-fatal: block.found is an optional live-notification feature. If
        // bb_event isn't initialized (e.g. CONFIG_BB_EVENT_AUTOREGISTER off), the
        // register call returns BB_ERR_INVALID_STATE — log and continue without
        // the topic rather than BB_ERROR_CHECK→abort. A headless board must never
        // crash-loop over an optional SSE topic (the S2 did exactly that for weeks
        // when its generated sdkconfig had bb_event autoregister stale-off).
        {
            static bb_event_topic_t s_block_topic = NULL;
            bb_err_t evt_err = bb_event_topic_register("block.found", &s_block_topic);
            if (evt_err == BB_OK) {
                evt_err = bb_event_routes_attach_ex("block.found", false);
            }
            if (evt_err == BB_OK) {
                mining_pool_stats_set_block_topic(s_block_topic);
            } else {
                bb_log_w(TAG, "block.found SSE unavailable (err %d): continuing without "
                              "live block events (CONFIG_BB_EVENT_AUTOREGISTER on?)", evt_err);
            }
        }

        // B1-352: register pool.notify -- non-retained event topic for new stratum jobs.
        // Posted from stratum task on each mining.notify received.
        // Non-fatal: SSE feature is optional.
        {
            static bb_event_topic_t s_pool_notify_topic = NULL;
            bb_err_t evt_err = bb_event_topic_register("pool.notify", &s_pool_notify_topic);
            if (evt_err == BB_OK) {
                evt_err = bb_event_routes_attach_ex("pool.notify", false);
            }
            if (evt_err != BB_OK) {
                bb_log_w(TAG, "pool.notify SSE unavailable (err %d)", evt_err);
            }
        }

        // B1-352: register health.alerts -- non-retained event topic for
        // sha-self-test / vcore-fault / vin-low / stratum-state transitions.
        // Non-fatal: SSE feature is optional.
        {
            bb_err_t evt_err = bb_event_topic_register("health.alerts", &s_health_alerts_topic);
            if (evt_err == BB_OK) {
                evt_err = bb_event_routes_attach_ex("health.alerts", false);
            }
            if (evt_err != BB_OK) {
                bb_log_w(TAG, "health.alerts SSE unavailable (err %d)", evt_err);
            }
        }

        // Attach "net.health" retained SSE topic and start 5-second link-health
        // evaluator. Must run after bb_registry_init() so bb_event_routes is up.
        // Non-fatal: degrades gracefully (no SSE topic) rather than aborting.
        {
            bb_err_t net_err = bb_net_health_attach_sse();
            if (net_err != BB_OK) {
                bb_log_w(TAG, "bb_net_health_attach_sse failed (err %d): "
                              "net.health SSE unavailable", net_err);
            }
        }

        // B1-352: register bb_sink_event for SSE fan-out of bb_pub periodic sources.
        // Subscribe EXPLICITLY to the periodic topics we want on SSE (not default-all).
        // MQTT gets the aggregate periodic sources via its own (default-all) subscription.
        // Event topics (pool.notify, health.alerts, block.found, net.health) are handled
        // via bb_event_post directly -- they are NOT teed through bb_pub periodic sources.
        //
        // Non-fatal: if bb_sink_event setup fails, MQTT still works normally.
        {
            // Register the per-topic SSE endpoints (each calls bb_event_topic_register +
            // bb_event_routes_attach_ex internally).
            // B1-352 / SSOT: mining_rates, pool, sensors_miner, and stats are now
            // registered via bb_pub_register_telemetry with BB_PUB_TELEM_SSE — they
            // get SSE fan-out through bb_cache automatically and must NOT appear here.
            static const char *const k_sse_topics[] = {
                "sys.mem", "net.detail",
                // B1-352: fan/power/thermal SSE topics are gated to the same per-board
                // hardware opt-in as their bb_pub_* sources (breadboard v0.70.0 AUTO_ATTACH).
                // Without the source there is no data, so don't register an empty SSE topic.
#ifdef CONFIG_BB_PUB_FAN_AUTO_ATTACH
                "fan",
#endif
#ifdef CONFIG_BB_PUB_POWER_AUTO_ATTACH
                "power",
#endif
#ifdef CONFIG_BB_PUB_THERMAL_AUTO_ATTACH
                "thermal",
#endif
            };
            bb_err_t sink_err = BB_OK;
            for (size_t i = 0; i < sizeof(k_sse_topics) / sizeof(k_sse_topics[0]); i++) {
                sink_err = bb_sink_event_register_topic(k_sse_topics[i], true);
                if (sink_err != BB_OK) {
                    bb_log_w(TAG, "bb_sink_event_register_topic(%s) failed (err %d): "
                                  "continuing without SSE topic", k_sse_topics[i], sink_err);
                    sink_err = BB_OK; // continue; non-fatal per topic
                }
            }

            // Wire the SSE sink into bb_pub fan-out (additive; does not displace MQTT).
            static bb_pub_sink_t s_sse_sink;
            bb_sink_event(&s_sse_sink);
            sink_err = bb_pub_add_sink(&s_sse_sink);
            if (sink_err == BB_OK) {
                // Seed all retained topics so SSE clients connecting at boot get current state.
                bb_sink_event_seed_all();
            } else {
                bb_log_w(TAG, "bb_pub_add_sink(SSE) failed (err %d): SSE telemetry unavailable",
                          sink_err);
            }
        }

        // Setters below depend on bb_update_check_init / bb_ota_pull_init having
        // run (they early-return BB_ERR_INVALID_STATE before init). The values are
        // sampled per-fetch in run_one, not at task creation, so this order is correct.
        bb_update_check_set_releases_url(
            "https://api.github.com/repos/dangernoodle-io/TaipanMiner/releases/latest");
        bb_update_check_set_firmware_board("taipanminer-" FIRMWARE_BOARD);
#ifndef ASIC_CHIP
        // Tdongle: USE the pause hook. Mining on Core 1 needs to be suspended for
        // the TLS handshake. (Bitaxe: NO pause hook — bm1370 quiesce/resume churns
        // heap; mining keeps running through the check.)
        bb_update_check_set_hooks(tm_update_pause, tm_update_resume);
#endif

        bb_ota_pull_set_releases_url("https://api.github.com/repos/dangernoodle-io/TaipanMiner/releases/latest");
        bb_ota_pull_set_http_timeout_ms(60000);
        // Unified OTA hooks (B1-255): one set of pause/resume + skip-check +
        // progress callbacks shared by the push, pull, and boot paths. Progress
        // is a no-op on boards whose led component stubs (S2).
        bb_ota_set_hooks(tm_ota_pause, tm_ota_resume);
        bb_ota_set_skip_check_cb(bb_nv_config_ota_skip_check);
        bb_ota_set_progress_cb(bb_ota_led_progress);
        // Register mDNS keys (manifest auto-registered by registry)
        {
            static const bb_manifest_mdns_t taipan_mdns_keys[] = {
                {.key = "worker", .desc = "stratum worker name"},
                {.key = "board", .desc = "firmware board identifier", .values = "tdongle-s3|bitaxe-601|bitaxe-403|bitaxe-650"},
                {.key = "version", .desc = "firmware semver"},
                {.key = "state", .desc = "device lifecycle state", .values = "provisioning|mining|ota"},
                {.key = "ui", .desc = "serves mining web UI", .values = "0|1"},
            };
            BB_ERROR_CHECK(bb_manifest_register_mdns("_taipanminer._tcp", taipan_mdns_keys, sizeof(taipan_mdns_keys) / sizeof(taipan_mdns_keys[0])));
        }
        BB_ERROR_CHECK(webui_register_mining_routes(bb_http_server_get_handle()));
#ifdef TM_BENCH_QUIET
bench_quiet_skip_net:;
#endif
    }

#ifndef TM_BENCH_QUIET
    // Sync time via SNTP (UTC)
    bb_ntp_start("pool.ntp.org");

    // bb_update_check + bb_ota_pull setters moved earlier (before bb_registry_init)
    // so task_core/task_priority take effect at worker-task creation time.

    // OTA push pause/resume + skip-check come from the unified bb_ota_set_*
    // hooks wired above (B1-255).

    // Wire production OTA validator ops before stratum starts
    ota_validator_init(&g_ota_timer_ops_default, &g_ota_mark_valid_ops_default);
#else
    bb_log_w(TAG, "TM_BENCH_QUIET: skipping NTP, OTA-pull, OTA-push, OTA-validator");
#endif

    // Start mining
    start_mining();

    // Status LED: slow dim breathe while mining (headless "alive + hashing"
    // signal). Gated by the led_heartbeat_en NVS setting (default on); when off
    // the LED stays dark while mining. No-op on boards without a status LED;
    // overridden by the OTA progress callback during an update.
    if (config_led_heartbeat_enabled()) {
        led_set_mining(true);
    } else {
        led_off();
    }

#if defined(BOARD_HAS_DISPLAY) && !defined(TM_BENCH_QUIET)
    // Start display status task on Core 0. Stack 4608: HWM showed ~4264 B free
    // (1880 used) at 6144; 4608 keeps >=2560 B margin (recouped 1536 B).
    xTaskCreatePinnedToCore(display_status_task, "display", 4608, NULL, 2, NULL, 0);
#endif
}
