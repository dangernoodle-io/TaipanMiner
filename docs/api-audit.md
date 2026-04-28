# TaipanMiner /api/* Endpoint Audit

**Branch:** `jae/ta-276-api-audit`  
**Date:** 2026-04-27  
**Backlog anchor:** TA-276 (depends on TA-237, TA-223, TA-246, TA-201, TA-228)

---

## 1. Summary

`/api/stats` has grown to ~30 fields spanning mining telemetry, build identity, pool
configuration, heap diagnostics, per-chip ASIC arrays, and a drop-event ring buffer.
Every field travels every 5-second poll regardless of whether it changes. This audit
catalogues every registered route, maps each response key to its webui consumer, and
flags the fields that belong on a different endpoint or that are missing entirely per
open backlog items. The decisions requested in §8 gate TA-275 (extract route descriptors
to a host-testable seam) and TA-250 (OpenAPI drift check), both of which require a stable
per-endpoint shape contract.

---

## 2. Endpoint Inventory

Routes are registered in `components/taipan_web/src/routes.c` at lines 1192–1241 via
`bb_http_register_described_route` and a set of breadboard helper calls. ASIC-only routes
are gated on `#ifdef ASIC_CHIP` at compile time; tdongle-s3 builds do not include
`/api/power`, `/api/fan`, or the ASIC sections of `/api/stats`.

### 2.1 TM-owned routes (routes.c)

| URI | Method | Handler / Lines | Owner | ASIC-only | Brief purpose |
|-----|--------|-----------------|-------|-----------|---------------|
| `/api/stats` | GET | `stats_handler` (144–379) | TM | partial | Mining telemetry snapshot + build identity + pool config + heap + ASIC chips + drop log |
| `/api/power` | GET | `power_handler` (382–450) | TM | yes | ASIC power rail and temperature telemetry |
| `/api/fan` | GET | `fan_handler` (452–482) | TM | yes | Fan RPM and duty cycle |
| `/api/knot` | GET | `knot_handler` (485–526) | TM | no | mDNS-discovered peer table snapshot |
| `/api/settings` | GET | `settings_get_handler` (559–578) | TM | no | Persisted pool + device config read |
| `/api/settings` | POST | `settings_post_handler` (725–735) | TM | no | Full settings replace |
| `/api/settings` | PATCH | `settings_patch_handler` (741–886) | TM | no | Partial settings update |

### 2.2 Breadboard-owned routes (registered from routes.c at lines 1229–1237)

| URI | Method | Registered via | Brief purpose |
|-----|--------|----------------|---------------|
| `/api/ota/check` | GET | `bb_ota_pull_register_handler` | Check for OTA update; returns `{update_available, latest_version, current_version}` |
| `/api/ota/update` | POST | `bb_ota_pull_register_handler` | Trigger OTA pull download + flash |
| `/api/ota/status` | GET | `bb_ota_pull_register_handler` | OTA progress state machine: `{state, in_progress, progress_pct}` |
| `/api/ota/push` | POST | `bb_ota_push_register_handler` | Firmware binary upload (content-type: application/octet-stream) |
| `/api/ota/mark-valid` | POST | `bb_ota_validator_init` (line 1202) | Mark running slot as valid (clears PENDING_VERIFY; called after mark-valid window) |
| `/api/version` | GET | `bb_http_register_common_routes` | Plain-text firmware version string |
| `/api/ping` | GET | `bb_http_register_common_routes` | Liveness probe: plain `ok <uptime_s>` |
| `/api/reboot` | POST | `bb_http_register_common_routes` | Trigger device reboot |
| `/api/scan` | GET | `bb_http_register_common_routes` | Wi-Fi AP scan result array |
| `/api/info` | GET | `bb_info_register_routes` | Device identity, build info, network state, TM extensions (worker, hostname, validated, wdt_resets, boot_time, stratum status) |
| `/api/wifi` | GET | `bb_wifi_register_routes` | Wi-Fi connection snapshot |
| `/api/board` | GET | `bb_board_register_routes` | Hardware info (board, chip model, MAC, flash size, heap, app size) |
| `/api/logs` | GET (SSE) | `bb_log_stream_register_routes` | Log event stream (Server-Sent Events) |
| `/api/logs/status` | GET | `bb_log_stream_register_routes` | Log stream subscriber status |
| `/api/log/level` | GET | `bb_log_register_routes` | Enumerate component log levels |
| `/api/log/level` | POST | `bb_log_register_routes` | Set per-component log level |

---

## 3. Per-endpoint Detail (TM-owned only)

### 3.1 `/api/stats` — GET

