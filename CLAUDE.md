# TaipanMiner

Bitcoin mining firmware for ESP32-S3 boards with optional ASIC support.

## Build

- Framework: ESP-IDF via PlatformIO
- `make help` — show all targets
- `make webui` — build web UI (Svelte SPA) into `webui/dist/`
- `make build` — build all boards (tdongle-s3, bitaxe-601, bitaxe-403)
- `make build-<env>` — build specific board (e.g. `make build-bitaxe-403`)
- `make flash-<env>` — flash specific board
- `make test` — host unit tests
- `make check` — static analysis (cppcheck)
- `make coverage` — test + gcovr coverage report
- `make monitor` — serial monitor
- `make compile-db` — generate `compile_commands.json` for all boards (clangd LSP prerequisite; no linking, cheap)
- Debug: `make build-tdongle-s3-debug` or `make build-bitaxe-601-debug` (adds `TAIPANMINER_DEBUG=1`)

### Python compatibility

ESP-IDF's pydantic-core dependency requires Python <= 3.13. If your system Python is 3.14+, create the ESP-IDF venv manually with Python 3.13 before building:

```bash
python3.13 -m venv ~/.platformio/penv/.espidf-5.5.3
~/.platformio/penv/.espidf-5.5.3/bin/pip install -r ~/.platformio/packages/framework-espidf/tools/requirements/requirements.core.txt
```

Then create `~/.platformio/penv/.espidf-5.5.3/pio-idf-venv.json` with the correct version info to prevent PlatformIO from overwriting the venv.

### Editor / LSP setup

For clangd-based C/C++ IntelliSense (via the `espidf-clangd-lsp` Claude Code plugin):

1. Install the plugin: `/plugin install espidf-clangd-lsp@dangernoodle-marketplace`.
2. Build any board: `make build-bitaxe-601` (or any env). The plugin's Stop-hook generates `.pio/build/<env>/compile_commands.json` and symlinks it to `compile_commands.json` at the project root. Restart clangd.
3. To switch active board: build a different env, run `/set-active-board <env>`, or `make lsp-<env>` (e.g. `make lsp-tdongle-s3`).
4. `.clangd` (committed) strips Xtensa-only GCC flags that confuse clang.
5. Cross-board correctness still requires `make build` / CI — clangd only sees one board at a time.

## Boards

| Env | Board | ASIC | Console |
|-----|-------|------|---------|
| `tdongle-s3` | LilyGo T-Dongle S3 | none (SW mining) | USB CDC |
| `bitaxe-601` | Bitaxe 601 Gamma | BM1370 | USB CDC |
| `bitaxe-403` | Bitaxe 403 | BM1368 | USB CDC |
| `bitaxe-650` | Bitaxe Gamma Duo | 2x BM1370 | USB CDC |

### Platform policy

**Exposed serial is a hard prerequisite.** Do not add support for boards without accessible UART or USB-CDC pins. A soft-lock on a bad OTA is unrecoverable without serial, and vendor-preflashed units with `SECURE_DOWNLOAD_MODE` burned in make serial itself useless — compounding the risk. The BITDSK N8-T attempt (see `ouroboros` KB) confirmed this the hard way.

### Adding a new board

1. **Board header** — create `components/board/include/boards/<board>.h` with pin definitions
2. **Board dispatch** — add `#elif defined(BOARD_<NAME>)` to `components/board/include/board.h`
3. **PlatformIO env** — add `[env:<board>]` and `[env:<board>-debug]` to `platformio.ini` with `-DBOARD_<NAME>` (and `-DASIC_<CHIP>` if applicable) in `build_flags`; set `board_build.cmake_extra_args = -DFIRMWARE_BOARD=<board>` for OTA project name
4. **sdkconfig delta** — create `sdkconfig/<board>` with settings that differ from `sdkconfig.defaults` (e.g. console type, UART ISR); add `sdkconfig/<board>-debug` for debug overlay
5. **Gitignore** — add `sdkconfig.<board>` and `sdkconfig.<board>-debug` to `.gitignore` (ESP-IDF auto-generates these at the project root)
6. **CI/release** — add the env name to the matrix arrays in `ci.yml` and `release.yml`
7. **Miner config** — define `g_miner_config` in the appropriate source file; if the board uses a novel hash engine, implement a `hash_backend_t`
8. **default_envs** — add the env to `default_envs` in `platformio.ini`
9. **LSP** — add a `pio run -t compiledb -e <env>` line to the `compile-db` target in `Makefile`

