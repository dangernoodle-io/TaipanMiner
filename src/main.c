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
#include "partition_fixup.h"
#include "bb_log.h"
#include "bb_ota_pull.h"
#include "bb_ota_push.h"
#include "bb_ota_boot.h"
#include "bb_ota_led.h"
#include "bb_ota_hooks.h"
#include "bb_update_check.h"
#include "bb_manifest.h"
#include "bb_registry.h"
#include "bb_event.h"
#include "bb_event_routes.h"
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
#endif
#include "bb_display_info.h"
#include "bb_led_info.h"
#include "bb_temp.h"

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

// Set while an OTA transfer (push/pull) holds the device: the display status
// task stops blitting (frees the SPI bus) and the panel is blanked.
static volatile bool s_display_quiesced = false;

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
    s_display_quiesced = true;
    ui_display_off();
    return paused;
}

static void tm_ota_resume(void)
{
    s_display_quiesced = false;
    ui_display_on();
    mining_resume();
}

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

    // Register WiFi kick callback for zombie-state recovery
    stratum_set_wifi_kick_cb(bb_wifi_force_reassociate);

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
        const uint32_t stratum_stack = 8192;
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
            if (xSemaphoreTake(mining_stats.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
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
    partition_fixup_check();

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
    bb_log_tag_register("partition_fixup", BB_LOG_LEVEL_INFO);
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

#ifdef BOARD_OTA_BOOT_MODE
    // OTA-only boot mode (tight/serial-less boards, e.g. S2): if armed via
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
    bb_temp_register_info();

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
        bb_update_check_set_hooks(mining_pause, mining_resume);
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
    // Start display status task on Core 0
    xTaskCreatePinnedToCore(display_status_task, "display", 6144, NULL, 2, NULL, 0);
#endif
}
