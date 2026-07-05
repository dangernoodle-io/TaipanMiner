// TaipanMiner v2 — PR 1 skeleton entry point.
//
// Handwired composition root (bb's "autowire"/bb_init walker is DELETED —
// see breadboard CLAUDE.md). This replicates bb's EARLY tier by hand, in the
// order given by the `// bbtool:init tier=early` markers in the vendored bb
// headers (bb_log.h, bb_system.h).
//
// True floor: boots, brings up logging, prints a boot banner + reset
// reason, then idles. No NVS/WiFi/mining/stratum/ASIC/display/LED/webui in
// this skeleton — those land as later PRs.
//
// NVS/WiFi are deliberately absent here, not merely deferred: NVS
// partition bring-up (nvs_flash_init) currently lives only inside
// bb_nv_flash_init(), and bb_nv is dissolving (bb_storage + bb_storage_nvs +
// bb_config). Calling bb_nv_* here would be a new dependency on a
// component mid-dissolution; calling nvs_flash_init() directly would be a
// raw ESP-IDF call bypassing the bb abstraction. Both rejoin once NVS
// bring-up lands in bb_storage_nvs.
#include "bb_log.h"
#include "bb_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "taipanminer";

static void log_reset_reason(void)
{
    const char *reason_str = bb_system_reset_reason_str(bb_system_get_reset_reason());
    bb_log_i(TAG, "reset reason: %s", reason_str);

    if (bb_system_is_abnormal_reset()) {
        bb_log_w(TAG, "abnormal reset detected (%s)", reason_str);
    }
}

void app_main(void)
{
    bb_err_t err;

    // log_stream provides the console writer task + ring buffer; log_config
    // applies Kconfig-driven default/per-tag levels. Both EARLY, in this
    // order (log_config requires=log_stream).
    err = bb_log_stream_init();
    if (err != BB_OK) {
        // Nothing to log to yet if this failed — fall through regardless.
    }

    err = bb_log_config_init();
    if (err != BB_OK) {
        bb_log_w(TAG, "bb_log_config_init failed (%d)", (int)err);
    }

    // Framework log-noise tags, inert until WiFi returns (blocked on
    // breadboard NVS/storage bring-up) — kept here so the boot-order
    // contract is in place before that PR lands.
    bb_log_level_set("wifi", BB_LOG_LEVEL_WARN);
    bb_log_level_set("wifi_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("net80211", BB_LOG_LEVEL_WARN);
    bb_log_level_set("pp", BB_LOG_LEVEL_WARN);
    bb_log_level_set("phy_init", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_handlers", BB_LOG_LEVEL_WARN);
    bb_log_level_set("esp_netif_lwip", BB_LOG_LEVEL_WARN);

    bb_log_tag_register("taipanminer", BB_LOG_LEVEL_INFO);

    // Boot banner (project/version/build/IDF identity) via bb_system's own
    // EARLY hook — formats and logs internally.
    err = bb_system_boot_banner_init();
    if (err != BB_OK) {
        bb_log_w(TAG, "bb_system_boot_banner_init failed (%d)", (int)err);
    }

    log_reset_reason();

    // --- PRODUCER REGISTRATION SLOT (empty; comment marker only) ---

    // --- MINING/STRATUM SLOT (empty; comment marker only) ---

    // --- NVS/WIFI SLOT: blocked on breadboard B1-840 (nvs_flash_init -> bb_storage_nvs) ---

    bb_log_i(TAG, "boot ok, idle");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
