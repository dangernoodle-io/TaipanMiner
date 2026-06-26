# TaipanMiner fleet test harness

`fleet.py` is the single entry point for all device testing: functional validation,
long-duration soak, stress, fault injection, transport-matrix, and OTA operations.

## Governance rule

**Extend the harness; never add one-off scripts.**  All device test, soak, stress,
fault-injection, and OTA-validation tooling lives here (`fleetlib/` + `suites/`).
When functionality is missing, add a suite, detector, or `fleetlib` helper — do NOT
drop standalone scripts into `scripts/test/`.

---

## Setup

```sh
# Option A — make
cd scripts/test
make setup

# Option B — manual
cd scripts/test
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

Run unit tests:

```sh
make test
# or
.venv/bin/python -m unittest discover tests
```

---

## Discovery

Devices advertise under `_taipanminer._tcp.local.` (mDNS / zeroconf).

```sh
# discover all devices on the LAN
.venv/bin/python fleet.py discover

# supply an explicit roster instead of mDNS
.venv/bin/python fleet.py discover --hosts 172.16.1.71,172.16.1.81

# filter by board class (substring match)
.venv/bin/python fleet.py discover --board bitaxe
```

The `--hosts` flag is accepted by every subcommand and bypasses mDNS.

---

## Subcommands

### `discover`

Print a table of discovered devices with board class, version, and uptime.

```sh
.venv/bin/python fleet.py discover [--hosts H,H] [--board CLASS] [--discover-timeout SEC]
```

### `status`

Fetch `/api/info` + `/api/health` per device and print a summary table.

```sh
.venv/bin/python fleet.py status [--hosts H,H]
```

### `functional`

Validate each device's REST API against its own served OpenAPI spec
(`GET /api/openapi.json`).  No hardcoded field lists — the device is the SSOT.

```sh
.venv/bin/python fleet.py functional [--strict] [--fields F,F] [--gate NAME] [--skip NAME]
```

Flags:
- `--strict` — fail on nullable/empty-enum schema issues (default: downgrade to SKIP)
- `--fields F,F` — check only that these dot-path fields exist in responses
- `--gate NAME` / `--skip NAME` — enable/disable individual check gates (repeatable)

CI-safe: read-only, no device state changes.

### `soak`

Long-duration fleet health soak.  Polls each device at `poll_interval`, applying the
full detector set (heap floor, heap leak, reboot, reset-reason, WDT, publisher, hashrate,
vcore).  Results include anomalies with detector name and poll index.

```sh
.venv/bin/python fleet.py soak [--duration 1h] [--interval 60] [--target VERSION]
                               [--expected-ghs N] [--settle SEC] [--no-settle]
                               [--gate NAME] [--skip NAME]
```

Duration accepts `30s`, `2m`, `1h`, or bare seconds.

Examples:

```sh
# 1-hour soak of all discovered devices
.venv/bin/python fleet.py soak

# 30-minute soak targeting one board class
.venv/bin/python fleet.py soak --board bitaxe --duration 30m

# soak with specific version assertion and faster poll
.venv/bin/python fleet.py soak --hosts 172.16.1.71 --duration 1h --interval 30 --target v0.70.0
```

Operator-only (long duration, consumes device resources).

### `stress`

Concurrent HTTP load generator.  Respects `max_concurrent` and `max_rps` from the
board profile so no board is over-driven.

```sh
.venv/bin/python fleet.py stress [--duration DURATION] [--level LEVEL]
```

Operator-only: can expose heap pressure / panic bugs.

### `faults`

Fault-injection scenarios: socket exhaustion, broker outage, broker-outage cycles
(TA-432).

```sh
.venv/bin/python fleet.py faults [--scenario NAME] [--yes] [--dry-run]
```

**Destructive / operator-only.**  Requires `--yes` to execute mutating operations;
`--dry-run` logs actions without applying them.

### `matrix`

Transport-matrix suite — configure each telemetry transport (MQTT plain/server-TLS/
mutual-TLS, HTTP plain/TLS), settle, and verify publish health per the no-false-sinks
rule.

```sh
.venv/bin/python fleet.py matrix [--rows R,R] [--receiver HOST] [--certs DIR]
                                 [--influx-container NAME] [--yes] [--dry-run]
```

Environment variables (can be overridden by flags):
- `BB_TEST_RECEIVER` — telemetry receiver host
- `BB_TEST_CERTS` — directory with `ca.crt` / `client.crt` / `client.key`

**Mutating (PATCHes /api/telemetry) / operator-only.**

### `ota`

OTA firmware operations.  All sub-operations require `--yes` or `--dry-run` for
mutating actions.

```sh
# push a local .bin to devices
.venv/bin/python fleet.py ota push --bin .pio/build/bitaxe-601/firmware.bin \
    --target v0.70.0 [--yes] [--dry-run]