**Lines:** routes.c 144–379 (handler) + 896–936 (schema descriptor `s_stats_responses[]`)  
**Mutex:** `mining_stats.mutex` (taken lines 173–208; 100 ms timeout)

#### 3.1.1 Base fields (always emitted)

| Key | Type | Conditional | Source field | Consumed by |
|-----|------|-------------|--------------|-------------|
| `hashrate` | number | — | `mining_stats.hw_hashrate` | `Hero.svelte` (fallback GH/s), `stores.ts` history |
| `hashrate_avg` | number | — | `mining_stats.hw_ema.value` | `Hero.svelte` (emaGhs fallback) |
| `temp_c` | number | — | `mining_stats.temp_c` | `Dashboard.svelte` Heat card, `Hero.svelte`, `stores.ts` history |
| `shares` | integer | — | `mining_stats.hw_shares` | `api.ts` Stats interface (typed `number \| null`) |
| `pool_difficulty` | number | — | `mining_stats.pool_difficulty` | `PoolStrip.svelte`, `Pool.svelte`, `Hero.svelte` (diffMult) |
| `session_shares` | integer | — | `mining_stats.session.shares` | `Pool.svelte`, `Hero.svelte` (accepted) |
| `session_rejected` | integer | — | `mining_stats.session.rejected` | `Pool.svelte` |
| `rejected` | object | — | see sub-keys below | — |
| `rejected.total` | integer | — | `session.rejected` | — (TA-246: not yet surfaced in UI) |
| `rejected.job_not_found` | integer | — | `session.rejected_job_not_found` | — (TA-246) |
| `rejected.low_difficulty` | integer | — | `session.rejected_low_difficulty` | — (TA-246) |
| `rejected.duplicate` | integer | — | `session.rejected_duplicate` | — (TA-246) |
| `rejected.stale_prevhash` | integer | — | `session.rejected_stale_prevhash` | — (TA-246) |
| `rejected.other` | integer | — | `session.rejected_other` | — (TA-246) |
| `rejected.other_last_code` | integer | — | `session.rejected_other_last_code` | — (TA-246) |
| `last_share_ago_s` | integer \| -1 | -1 if no share | derived from `session.last_share_us` | `Pool.svelte`, `Hero.svelte` |
| `lifetime_shares` | integer | — | `lifetime.total_shares` | `Hero.svelte` |
| `best_diff` | number | — | `session.best_diff` | `Hero.svelte` |
| `pool_host` | string | — | `taipan_config_pool_host()` | `PoolStrip.svelte`, `Pool.svelte` |
| `pool_port` | integer | — | `taipan_config_pool_port()` | `PoolStrip.svelte`, `Pool.svelte` |
| `worker` | string | — | `taipan_config_worker_name()` | `PoolStrip.svelte`, `Pool.svelte` |
| `wallet` | string | — | `taipan_config_wallet_addr()` | `api.ts` Stats interface |
| `uptime_s` | integer | — | derived from `session.start_us` | `Hero.svelte`, `System.svelte`, `stores.ts` sharesPerHour |
| `free_heap` | integer | — | `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` | `System.svelte` Donut (heapUsed) |
| `total_heap` | integer | — | `heap_caps_get_total_size(MALLOC_CAP_INTERNAL)` | `System.svelte` Donut |
| `rssi_dbm` | integer \| null | null if no WiFi | `bb_wifi_get_rssi()` | `System.svelte` (fallback: `$info?.network?.rssi ?? $stats?.rssi_dbm`), `Settings.svelte` (placeholder text) |
| `version` | string | — | `bb_system_get_version()` | `Header.svelte`, `System.svelte` Firmware card |
| `build_date` | string | — | `bb_system_get_build_date()` | `System.svelte` (via `$info?.build_date` first; `$stats` not directly used) |
| `build_time` | string | — | `bb_system_get_build_time()` | `System.svelte` (same as above) |
| `board` | string | — | `BOARD_NAME` macro | `Header.svelte`, `System.svelte` Device card, `LiveTitle.svelte` |
| `display_en` | boolean | — | `bb_nv_config_display_enabled()` | `api.ts` Stats interface (not directly rendered — Settings reads from /api/settings) |
| `expected_ghs` | number \| null | null if `asic_freq_cfg < 0`; `0.000223` on tdongle | derived: `freq_cfg * BOARD_SMALL_CORES * BOARD_ASIC_COUNT / 1000` | `Dashboard.svelte` Performance card |

#### 3.1.2 ASIC-only fields (`#ifdef ASIC_CHIP`)

