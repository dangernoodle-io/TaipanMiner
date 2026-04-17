# TaipanMiner

Bitcoin mining firmware for ESP32-S3 boards with optional ASIC support.

## Build

- Framework: ESP-IDF via PlatformIO
- `make help` — show all targets
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

For clangd-based C/C++ IntelliSense (e.g. via the `esp-idf-clangd` Claude Code plugin):

1. Run `make compile-db` once to generate `compile_commands.json` for every board. Re-run only when `platformio.ini` or toolchain versions change.
2. Copy `.clangd.example` to `.clangd` (gitignored, per-developer).
3. Uncomment the `CompilationDatabase:` line matching the board you're actively developing.

## Boards

| Env | Board | ASIC | Console |
|-----|-------|------|---------|
| `tdongle-s3` | LilyGo T-Dongle S3 | none (SW mining) | USB CDC |
| `bitaxe-601` | Bitaxe 601 Gamma | BM1370 | USB CDC |
| `bitaxe-403` | Bitaxe 403 | BM1368 | USB CDC |

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
9. **LSP** — add a `pio run -t compiledb -e <env>` line to the `compile-db` target in `Makefile`, and add a matching `# CompilationDatabase: .pio/build/<env>` line to `.clangd.example`

## Project layout

- `src/` — app entry point, version
- `components/` — ESP-IDF components (mining, stratum, board, asic, display, wifi_prov, http_server, led, nv_config, ota_pull)
- `components/board/include/boards/` — per-board pin/peripheral headers
- `sdkconfig/` — hand-authored sdkconfig deltas per board
- `test/test_host/` — host-based unit tests (run without hardware via native env)
- `test/test_device/` — on-device integration tests

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

- Mining-mode SPA: `mining.html` + `mining.js` at `/`, four tabs (Info, Status, Settings, Update)
- Provisioning-mode: `prov_form.html` at `/`, `prov_save.html` at `/save`
- `theme.css` shared between both modes (dark navy/gold design system)
- `scripts/embed_html.py` pre-build: gzip-compresses web assets → C byte arrays in `src/*_gz.c`
- To add a web asset: add file to `components/http_server/`, add to `embed_html.py` FILES, add `src/<name>_gz.c` to CMakeLists.txt SRCS, add extern + handler in `http_server.c`
- API: `/api/stats` (polled every 5s), `/api/info` (device details), `/api/version`, `/api/ota/check`
- OTA check suspends mining task to free heap for TLS handshake (~29 KB stack)

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
