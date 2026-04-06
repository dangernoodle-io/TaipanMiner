# TaipanMiner

<p align="center">
  <img src="components/http_server/logo.svg" width="120" alt="TaipanMiner logo">
</p>

[![CI](https://github.com/dangernoodle-io/TaipanMiner/actions/workflows/ci.yml/badge.svg)](https://github.com/dangernoodle-io/TaipanMiner/actions/workflows/ci.yml)
[![Release](https://github.com/dangernoodle-io/TaipanMiner/actions/workflows/release.yml/badge.svg)](https://github.com/dangernoodle-io/TaipanMiner/actions/workflows/release.yml)
[![Coverage Status](https://coveralls.io/repos/github/dangernoodle-io/TaipanMiner/badge.svg?branch=main)](https://coveralls.io/github/dangernoodle-io/TaipanMiner?branch=main)

Bitcoin mining firmware for ESP32-S3 boards with optional ASIC support.

## Supported Boards

| Board | ASIC | Hash Rate |
|-------|------|-----------|
| LilyGo T-Dongle S3 | None (ESP32-S3 HW SHA) | ~223 kH/s |
| Bitaxe 601 Gamma | BM1370 | ~485 GH/s |

## Quick Start

### Flash

Download the latest release binary for your board from the [releases page](https://github.com/dangernoodle-io/TaipanMiner/releases/latest).

```bash
esptool.py --chip esp32s3 write_flash 0x0 taipanminer-<board>.bin
```

### Provision

On first boot, the device creates a WiFi access point named `TaipanMiner-XX`. Connect to it (password: `taipanminer`) and configure your WiFi and pool settings via the captive portal.

### OTA Update

After provisioning, the device is accessible at `taipanminer-<worker>.local`. Firmware can be updated via push or pull OTA:

```bash
# Push: upload a binary directly
curl -X POST http://taipanminer-<worker>.local/ota/upload --data-binary @taipanminer-<board>.bin

# Pull: trigger the device to fetch the latest release from GitHub
curl -X POST http://taipanminer-<worker>.local/api/ota/update
```

## Build

Requires [PlatformIO](https://platformio.org/).

```bash
make build              # build all boards
make test               # run host unit tests
make flash-<board>      # flash specific board (e.g. make flash-tdongle-s3)
```

## License

[MIT](LICENSE)