| Key | Type | Conditional | Source field | Consumed by |
|-----|------|-------------|--------------|-------------|
| `asic_hashrate` | number | ASIC only | `mining_stats.asic_hashrate` | `Hero.svelte` (ghs primary), `stores.ts` history |
| `asic_hashrate_avg` | number | ASIC only | `mining_stats.asic_ema.value` | `Hero.svelte` emaGhs |
| `asic_shares` | integer | ASIC only | `mining_stats.asic_shares` | `api.ts` Stats interface |
| `asic_temp_c` | number | ASIC only | `mining_stats.asic_temp_c` | `Dashboard.svelte` Heat card, `stores.ts` history |
| `asic_freq_configured_mhz` | number \| null | null if `asic_freq_cfg < 0` | `mining_stats.asic_freq_configured_mhz` | `Dashboard.svelte` Performance card |
| `asic_freq_effective_mhz` | number \| null | null if `asic_freq_eff < 0` | `mining_stats.asic_freq_effective_mhz` | `Dashboard.svelte` Performance card, `stores.ts` history |
| `asic_small_cores` | integer | ASIC only | `BOARD_SMALL_CORES` macro | `Dashboard.svelte` Performance card |
| `asic_count` | integer | ASIC only | `BOARD_ASIC_COUNT` macro | `Dashboard.svelte` Performance card |
| `asic_total_ghs` | number \| null | null if `asic_total_valid == false` | `mining_stats.asic_total_ghs` | `Dashboard.svelte`, `Hero.svelte`, `stores.ts` history, `LiveTitle.svelte` |
| `asic_hw_error_pct` | number \| null | null if `asic_total_valid == false` | `mining_stats.asic_hw_error_pct` | `stores.ts` history |
| `asic_total_ghs_1m` | number \| null | null if `asic_total_valid == false` | `mining_stats.asic_total_ghs_1m` | `api.ts` Stats interface (no direct page render found) |
| `asic_total_ghs_10m` | number \| null | null if `asic_total_valid == false` | `mining_stats.asic_total_ghs_10m` | `api.ts` Stats interface (no direct page render found) |
| `asic_total_ghs_1h` | number \| null | null if `asic_total_valid == false` | `mining_stats.asic_total_ghs_1h` | `api.ts` Stats interface (no direct page render found) |
| `asic_hw_error_pct_1m` | number \| null | null if `asic_total_valid == false` | `mining_stats.asic_hw_error_pct_1m` | `api.ts` Stats interface (no direct page render found) |
| `asic_hw_error_pct_10m` | number \| null | null if `asic_total_valid == false` | `mining_stats.asic_hw_error_pct_10m` | `api.ts` Stats interface (no direct page render found) |
| `asic_hw_error_pct_1h` | number \| null | null if `asic_total_valid == false` | `mining_stats.asic_hw_error_pct_1h` | `api.ts` Stats interface (no direct page render found) |

#### 3.1.3 `asic_chips[]` array (`#ifdef ASIC_CHIP`, lines 308–347)

Array of `BOARD_ASIC_COUNT` objects. Each object:

| Key | Type | Conditional | Source field | Consumed by |
|-----|------|-------------|--------------|-------------|
| `idx` | integer | — | loop index `c` | `ChipsCard.svelte` |
| `total_ghs` | number | — | `chip_tel[c].total_ghs` | `ChipsCard.svelte`, `Dashboard.svelte` (expectedPerDomain) |
| `error_ghs` | number | — | `chip_tel[c].error_ghs` | `api.ts` Chip interface (not rendered) |
| `hw_err_pct` | number | — | `chip_tel[c].hw_err_pct` | `ChipsCard.svelte` |
| `total_raw` | integer | — | `chip_tel[c].total_raw` | `api.ts` Chip interface (not rendered) |
| `error_raw` | integer | — | `chip_tel[c].error_raw` | `api.ts` Chip interface (not rendered) |
| `total_drops` | integer | — | `chip_tel[c].total_drops` | `ChipsCard.svelte` (annotation badge) |
| `error_drops` | integer | — | `chip_tel[c].error_drops` | `api.ts` Chip interface (not rendered) |
| `last_drop_ago_s` | number \| null | null if never dropped | derived from `chip_tel[c].last_drop_us` | `ChipsCard.svelte` (corrupt badge via `getChipState`) |
| `domain_ghs[4]` | number[] | — | `chip_tel[c].domain_ghs[]` | `ChipsCard.svelte` domain heatmap |
| `domain_drops[4]` | number[] | — | `chip_tel[c].domain_drops[]` | `api.ts` Chip interface (not rendered) |

#### 3.1.4 `recent_drops[]` array (`#ifdef ASIC_CHIP`, lines 349–370)

Ring of up to `ASIC_DROP_LOG_CAP` events. Each object:

| Key | Type | Consumed by |
|-----|------|-------------|
| `ts_ago_s` | number | `api.ts` RecentDrop interface (not rendered in any page) |
| `chip` | integer | same |
| `kind` | string (`total`/`error`/`domain`) | same |
| `domain` | integer | same |
| `addr` | integer | same |
| `ghs` | number | same |
| `delta` | number | same |
| `elapsed_s` | number | same |

#### 3.1.5 Schema descriptor gap

`s_stats_responses[]` (lines 896–936) documents only the base 25 fields. The ASIC chip
fields, `asic_chips[]`, and `recent_drops[]` arrays are **absent** from the descriptor.
The descriptor also omits `display_en` from the required list. This is the primary
symptom TA-275 will fix: the live response diverges from the declared schema.

---

### 3.2 `/api/power` — GET (ASIC only)

**Lines:** routes.c 382–450 (handler) + 1077–1102 (descriptor `s_power_responses[]`)  
**Mutex:** `mining_stats.mutex` (100 ms timeout, lines 390–399)

| Key | Type | Conditional | Source field | Consumed by |
|-----|------|-------------|--------------|-------------|
| `vcore_mv` | integer \| null | null if `vcore_mv < 0` | `mining_stats.vcore_mv` | `Dashboard.svelte` Power card |
| `icore_ma` | integer \| null | null if `icore_ma < 0` | `mining_stats.icore_ma` | `Dashboard.svelte` Power card |
| `pcore_mw` | integer \| null | null if `pcore_mw < 0` | `mining_stats.pcore_mw` | `Dashboard.svelte` Power card, `stores.ts` history |
| `efficiency_jth` | number \| null | null until power and hashrate both available | derived from `pcore_mw` and `asic_hashrate` | `Dashboard.svelte` Power card, `stores.ts` history |
| `vin_mv` | integer \| null | null if `vin_mv < 0` | `mining_stats.vin_mv` | `Dashboard.svelte` Power card |
| `vin_low` | boolean \| null | null if `vin_mv < 0` | derived from `vin_mv` vs `BOARD_NOMINAL_VIN_MV` | `api.ts` Power interface (not rendered — no UI alert found) |
| `board_temp_c` | number \| null | null if `board_temp_c < 0` | `mining_stats.board_temp_c` | `Dashboard.svelte` Heat card, `stores.ts` history |
| `vr_temp_c` | number \| null | null if `vr_temp_c < 0` | `mining_stats.vr_temp_c` | `Dashboard.svelte` Heat card, `stores.ts` history |

Schema descriptor matches the live response. No gaps.

---

### 3.3 `/api/fan` — GET (ASIC only)

**Lines:** routes.c 452–482 (handler) + 1108–1127 (descriptor `s_fan_responses[]`)  
**Mutex:** `mining_stats.mutex` (100 ms timeout, lines 458–462)

| Key | Type | Conditional | Source field | Consumed by |
|-----|------|-------------|--------------|-------------|
| `rpm` | integer \| null | null if `fan_rpm < 0` | `mining_stats.fan_rpm` | `Dashboard.svelte` Fan card, `stores.ts` history |
| `duty_pct` | integer \| null | null until first telemetry tick | `mining_stats.fan_duty_pct` | `Dashboard.svelte` Fan card (bar + tile), `stores.ts` history |

Schema descriptor matches. No gaps.

---

### 3.4 `/api/settings` — GET

**Lines:** routes.c 559–578 (handler) + 986–1012 (descriptor `s_settings_get_responses[]`)

| Key | Type | Source | Consumed by |
|-----|------|--------|-------------|
| `pool_host` | string | `taipan_config_pool_host()` | `Pool.svelte` |
| `pool_port` | integer | `taipan_config_pool_port()` | `Pool.svelte` |
| `wallet` | string | `taipan_config_wallet_addr()` | `Pool.svelte` |
| `worker` | string | `taipan_config_worker_name()` | `Pool.svelte` |
| `pool_pass` | string | `taipan_config_pool_pass()` | `Pool.svelte` |
| `hostname` | string | `taipan_config_hostname()` | Settings page (via `patchSettings`) |
| `display_en` | boolean | `bb_nv_config_display_enabled()` | `Settings.svelte` |
| `ota_skip_check` | boolean | `bb_nv_config_ota_skip_check()` | `Settings.svelte` |

Schema descriptor requires all 8 fields. Matches live handler. No gaps.

---

### 3.5 `/api/settings` — POST

**Lines:** routes.c 725–735 (handler, delegates to `apply_settings`), `apply_settings` 582–723  
**Mutex:** none  
**Body:** `application/json`

