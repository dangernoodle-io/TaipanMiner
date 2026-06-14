# Hardware-in-the-loop telemetry validation

Scripts that validate the breadboard telemetry + update stack on a real ESP32
device fleet — transport matrix (MQTT plain/server-TLS/mutual-TLS, HTTP
plain/TLS), heap/stability soak, OTA push, and update-check behavior.

## Requirements

- The **mqtt-stack** docker receiver running: mosquitto (`1883` plain / `8883`
  server-TLS / `8884` mutual-TLS), Fluent Bit (`9880` HTTP / `9881` HTTPS),
  Telegraf, InfluxDB, Grafana.
- The mqtt-stack TLS certs (`ca.crt`, `client.crt`, `client.key`).

## Environment

| Var | Default | Meaning |
|-----|---------|---------|
| `BB_TEST_RECEIVER` | `172.16.1.100` | host running the mqtt-stack receivers |
| `BB_TEST_CERTS` | `../mqtt-stack/certs` (sibling of this repo) | dir holding `ca.crt`/`client.crt`/`client.key` |

The default fleet IPs in `fleetmon.py` / `updatecheck.py` / the launch examples
are the dev fleet; edit them for your devices.

> Transport switching is **reboot-to-apply** (breadboard B1-289): `PATCH
> /api/telemetry` validates + persists NVS, and a reboot wires the one enabled
> sink. These scripts drive that supported config→reboot→verify flow.

## Scripts

| Script | Usage | Purpose |
|--------|-------|---------|
| `fleetsoak.py` | `fleetsoak.py <host> <hosttag> <dwell_min> <path...>` | Per-board soak rotation: cycle a board through transport paths (`mqtt_plain` `mqtt_stls` `mqtt_mtls` `http_plain` `http_tls`), config→reboot→verify, dwell polling heap/reset/publisher, fire an update-check mid-dwell. Run one per board in parallel. |
| `fleetmon.py` | `fleetmon.py [cycles] [interval_s]` | Fleet health snapshot per cycle: `reset_reason`, free heap, active transport+TLS, publisher state. Flags abnormal resets / down boards. |
| `otaflash.py` | `otaflash.py <host> <firmware.bin> [--mark-valid]` | OTA-push a firmware image (`POST /api/update/push`) and wait for reboot onto the new version. `--mark-valid` POSTs `/api/update/mark-valid` after boot to cancel rollback; a `409 not pending` response means the image already self-validated (success). |
| `updatecheck.py` | `updatecheck.py` | Sweep: `POST /api/update/check` across the fleet, record heap before/after + status, flag crash-on-update-check. |
| `tlsrow.py` | `tlsrow.py <device_host> <step>` | Single transport path via config→reboot→verify (`step` = one of the five paths). Confirms cert upload (`*_set` flags) and InfluxDB ingestion. |

## Examples

```sh
# soak one board through all five paths, 12-min dwells
python3 scripts/test/fleetsoak.py 172.16.1.71 tdongles3-1 12 \
    http_tls mqtt_mtls http_plain mqtt_stls mqtt_plain

# health snapshot, 30 cycles @ 60s
python3 scripts/test/fleetmon.py 30 60

# flash a board
python3 scripts/test/otaflash.py 172.16.1.110 .pio/build/esp32-c3-supermini/firmware.bin

# one TLS row with a custom receiver/certs
BB_TEST_RECEIVER=10.0.0.5 BB_TEST_CERTS=/path/to/certs \
    python3 scripts/test/tlsrow.py 172.16.1.81 mqtt_mtls
```
