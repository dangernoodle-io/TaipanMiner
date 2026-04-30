# <img src="webui/ui-kit/assets/logo.svg" width="80" align="absmiddle" alt="TaipanMiner logo"> TaipanMiner

[![Build](https://github.com/dangernoodle-io/TaipanMiner/actions/workflows/build.yml/badge.svg)](https://github.com/dangernoodle-io/TaipanMiner/actions/workflows/build.yml)
[![Release](https://github.com/dangernoodle-io/TaipanMiner/actions/workflows/release.yml/badge.svg)](https://github.com/dangernoodle-io/TaipanMiner/actions/workflows/release.yml)
[![Coverage Status](https://coveralls.io/repos/github/dangernoodle-io/TaipanMiner/badge.svg?branch=main)](https://coveralls.io/github/dangernoodle-io/TaipanMiner?branch=main)

Bitcoin mining firmware for ESP32-S3 boards with optional ASIC support.

## Supported Boards

| Board | ASIC | Hash Rate |
|-------|------|-----------|
| LilyGo T-Dongle S3 | None (ESP32-S3 HW SHA) | ~223 kH/s |
| Bitaxe 601 Gamma | BM1370 | ~485 GH/s |
| Bitaxe 403 | BM1368 | ~500 GH/s |

## Quick Start

### Flash

Download the latest release for your board from the [releases page](https://github.com/dangernoodle-io/TaipanMiner/releases/latest). Each release ships two variants:

- **`taipanminer-<board>-factory.bin`** — full image (bootloader + partition table + ota_data_initial + app). Use for **first-time flashing** of a fresh device.
- **`taipanminer-<board>.bin`** — app-only. Use for OTA updates (see below); do **not** flash at offset `0` on a fresh device.

```bash
esptool --chip esp32s3 --before default_reset --after hard_reset write-flash 0 taipanminer-<board>-factory.bin
```

### Provision

On first boot, the device creates a WiFi access point named `TaipanMiner-XX`. Connect to it (password: `taipanminer`) and configure your WiFi and pool settings via the captive portal.

### OTA Update

After provisioning, the device is accessible at `taipanminer-<worker>.local`. Firmware can be updated via push or pull OTA:

```bash
# Push: upload a binary directly
curl -X POST http://taipanminer-<worker>.local/api/ota/push --data-binary @taipanminer-<board>.bin

# Pull: trigger the device to fetch the latest release from GitHub
curl -X POST http://taipanminer-<worker>.local/api/ota/update
```

## Build

Requires [PlatformIO](https://platformio.org/).

TaipanMiner consumes shared infrastructure (WiFi, provisioning, HTTP server, OTA, logging, NVS) from the [breadboard](https://github.com/dangernoodle-io/breadboard) library. The CMakeLists.txt uses `EXTRA_COMPONENT_DIRS` to include breadboard components at build time.

### Firmware build

The firmware embeds a web UI (Svelte + TypeScript SPA). Build the web UI before the firmware:

```bash
make webui              # build web UI into webui/dist/
make build              # build all boards
make test               # run host unit tests
make flash-<board>      # flash specific board (e.g. make flash-tdongle-s3)
```

### Web UI development

The web UI can be developed standalone without reflashing:

```bash
cd webui
npm ci                  # install dependencies
npm run dev             # start dev server (proxies /api/* to a miner on the network)
```

Set `VITE_MINER_URL=http://<miner-ip>` in `webui/.env` to target a live device. Run tests with `npm test -- --run`.

## License

[MIT](LICENSE)