| Field | Required | Type | Validation | Notes |
|-------|----------|------|------------|-------|
| `pool_host` | yes | string | non-empty | — |
| `pool_port` | yes | integer | 1–65535 | — |
| `wallet` | yes | string | non-empty | — |
| `worker` | yes | string | non-empty | — |
| `pool_pass` | no | string | none | optional even for POST |
| `display_en` | no | boolean | none | applied immediately, no reboot |
| `ota_skip_check` | no | boolean | none | applied immediately, no reboot |

Note: POST does **not** accept `hostname` (see PATCH below). The `s_settings_post_route`
request schema omits `hostname`; the handler omits it too. This is intentional but
undocumented — pool changes trigger reboot, hostname change is a separate concern.

Response: `{"status":"saved","reboot_required":<bool>}` + 400/500 text on error.

---

### 3.6 `/api/settings` — PATCH

**Lines:** routes.c 741–886 (handler), 1049–1069 (descriptor `s_settings_patch_route`)  
**Mutex:** none  
**Body:** `application/json`, all fields optional

| Field | Type | Notes |
|-------|------|-------|
| `pool_host` | string | defaults to current value if absent |
| `pool_port` | integer | 1–65535; defaults to current if absent |
| `wallet` | string | defaults to current if absent |
| `worker` | string | defaults to current if absent |
| `pool_pass` | string | defaults to current if absent |
| `hostname` | string | validated via `taipan_config_set_hostname()`; triggers reboot |
| `display_en` | boolean | immediate effect |
| `ota_skip_check` | boolean | immediate effect |

**Known code issue:** when `reboot_required` is true and `hostname_changed` is also true,
the handler calls `taipan_config_set_pool()` in both branches of the if/else (lines 831
and 838) with identical bodies. This is dead-code duplication, not a bug, but worth
cleaning up in a separate refactor PR.

Response: `{"status":"saved","reboot_required":<bool>}` + 400/500 text on error.

---

### 3.7 `/api/knot` — GET

**Lines:** routes.c 485–526 (handler) + 952–980 (descriptor `s_knot_responses[]`)  
**Mutex:** none (`knot_snapshot` is internally synchronized)

Response: JSON array of peer objects. Each peer:

| Key | Type | Source |
|-----|------|--------|
| `instance` | string | mDNS instance name |
| `hostname` | string | mDNS hostname |
| `ip` | string | IPv4 address |
| `worker` | string | mDNS TXT record |
| `board` | string | mDNS TXT record |
| `version` | string | mDNS TXT record |
| `state` | string | mDNS TXT record |
| `seen_ago_s` | integer | derived from `last_seen_us` |

Consumed by `Knot.svelte`. Schema descriptor matches. No gaps.

---

## 4. Issues

