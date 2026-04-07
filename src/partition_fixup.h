#pragma once

// Check if the on-flash partition table matches our expected layout.
// If not (foreign table from e.g. BLACKBOX firmware), copy our firmware
// to ota_0 (0x20000), rewrite the partition table + otadata, and reboot.
// Called as the FIRST thing in app_main(), before nvs_flash_init().
// On normal boot (table matches): returns immediately (~1μs).
// On fixup: never returns (calls esp_restart()).
void partition_fixup_check(void);