# trigger pull-OTA
.venv/bin/python fleet.py ota pull [--mode auto|pull] [--target VERSION] [--yes]

# mark current image valid (cancel rollback timer)
.venv/bin/python fleet.py ota mark-valid [--yes]

# rollback to previous image
.venv/bin/python fleet.py ota recover [--yes]

# read OTA status + progress (read-only)
.venv/bin/python fleet.py ota status

# verify version + mining state post-settle
.venv/bin/python fleet.py ota verify --target v0.70.0 [--settle SEC]
```

**Destructive / operator-only.**

---

## Common flags

All subcommands accept:

| Flag | Default | Description |
|------|---------|-------------|
| `--hosts H,H` | mDNS | skip discovery, use these IPs/hostnames |
| `--board CLASS` | all | filter by board-class substring |
| `--discover-timeout SEC` | 10 | mDNS browse window |
| `--fields F,F` | — | on-demand field subset for functional validation |
| `--gate NAME` | all enabled | enable a specific check gate (repeatable) |
| `--skip NAME` | — | disable a specific check gate (repeatable) |
| `--out-json PATH` | — | write JSON results to file |
| `--out-junit PATH` | — | write JUnit XML results to file |
| `--baseline PATH` | — | compare against a prior JSON result file |
| `--criteria PATH` | `config/criteria.yaml` | YAML criteria override file |
| `--settle SEC` | from criteria | warmup settle delay |
| `--no-settle` | false | disable settle/readiness gate |
| `--dry-run` | false | log mutating ops but don't execute |
| `--yes` | false | auto-confirm guard for mutating ops |
| `--log-level` | WARNING | DEBUG / INFO / WARNING / ERROR |

---

## Config reference

### `config/criteria.yaml`

Soak pass/fail thresholds.  All keys match `fleetlib/criteria.py` `Criteria` fields:

| Key | Default | Description |
|-----|---------|-------------|
| `poll_interval` | 60.0 | seconds between samples |
| `duration` | 3600.0 | total soak window (seconds) |
| `heap_floor` | 50000 | bytes — `heap.internal.free` must stay >= this |
| `heap_leak_check` | true | `min_free` must not decline over window |
| `reboot_tolerance_ms` | 30000 | uptime regression > this (ms) => reboot detected |
| `bad_reset_reasons` | [panic, task_wdt, int_wdt, brownout] | reset reasons that trigger anomaly |
| `wdt_resets_flat` | true | `wdt_resets` count must not increase |
| `publisher_max_polls` | 6 | consecutive polls with `pub_ok=false` before anomaly |
| `hashrate_floor_pct` | 80.0 | % of `expected_ghs` (ASIC only) |
| `vcore_floor_mv` | 500 | mV floor for vcore (ASIC only) |
| `vcore_restart_flat` | true | `vcore_restart_count` must not increase (ASIC) |
| `version_check` | false | require running version == target |
| `max_missed_polls` | 4 | consecutive missed polls => downtime anomaly |
| `settle_delay` | 120 | warmup floor in seconds |

### `config/profiles.yaml`

Board-class capability overrides.  Keys are board-class prefix strings (e.g. `bitaxe`,
`esp32-c3`).  Each entry overrides the corresponding `Profile` dataclass fields:

| Key | Description |
|-----|-------------|
| `is_asic` | enables vcore + hashrate checks |
| `has_psram` | informational |
| `max_concurrent` | max HTTP connections during stress |
| `max_rps` | max requests/second during stress |
| `poll_interval` | override soak poll interval for this class |
| `heap_floor` | override heap floor for this class |
| `vcore_floor_mv` | override vcore floor (ASIC) |
| `publisher_polls` | override publisher anomaly threshold |

---

## Results

`--out-json PATH` writes a JSON file with per-test status, detail, and metrics.
`--out-junit PATH` writes JUnit XML for CI integration.
`--baseline PATH` compares the current run against a prior JSON result and prints regressions.

---

## Suite CI safety

| Suite | CI-safe | Notes |
|-------|---------|-------|
| `functional` | yes | read-only; validates OpenAPI responses |
| `soak` | no | long-duration; operator use |
| `stress` | no | applies load; may expose instability |
| `faults` | no | destructive; requires `--yes` |
| `matrix` | no | mutates transport config; requires receiver + certs |
| `ota push/pull/recover` | no | flashes firmware; requires `--yes` |
| `ota status/verify` | yes | read-only |

---

## Identity verification

OTA push and other destructive operations call `fleetlib.ota` which re-fetches
`/api/info` at the target IP immediately before acting and verifies board identity.
This guards against stale-IP hazards on DHCP churn (a reassigned IP otherwise risks
flashing the wrong board).