| # | Endpoint | Field | Issue | Source backlog |
|---|----------|-------|-------|----------------|
| I-1 | `/api/stats` | `version` | WRONG\_PLACE: identical value already in `/api/info.version`; `/api/info` is loaded once and cached. Polled every 5s needlessly. | TA-276 |
| I-2 | `/api/stats` | `build_date` | WRONG\_PLACE: `System.svelte` prefers `$info?.build_date` — stats value is shadow read only if info unavailable. Polled every 5s. | TA-276 |
| I-3 | `/api/stats` | `build_time` | WRONG\_PLACE: same as I-2. | TA-276 |
| I-4 | `/api/stats` | `board` | WRONG\_PLACE: also in `/api/info.board` and `/api/board.board`. `Header.svelte` reads from stats; `System.svelte` reads from `/api/info`. Two sources for same datum. | TA-276 |
| I-5 | `/api/stats` | `pool_host`, `pool_port`, `wallet`, `worker` | WRONG\_PLACE: pool config is static between reboots. Already in `/api/settings`. `Pool.svelte` reads live pool state from `/api/stats` for the "Active" card; settings form reads `/api/settings`. Duplication is intentional today but muddles the stats contract. | TA-276 |
| I-6 | `/api/stats` | `display_en` | WRONG\_PLACE: a device preference, not a mining stat. Already in `/api/settings`. No webui component renders it from stats. | TA-276 |
| I-7 | `/api/stats` | `free_heap`, `total_heap` | WRONG\_PLACE: system diagnostics. `System.svelte` reads them from stats for the RAM Donut; `/api/board` also carries heap fields. Polled every 5s. | TA-276 |
| I-8 | `/api/stats` | `rssi_dbm` | WRONG\_PLACE: `System.svelte` prefers `$info?.network?.rssi`; stats is the fallback. A network field should live under `/api/wifi` (breadboard already provides it). | TA-276 |
| I-9 | `/api/stats` | `asic_total_ghs_1m`, `asic_total_ghs_10m`, `asic_total_ghs_1h`, `asic_hw_error_pct_1m`, `asic_hw_error_pct_10m`, `asic_hw_error_pct_1h` | UNCONSUMED: declared in `api.ts` Stats interface but no webui page or component renders these 6 rolling-window fields. No history chart uses them (history is client-side at 5s). | TA-276 (analysis) |
| I-10 | `/api/stats` | `asic_chips[].error_ghs`, `asic_chips[].total_raw`, `asic_chips[].error_raw`, `asic_chips[].error_drops`, `asic_chips[].domain_drops[]` | UNCONSUMED: in `api.ts` Chip interface but no page renders them. Sent on every 5s poll. | TA-276 (analysis) |
| I-11 | `/api/stats` | `recent_drops[]` full array | UNCONSUMED: `api.ts` declares RecentDrop interface but no page or component renders this array. 8 fields × up to N events travel every poll. | TA-276 (analysis) |
| I-12 | `/api/stats` | `asic_chips[]`, `recent_drops[]` | WRONG\_PLACE: per-chip diagnostic telemetry and forensic drop log are not "stats" in the same polling sense as hashrate. A candidate `/api/asic` or `/api/diag/asic` endpoint would let the UI fetch on demand rather than every 5s. | TA-276 |
| I-13 | `/api/stats` | `s_stats_responses[]` schema | MISSING (descriptor gap): `asic_chips[]`, `recent_drops[]`, and all ASIC top-level fields absent from the OpenAPI descriptor (lines 896–936). TA-275 seam extraction cannot validate these fields. | TA-276, TA-275 (dependency) |
| I-14 | `/api/stats` | `rejected.*` sub-object | UNCONSUMED (UI only): shape is correct post-TA-244. `api.ts` has the full `rejected` object but no page renders the breakdown. `Pool.svelte` shows only `session_rejected`. Surface in UI per TA-246. | TA-246 |
| I-15 | `/api/stats` | `last_drop_ago_s` per chip | PRESENT (already landed): `chip_tel[c].last_drop_us` → `last_drop_ago_s` per chip implemented (routes.c 325–331). `ChipsCard.svelte` consumes it. TA-237 firmware side is done; verify `getChipState` logic applies the 300s threshold correctly. | TA-237 |
| I-16 | `/api/stats` | `total_drops` per chip | PRESENT (already landed): `chip_tel[c].total_drops` emitted (routes.c 322). `ChipsCard.svelte` shows annotation badge. TA-223 drop counter is in place. | TA-223 |
| I-17 | `/api/stats` | `wallet` | UNCONSUMED (UI): in `api.ts` Stats interface but no page renders `$stats.wallet`. `Pool.svelte` reads wallet from `/api/settings`, not stats. | TA-276 (analysis) |
| I-18 | `/api/power` | `vin_low` | UNCONSUMED: computed and transmitted but no page renders or alerts on `vin_low`. | TA-276 (analysis) |
| I-19 | n/a | `/api/stratum` | MISSING: pool page currently reads `pool_host`, `pool_port`, `worker` from `/api/stats` for the "Active" card. A dedicated `/api/stratum` endpoint (TA-201) would expose live stratum connection state (prev block hash, extranonce, versionmask, etc.) without polluting stats. | TA-201 |

---

## 5. Target-Shape Recommendations

### 5.1 Split build-identity fields out of `/api/stats` (TA-276)

Fields `version`, `build_date`, `build_time`, `board` are static across a session.
`/api/info` already provides all four (`Info` interface in `api.ts` lines 86–108);
`/api/board` also carries them. `System.svelte` already prefers `$info` — `$stats.version`
and `$stats.board` are only used in `Header.svelte` and `LiveTitle.svelte` as a fast path
before `info` loads.

**Recommendation — Option A (preferred):** Remove `version`, `build_date`, `build_time`,
`board` from `/api/stats`. Update `Header.svelte` and `LiveTitle.svelte` to source from
`$info` (already loaded once-on-mount). `/api/info` becomes the single source of truth.

**Option B (migration-safe):** Keep them in stats for one release while UI migrates, then
drop in a follow-up PR with a bumped minor version. Requires a deprecation note in the
schema descriptor.

Decision required: see §8 Q1.

---

### 5.2 Split pool-config fields out of `/api/stats` (TA-276)

Fields `pool_host`, `pool_port`, `worker`, `wallet` are volatile only on reboot.
`Pool.svelte` currently reads `pool_host`, `pool_port`, `worker` from `$stats` for the
live "Active" card (connection-layer values that may differ from saved config if a change
is pending reboot). `wallet` is read from stats but not rendered anywhere.