## Project layout

- `src/` — app entry point, version
- `components/` — ESP-IDF components:
  - Local: `mining`, `stratum`, `board`, `asic`, `display`, `led`, `ota_validator`, `taipan_config`, `webui`
  - From breadboard: `log_stream`, `nv_config`, `ota_pull`, `ota_push`, `http_server`, `bb_wifi`, `bb_prov`, `bb_mdns`
- `components/board/include/boards/` — per-board pin/peripheral headers
- `sdkconfig/` — hand-authored sdkconfig deltas per board
- `test/test_host/` — host-based unit tests (run without hardware via native env)
- `test/test_device/` — on-device integration tests

## Breadboard dependency

TaipanMiner consumes shared infrastructure components from the breadboard library:

- **Pattern**: `EXTRA_COMPONENT_DIRS += $(BREADBOARD_COMPONENTS_DIR)` in CMakeLists.txt includes breadboard components in the build.
- **Namespace isolation**: `BB_NV_CONFIG_NAMESPACE="taipanminer"` (compile-define) preserves NVS key compatibility with the old TaipanMiner-local NVS layout.
- **Components consumed**:
  - `log_stream` — logging middleware
  - `nv_config` — NVS configuration layer (TaipanMiner wraps via `taipan_config`)
  - `ota_pull` — remote OTA fetch + signature validation
  - `ota_push` — HTTP upload OTA receiver
  - `http_server` — HTTP server base with standard URI registration
  - `bb_wifi` — WiFi STA/AP, reconnect logic
  - `bb_prov` — provisioning state machine, event distribution
  - `bb_mdns` — mDNS advertisement
