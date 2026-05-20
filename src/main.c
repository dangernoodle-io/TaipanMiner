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
#include "led.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "partition_fixup.h"
#include "bb_log.h"
#include "bb_ota_pull.h"
#include "bb_ota_push.h"
#include "bb_update_check.h"
#include "bb_manifest.h"
#include "bb_registry.h"
#include "bb_event.h"
#include "bb_event_routes.h"
#include "knot.h"
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

static const char *TAG = "taipanminer";

// Mining task handles (for suspend/resume during OTA verification)
#ifdef ASIC_CHIP
TaskHandle_t asic_task_handle = NULL;
#else
TaskHandle_t mining_hw_task_handle = NULL;
#endif

static esp_timer_handle_t s_stats_timer = NULL;
static TaskHandle_t s_stats_save_task = NULL;

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

static void start_mining(void)
{
    // Create inter-task queues
    work_queue = xQueueCreate(1, sizeof(mining_work_t));
    result_queue = xQueueCreate(16, sizeof(mining_result_t));

    // Initialize mining stats
    mining_stats_init();
    mining_pool_stats_init();

    mining_pause_init(&g_mining_pause_sync_ops_default);

    // Create task for NVS stats save (low priority, blocks on notification)
    xTaskCreate(stats_save_task, "nvs_save", 4096, NULL, 1, &s_stats_save_task);

    // Start periodic stats save timer (10 minutes)
    const esp_timer_create_args_t timer_args = {
        .callback = stats_save_timer_cb,
        .name = "stats_save",
    };
    BB_ERROR_CHECK(esp_timer_create(&timer_args, &s_stats_timer));
    BB_ERROR_CHECK(esp_timer_start_periodic(s_stats_timer, 10ULL * 60 * 1000000));

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
        // Start stratum task on Core 0
        xTaskCreatePinnedToCore(stratum_task, "stratum", 8192, NULL, 5, NULL, 0);

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
                status.uptime_us = esp_timer_get_time() - mining_stats.session.start_us;
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

/* ---------------------------------------------------------------------------
 * migrate_legacy_bb_keys: one-shot copy of BB-owned NVS keys from the legacy
 * "taipanminer" namespace into "bb_cfg" using direct ESP-IDF NVS API.
 *
 * MUST run before bb_registry_init_early() so bb_nv_config_init (EARLY tier)
 * reads the migrated wifi creds and bb_wifi_autoinit can connect. Using bb_nv_config_*
 * wrappers here would be wrong — they depend on bb_nv_config_init having already
 * loaded NVS into the in-memory s_config cache.
 *
 * Safe to call every boot: the "bb_cfg key already present → skip" guard makes
 * it a no-op after the first successful migration. Legacy keys are erased after
 * copying so no orphan data survives.
 * --------------------------------------------------------------------------- */
#ifdef ESP_PLATFORM
static void migrate_legacy_bb_keys(void)
{
    /* nvs_flash_init is idempotent; bb_nv_flash_init (called later inside
     * bb_nv_config_init) performs the same call and the erase-on-error retry. */
    nvs_flash_init();

    nvs_handle_t legacy = 0, new_ns = 0;
    bool legacy_open = false, new_open = false;

    if (nvs_open("taipanminer", NVS_READWRITE, &legacy) == ESP_OK) {
        legacy_open = true;
    }
    if (nvs_open("bb_cfg", NVS_READWRITE, &new_ns) == ESP_OK) {
        new_open = true;
    }

    if (!legacy_open || !new_open) {
        bb_log_w(TAG, "migrate_legacy_bb_keys: could not open NVS handles (legacy=%d new=%d)",
                 legacy_open, new_open);
        goto done;
    }

    /* --- wifi_ssid / wifi_pass (migrate as a pair) --- */
    {
        char existing_ssid[33] = {0};
        size_t len = sizeof(existing_ssid);
        bool bb_cfg_has_ssid = (nvs_get_str(new_ns, "wifi_ssid", existing_ssid, &len) == ESP_OK)
                               && existing_ssid[0];

        if (!bb_cfg_has_ssid) {
            char ssid[33] = {0};
            char pass[65] = {0};
            size_t ssid_len = sizeof(ssid);
            size_t pass_len = sizeof(pass);
            bool got_ssid = (nvs_get_str(legacy, "wifi_ssid", ssid, &ssid_len) == ESP_OK) && ssid[0];
            nvs_get_str(legacy, "wifi_pass", pass, &pass_len);

            if (got_ssid) {
                if (nvs_set_str(new_ns, "wifi_ssid", ssid) == ESP_OK &&
                    nvs_set_str(new_ns, "wifi_pass", pass) == ESP_OK) {
                    nvs_erase_key(legacy, "wifi_ssid");
                    nvs_erase_key(legacy, "wifi_pass");
                    bb_log_i(TAG, "migrate: taipanminer/wifi_ssid → bb_cfg");
                    bb_log_i(TAG, "migrate: taipanminer/wifi_pass → bb_cfg");
                } else {
                    bb_log_w(TAG, "migrate: wifi write to bb_cfg failed");
                }
            }
        }
    }

    /* --- hostname --- */
    {
        char existing[33] = {0};
        size_t len = sizeof(existing);
        bool bb_cfg_has = (nvs_get_str(new_ns, "hostname", existing, &len) == ESP_OK)
                          && existing[0];

        if (!bb_cfg_has) {
            char val[33] = {0};
            size_t vlen = sizeof(val);
            if (nvs_get_str(legacy, "hostname", val, &vlen) == ESP_OK && val[0]) {
                if (nvs_set_str(new_ns, "hostname", val) == ESP_OK) {
                    nvs_erase_key(legacy, "hostname");
                    bb_log_i(TAG, "migrate: taipanminer/hostname → bb_cfg");
                } else {
                    bb_log_w(TAG, "migrate: hostname write to bb_cfg failed");
                }
            }
        }
    }

    /* --- mdns_en (u8; default 1 — only migrate if legacy explicitly disabled) --- */
    {
        uint8_t new_val = 1;
        bool bb_cfg_has = (nvs_get_u8(new_ns, "mdns_en", &new_val) == ESP_OK);
        if (!bb_cfg_has) {
            uint8_t v = 1;
            if (nvs_get_u8(legacy, "mdns_en", &v) == ESP_OK) {
                if (nvs_set_u8(new_ns, "mdns_en", v) == ESP_OK) {
                    nvs_erase_key(legacy, "mdns_en");
                    bb_log_i(TAG, "migrate: taipanminer/mdns_en → bb_cfg");
                } else {
                    bb_log_w(TAG, "migrate: mdns_en write to bb_cfg failed");
                }
            }
        }
    }

    /* --- update_check_en (u8; default 1) --- */
    {
        uint8_t new_val = 1;
        bool bb_cfg_has = (nvs_get_u8(new_ns, "update_check_en", &new_val) == ESP_OK);
        if (!bb_cfg_has) {
            uint8_t v = 1;
            if (nvs_get_u8(legacy, "update_check_en", &v) == ESP_OK) {
                if (nvs_set_u8(new_ns, "update_check_en", v) == ESP_OK) {
                    nvs_erase_key(legacy, "update_check_en");
                    bb_log_i(TAG, "migrate: taipanminer/update_check_en → bb_cfg");
                } else {
                    bb_log_w(TAG, "migrate: update_check_en write to bb_cfg failed");
                }
            }
        }
    }

    /* --- provisioned (u8; absence = not provisioned) --- */
    {
        uint8_t new_val = 0;
        bool bb_cfg_has = (nvs_get_u8(new_ns, "provisioned", &new_val) == ESP_OK);
        if (!bb_cfg_has) {
            uint8_t v = 0;
            if (nvs_get_u8(legacy, "provisioned", &v) == ESP_OK) {
                if (nvs_set_u8(new_ns, "provisioned", v) == ESP_OK) {
                    nvs_erase_key(legacy, "provisioned");
                    bb_log_i(TAG, "migrate: taipanminer/provisioned → bb_cfg");
                } else {
                    bb_log_w(TAG, "migrate: provisioned write to bb_cfg failed");
                }
            }
        }
    }

    /* --- display_en (u8; default 1) --- */
    {
        uint8_t new_val = 1;
        bool bb_cfg_has = (nvs_get_u8(new_ns, "display_en", &new_val) == ESP_OK);
        if (!bb_cfg_has) {
            uint8_t v = 1;
            if (nvs_get_u8(legacy, "display_en", &v) == ESP_OK) {
                if (nvs_set_u8(new_ns, "display_en", v) == ESP_OK) {
                    nvs_erase_key(legacy, "display_en");
                    bb_log_i(TAG, "migrate: taipanminer/display_en → bb_cfg");
                } else {
                    bb_log_w(TAG, "migrate: display_en write to bb_cfg failed");
                }
            }
        }
    }

    nvs_commit(new_ns);
    nvs_commit(legacy);

done:
    if (new_open)    nvs_close(new_ns);
    if (legacy_open) nvs_close(legacy);
}
#endif /* ESP_PLATFORM */

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

    // Pre-migrate BB-owned keys from legacy "taipanminer" namespace into
    // BB's default "bb_cfg" namespace. Must run before bb_nv_config_init
    // (in EARLY tier) so bb_wifi_autoinit sees the migrated wifi creds.
#ifdef ESP_PLATFORM
    migrate_legacy_bb_keys();
#endif

    // Initialize early-tier registry (log_stream, nv_flash, nv_config)
    BB_ERROR_CHECK(bb_registry_init_early());
    BB_ERROR_CHECK(config_init());
    // Register manifest so /api/manifest exposes the NVS keyspace
    BB_ERROR_CHECK(config_register_manifest());
    log_reset_reason();
    BB_ERROR_CHECK(led_init());

    // Boot failure counter — incremented only on WiFi timeout restart (wifi_prov.c),
    // not on every boot, so flash/power-cycle doesn't trigger AP fallback.
    uint8_t boot_cnt = bb_nv_config_boot_count();

    // Register TaipanMiner-specific info extender for breadboard's /api/info endpoint
    BB_ERROR_CHECK(webui_register_info_extender());

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
    BB_ERROR_CHECK(bb_display_init());
    ui_show_splash();
    vTaskDelay(pdMS_TO_TICKS(2000));
#endif
#ifdef TM_BENCH_QUIET
    bb_log_w(TAG, "TM_BENCH_QUIET: display disabled");
#endif

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
            }
            /* Instance name == hostname so dns-sd browses surface a useful identifier. */
            bb_mdns_set_instance_name(hn);
            bb_mdns_set_hostname(hn);
        }
        bool mdns_en = bb_nv_config_mdns_enabled();
        bool knot_en = config_knot_enabled() && mdns_en;  // dependency
        if (mdns_en) {
            bb_mdns_init();
            bb_mdns_set_txt("worker", config_worker_name());
            bb_mdns_set_txt("board", FIRMWARE_BOARD);
            bb_mdns_set_txt("version", bb_system_get_version());
            bb_mdns_set_txt("state", "mining");
        }
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
#endif

        // Initialize registry: walks PRE_HTTP tier (CORS, OpenAPI meta, route-reserve),
        // auto-starts HTTP server (CONFIG_BB_HTTP_AUTOSTART=y), then walks regular tier
        // (auto-registers all breadboard routes and endpoints). Creates the
        // bb_update_check + bb_ota_pull worker tasks using the affinity/priority above.
        BB_ERROR_CHECK(bb_registry_init());

        // Register "block.found" SSE topic and hand the handle to mining_pool_stats
        // so record_block() can post events. Must run after bb_registry_init() so
        // bb_event_routes is already initialized.
        {
            static bb_event_topic_t s_block_topic = NULL;
            BB_ERROR_CHECK(bb_event_topic_register("block.found", &s_block_topic));
            BB_ERROR_CHECK(bb_event_routes_attach_ex("block.found", false));
            mining_pool_stats_set_block_topic(s_block_topic);
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
        bb_ota_pull_set_firmware_board("taipanminer-" FIRMWARE_BOARD);
        bb_ota_pull_set_hooks(mining_pause, mining_resume);
        bb_ota_pull_set_skip_check_cb(bb_nv_config_ota_skip_check);
        bb_ota_pull_set_http_timeout_ms(60000);
        // Register mDNS keys (manifest auto-registered by registry)
        {
            static const bb_manifest_mdns_t taipan_mdns_keys[] = {
                {.key = "worker", .desc = "stratum worker name"},
                {.key = "board", .desc = "firmware board identifier", .values = "tdongle-s3|bitaxe-601|bitaxe-403|bitaxe-650"},
                {.key = "version", .desc = "firmware semver"},
                {.key = "state", .desc = "device lifecycle state", .values = "provisioning|mining|ota"},
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

    // Initialize OTA push with breadboard component
    bb_ota_push_set_hooks(mining_pause, mining_resume);
    bb_ota_push_set_skip_check_cb(bb_nv_config_ota_skip_check);

    // Wire production OTA validator ops before stratum starts
    ota_validator_init(&g_ota_timer_ops_default, &g_ota_mark_valid_ops_default);
#else
    bb_log_w(TAG, "TM_BENCH_QUIET: skipping NTP, OTA-pull, OTA-push, OTA-validator");
#endif

    // Start mining
    start_mining();

#if defined(BOARD_HAS_DISPLAY) && !defined(TM_BENCH_QUIET)
    // Start display status task on Core 0
    xTaskCreatePinnedToCore(display_status_task, "display", 6144, NULL, 2, NULL, 0);
#endif
}