**Recommendation:** Introduce `/api/pool` returning live stratum connection state:
`{pool_host, pool_port, worker, diff, session_shares, session_rejected, last_share_ago_s}`.
This is a subset of today's stats focused on the pool relationship. Remove the raw config
fields (`wallet` in particular) from `/api/stats`.

Note: this partially overlaps with TA-201 (`/api/stratum`). Coordinate: `/api/pool` =
session-level pool stats; `/api/stratum` = protocol-level job snapshot. They can
coexist, or `/api/stratum` can subsume `/api/pool`.

Decision required: see §8 Q2.

---

### 5.3 Split system/diagnostic fields out of `/api/stats` (TA-276)

Fields `free_heap`, `total_heap`, `rssi_dbm`, `display_en` belong to system/device
state, not mining telemetry. `rssi_dbm` is already on `/api/wifi` (breadboard). `free_heap`/
`total_heap` are already on `/api/board`.

**Recommendation:** Remove `free_heap`, `total_heap`, `rssi_dbm`, `display_en` from
`/api/stats`. `System.svelte` already prefers `$info` network rssi; heap is already on
`/api/board`. Settings reads `display_en` from `/api/settings`. No UI loses data.

Decision required: see §8 Q3.

---

### 5.4 Move ASIC chip telemetry and drop log to `/api/diag/asic` (TA-276)

`asic_chips[]` and `recent_drops[]` are diagnostic arrays polled every 5s but rendered
only when the Chips card is visible and no page currently renders `recent_drops` at all.
Moving them to a separate endpoint (`/api/diag/asic`) allows:

- On-demand fetch (e.g. only when Chips card is in view, or on a slower poll)
- `recent_drops` can be exposed without growing every stats payload
- Stats schema becomes simple enough for TA-275's host-testable seam

The 6 rolling-window fields (`asic_total_ghs_1m` etc.) are also unconsumed by any UI
component and should move to `/api/diag/asic` or be dropped until a History page
time-series panel is built.

Decision required: see §8 Q4 and Q5.

---

### 5.5 TA-237 — `last_drop_ago_s` per chip

**Status: implemented.** `routes.c` lines 325–331 compute `last_drop_ago_s` from
`chip_tel[c].last_drop_us` and emit null if never dropped. `ChipsCard.svelte` consumes
it via `getChipState`. No firmware change required. The UI threshold of 300s lives in
`lib/chipState.ts` (or equivalent). Verify `getChipState` applies `last_drop_ago_s < 300`
and that the annotation "N drops" (total_drops) does not independently trigger the red
`corrupt` state.

---

### 5.6 TA-223 — per-chip drop counters

**Status: implemented.** `total_drops`, `error_drops`, `domain_drops[4]` are all present
in `asic_chip_telemetry_t` (mining.h lines 216–219) and emitted by `stats_handler` (routes.c
lines 322–343). `ChipsCard.svelte` renders `total_drops` as an amber annotation. The
firmware side of TA-223 is done; remaining work is UI refinement (the original backlog
item asked for an amber state, not red, when drops > 0 but chip is hashing — verify
`getChipState` implements that correctly).

---

### 5.7 TA-276 — candidate destination endpoints

After split, `/api/stats` would carry only:

```
hashrate, hashrate_avg, temp_c, shares,
pool_difficulty, session_shares, session_rejected, rejected{},
last_share_ago_s, lifetime_shares, best_diff, uptime_s,
expected_ghs,
[ASIC]: asic_hashrate, asic_hashrate_avg, asic_shares, asic_temp_c,
         asic_freq_configured_mhz, asic_freq_effective_mhz,
         asic_small_cores, asic_count, asic_total_ghs, asic_hw_error_pct
```

Proposed destination endpoints:

| New endpoint | Fields moved there |
|--------------|--------------------|
| `/api/build` | `version`, `build_date`, `build_time`, `board` |
| `/api/pool` | `pool_host`, `pool_port`, `worker`, `wallet` (active connection snapshot) |
| `/api/diag/asic` | `asic_chips[]`, `recent_drops[]`, `asic_total_ghs_1m/10m/1h`, `asic_hw_error_pct_1m/10m/1h` |
| remove from stats | `free_heap`, `total_heap`, `rssi_dbm`, `display_en` (already on other endpoints) |

---

### 5.8 TA-201 — `/api/stratum` (future work)