- **Display** remains local (three panel drivers; raw-bitmap API incompatible with breadboard's LVGL design).

## Hardware

- All boards use ESP32-S3 dual-core @ 240MHz
- Pin assignments in `components/board/include/boards/<board>.h`
- Board dispatch via `components/board/include/board.h` (`BOARD_*` defines)

## Architecture

- Core 0: WiFi, Stratum, HTTP server, display, LED
- Core 1: Mining (HW SHA on dongle, ASIC on bitaxe) — priority 20
- Inter-core: FreeRTOS queues (work_queue, result_queue) + mutex (mining_stats)

### Miner dispatch

- `miner_config_t` in `mining.h`: unified config struct (task function, stack, priority, core, extranonce2 roll)
- `g_miner_config`: board-specific instance, defined in `mining.c` (dongle) or `asic_task.c` (bitaxe)
- `main.c` dispatches via `g_miner_config` — no `#ifdef ASIC_BM1370` in task creation

### Mining pipeline (dongle — ESP32-S3 HW SHA)

- `hash_backend_t`: function pointer struct for hash operations (init, prepare_job, hash_nonce)
- HW backend: Phase 3 zero-bswap pipeline via `sha256_hw_mine_nonce` (force-inlined MMIO)
- SW backend: pure `sha256_transform` — used for host tests and native builds
- `mine_nonce_range()`: unified nonce loop, parameterized by backend + nonce range
- Full 32-bit nonce space (0x00000000–0xFFFFFFFF), BIP 320 version rolling when exhausted
- APB peripheral bus fixed at 80 MHz — HW mining is MMIO-bound (~223 kH/s ceiling)
- Yield every 256K nonces (0x3FFFF mask), hashrate log every 1M (0xFFFFF)
- Hash byte order: little-endian (byte[31]=MSB) — matches Bitcoin internal byte order; `meets_target` and `hash_to_difficulty` both use this convention

### Mining pipeline (bitaxe — BM1370 ASIC)

- ASIC scans nonces autonomously at ~485 GH/s
- Stratum re-feeds fresh extranonce2 every 500ms via `g_miner_config.extranonce2_roll`
- Firmware re-verifies ASIC nonces with SHA256d before pool submission
- ASIC task uses UART I/O event loop, not `hash_backend_t`

### Network

- TCP keepalive (60s idle, 10s interval, 3 probes), TCP_NODELAY
- SO_RCVTIMEO cached to avoid redundant setsockopt calls
- Framework log noise suppressed at init: `esp_log_level_set("wifi", ESP_LOG_WARN)` etc.
- WiFi: infinite retry with 5s backoff, 60s startup timeout with esp_restart()

### Web UI

**Mining-mode SPA**: Svelte + TypeScript + Vite SPA in `webui/`. Hash-based routing. Tabs: Dashboard, Diagnostics, History, Pool, Settings, System, Update.

**Build**: `cd webui && npm ci && npm run build` generates `webui/dist/index.html` + `assets/index.js` + `assets/index.css` + `favicon.svg` + `logo.svg` (stable filenames — no content hashes, as firmware version is the cache-buster).

**Embed**: `components/webui/CMakeLists.txt` references `webui/dist/*` via `bb_embed_assets`. Firmware build assumes `webui/dist/` exists — run `make webui` before `make build-<env>` if stale.

**Provisioning-mode**: `prov_form.html` + `prov_save.html` in `components/webui/` (hand-authored, unchanged).

**Theme**: `theme.css` in `components/webui/` is provisioning-only now; SPA has its own theme baked in.

**Dev loop**: `cd webui && npm run dev` with `VITE_MINER_URL=http://<miner-host>` in `webui/.env` — dev server proxies `/api/*` to the miner. Run against any tdongle/bitaxe on the network without reflashing. Works side-by-side with `--port 5174` for multi-device compare.

**Tests**: `cd webui && npm test -- --run` (vitest).

**To add a web asset to the SPA**: edit the Svelte source. Assets bundled by Vite are included automatically in `dist/`. Only provisioning-mode static files need manual `bb_embed_assets` entries.

**API routes**: unchanged. `/api/stats` (polled every 5s), `/api/info` (device details), `/api/version`, `/api/ota/check`, `/api/ota/push`, `/api/ota/update`, `/api/power` (bitaxe-only — 404 on tdongle), `/api/fan` (bitaxe-only — 404 on tdongle; `duty_pct` reflects actual curve-controlled setting, null until first 5s telemetry tick), `/api/logs/status`, `/api/logs`.

**OTA check** suspends mining task to free heap for TLS handshake (~29 KB stack).

## Conventions

- FreeRTOS task stacks: 4096–8192 bytes; justify anything outside that range
- Mining task runs at priority 20 on core 1 — new tasks must not preempt it unintentionally
- Always take the `mining_stats` mutex before reading or writing shared stats
- Gate verbose debug output with `#ifdef TAIPANMINER_DEBUG`
- Network reconnect uses a goto-based loop — do not restructure to callbacks
- Board dispatch via `components/board/include/board.h` `#if defined(BOARD_*)` chain — never hardcode pins outside board headers
- `build_src_filter` in `platformio.ini` defines the host-testable boundary — never reference ESP-only APIs in files included by the native env

## Testing

- Host test scope: SHA-256, Stratum parsing, coinbase/merkle, header serialization, target, mining loop (nonce iteration, early reject, share finding), CRC, PLL, BM1370 framing
- Device test scope: mining integration, NVS persistence, live pool handshake
- Anonymize test data per workspace rules

## Releases

Tagging is manual: `git tag -a vX.Y.Z -m 'chore: vX.Y.Z tag' && git push origin vX.Y.Z`. The `release.yml` workflow runs CI then publishes a GitHub release with auto-generated notes categorized by PR label (`.github/release.yml`). PR labels are auto-applied from conventional-commit prefixes; `new-component` PRs need that label set manually.
