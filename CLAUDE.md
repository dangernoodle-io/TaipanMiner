# TaipanMiner

Bitcoin mining firmware for LilyGo T-Dongle S3 (ESP32-S3).

## Build

- Framework: ESP-IDF via PlatformIO
- Build: `pio run`
- Flash: `pio run -t upload` (hold BOOT button to enter download mode)
- Monitor: `pio device monitor`
- Monitor (non-TTY): `stty -f /dev/cu.usbmodem101 115200 raw -echo -echoe -echok -echoctl -echoke && cat /dev/cu.usbmodem101`
- Host tests: `pio test -e native`
- Debug build: `pio run -e debug` (adds `TAIPANMINER_DEBUG=1` — enables SW mining task, SHA verification, benchmarks)
- Static analysis: `pio check --skip-packages`

### Python compatibility

ESP-IDF's pydantic-core dependency requires Python <= 3.13. If your system Python is 3.14+, create the ESP-IDF venv manually with Python 3.13 before building:

```bash
python3.13 -m venv ~/.platformio/penv/.espidf-5.5.3
~/.platformio/penv/.espidf-5.5.3/bin/pip install -r ~/.platformio/packages/framework-espidf/tools/requirements/requirements.core.txt
```

Then create `~/.platformio/penv/.espidf-5.5.3/pio-idf-venv.json` with the correct version info to prevent PlatformIO from overwriting the venv.

## Project layout

- `src/` — app entry point, board pin definitions, version
- `components/` — ESP-IDF components (mining, stratum, display, wifi_prov, http_server, led, nv_config)
- `test/test_host/` — host-based unit tests (run without hardware via native env)
- `test/test_device/` — on-device integration tests

## Hardware

- ESP32-S3 dual-core @ 240MHz, 512KB SRAM, no PSRAM, 16MB flash
- 80x160 ST7735 LCD, APA102 RGB LED, BOOT button (GPIO0)
- Pin assignments in `src/board.h`

## Architecture

- Core 0: WiFi, Stratum, HTTP server, display, LED
- Core 1: HW SHA mining (priority 20, nonces 0x00000000-0x7FFFFFFF)
- Core 0: SW SHA mining (priority 3, nonces 0x80000000-0xFFFFFFFF, yields to WiFi/Stratum)
- Naming: `s_` prefix for file-scope static variables (e.g., `s_config`, `s_sock`)
- Inter-core: FreeRTOS queues (work_queue, result_queue) + mutex (mining_stats)

### Mining pipeline

- Phase 3 zero-bswap HW-format pipeline: midstate stored in HW-native word order
- `sha256_hw_mine_nonce`: force-inlined, midstate→SHA_H, block2+nonce→SHA_TEXT, SHA_CONTINUE, direct SHA_H→SHA_TEXT copy for pass 2, SHA_START
- SHA_TEXT registers are NOT preserved after SHA operations (verified empirically)
- SHA_START is 21% faster than SHA_CONTINUE+H0 for pass 2
- APB peripheral bus fixed at 80 MHz — HW mining is MMIO-bound, not CPU-bound (~223 kH/s ceiling)
- BIP 320 version rolling when nonce space exhausted
- Yield every 256K nonces (0x3FFFF mask), hashrate log every 1M (0xFFFFF)
- FreeRTOS tick rate: 100 Hz; vTaskDelay uses pdMS_TO_TICKS()
- Combined hashrate logged by HW task (hw + sw + total in kH/s)

### Network

- TCP keepalive (60s idle, 10s interval, 3 probes), TCP_NODELAY
- SO_RCVTIMEO cached to avoid redundant setsockopt calls
- Framework log noise suppressed at init: `esp_log_level_set("wifi", ESP_LOG_WARN)` etc.
- WiFi: infinite retry with 5s backoff, 60s startup timeout with esp_restart()

## Testing

- Host test scope: SHA-256, Stratum parsing, coinbase/merkle, header serialization, target, CRC, PLL, BM1370 framing
- Device test scope: mining integration, NVS persistence, live pool handshake
- Anonymize test data per workspace rules