Do not design the `/api/stratum` shape here. Flag: a future endpoint would carry live
stratum protocol state: `{connected, pool_host, pool_port, worker, extranonce1,
extranonce2_size, version_mask, prev_hash, nbits, ntime, job_id}`. This data is parsed
in `components/stratum` but not yet exposed via HTTP. Required before the Pool/Block
Header UI page (noted as pending in Pool.svelte comments) can render anything interesting.
The stratum mutex must be held when capturing the snapshot. See TA-201 for the full
field list.

---

### 5.9 TA-246 — `rejected` shape confirmation

The `rejected` sub-object shape (total + 5 reason codes + `other_last_code`) is correct
as of TA-244. No firmware change required. The outstanding work is UI-only: surface the
breakdown in `Pool.svelte` (see TA-246 for display options). Flag as UI-only follow-up.

---

### 5.10 TA-228 — SSE backoff (out of scope)

Fixed 3s retry in `Diagnostics.svelte` is a transport concern (`/api/logs` SSE). Not a
shape issue; tracked separately in TA-228. Out of scope for this audit.

---

### 5.11 Unconsumed fields — recommendations

| Field | Recommendation |
|-------|---------------|
| `vin_low` | ADD alert: show warning badge on Dashboard when true; or REMOVE if threshold is device-specific and should stay firmware-internal |
| `error_ghs`, `total_raw`, `error_raw`, `error_drops`, `domain_drops[]` per chip | MOVE to `/api/diag/asic`; keep out of 5s stats payload |
| `recent_drops[]` | MOVE to `/api/diag/asic`; fetch on-demand only |
| `asic_total_ghs_1m/10m/1h`, `asic_hw_error_pct_1m/10m/1h` | MOVE to `/api/diag/asic`; no UI consumer today |
| `wallet` (in stats) | REMOVE from stats; already in `/api/settings` |

---

## 6. Schema Lock-In Note

Once the decisions in §5 are agreed, the resulting shapes become the binding contract for
TA-275 (extract `s_*_responses[]` descriptors to a host-testable JSON seam); any future
field addition or removal must update **both** the handler body **and** the corresponding
`s_*_responses[]` descriptor in the same commit so the OpenAPI schema never drifts from
the live response.

---

## 7. Out of Scope

- Implementation of any shape change (no code in this PR)
- TA-228 SSE reconnect backoff — transport concern, not shape
- Breadboard-owned route redesign (`/api/version`, `/api/info`, `/api/wifi`, `/api/board`, OTA endpoints)
- `/api/stratum` detailed design — noted as future work in §5.8
- `bb_nv_config` field additions (e.g. frequency tuning, fan curve overrides)

---

## 8. Decision Queue

The following questions must be answered before TA-275 (seam extraction) can proceed.
Each yes/no gates a specific set of field moves.

1. **Split build identity:** Move `version`, `build_date`, `build_time`, `board` from
   `/api/stats` to `/api/info` as sole source, removing them from stats? *(yes/no — and
   if yes, migration window: immediate or one-release deprecation?)*

2. **Split pool config:** Move `pool_host`, `pool_port`, `worker`, `wallet` from
   `/api/stats` to a new `/api/pool` endpoint returning live connection-layer pool state?
   *(yes/no — and if yes, does `/api/pool` subsume `/api/stratum` (TA-201) or are they
   separate?)*

3. **Remove system fields from stats:** Remove `free_heap`, `total_heap`, `rssi_dbm`,
   `display_en` from `/api/stats` (all available on `/api/board`, `/api/wifi`,
   `/api/settings`)? *(yes/no)*

4. **Move chip arrays to `/api/diag/asic`:** Move `asic_chips[]` and `recent_drops[]`
   out of `/api/stats` to a new `/api/diag/asic` endpoint fetched on-demand? *(yes/no —
   if yes, does `/api/diag/asic` also absorb the 6 rolling-window fields?)*

5. **Drop or keep rolling-window hashrate fields:** `asic_total_ghs_1m/10m/1h` and
   `asic_hw_error_pct_1m/10m/1h` have no current webui consumer. Keep (move to
   `/api/diag/asic`) or remove? *(keep/remove)*

6. **`vin_low` alert:** Should `vin_low` (currently computed but never rendered) drive a
   UI warning badge on Dashboard, or be removed from the API? *(surface/remove)*

7. **`wallet` in stats:** Remove `wallet` from `/api/stats` (only in the `api.ts` Stats
   interface; no page renders it from stats; `/api/settings` is the authoritative source)?
   *(yes/no)*

8. **TA-237 verify:** Confirm `getChipState(chip)` in `lib/chipState.ts` uses
   `last_drop_ago_s < 300` (not `total_drops > 0`) as the `corrupt` state condition, and
   that `total_drops > 0` maps to the amber annotation only. *(confirm or flag as
   remaining UI work)*
