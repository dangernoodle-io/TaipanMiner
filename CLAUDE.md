# StickMiner

Bitcoin mining firmware for LilyGo T-Dongle S3 (ESP32-S3).

## Build

- Framework: ESP-IDF via PlatformIO
- Build: `pio run`
- Flash: `pio run -t upload` (hold BOOT button to enter download mode)
- Monitor: `pio device monitor`
- Host tests: `pio test -e native`
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
- Core 1: dedicated mining (high priority)
- Inter-core: FreeRTOS queues (work_queue, result_queue) + mutex (MiningStats)

## Testing

- Host tests cover SHA-256, Stratum message parsing, coinbase/merkle, header serialization, target conversion
- Device tests cover mining task integration, NVS persistence, live pool handshake
- Anonymize test data per workspace rules
