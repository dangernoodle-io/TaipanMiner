# TaipanMiner fleet test harness

`fleet.py` is the single entry point for all device testing: functional validation,
long-duration soak, stress, fault injection, telemetry transport, and OTA operations.

## Quick start

```sh
cd scripts/fleet
./fleet discover
```

`./fleet` is a bash wrapper that auto-creates `.venv` and installs `requirements.txt`
on first run (needs network once).  Subsequent runs skip pip entirely.  All bootstrap
output goes to stderr; stdout is clean for harness output and JSON.

---

## Governance rule

**Extend the harness; never add one-off scripts.**  All device test, soak, stress,
fault-injection, and OTA-validation tooling lives here (`fleetlib/` + `suites/`).
When functionality is missing, add a suite, detector, or `fleetlib` helper — do NOT
drop standalone scripts into `scripts/fleet/`.

---

## Setup

**Requires Python >= 3.11** (CI target).  The `./fleet` wrapper, `fleet.py`, and
`make setup` all check the version at startup and print a clear error if the system
Python is too old (TA-450).

```sh
# Option A — make
cd scripts/fleet
make setup

# Option B — manual
cd scripts/fleet
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

Run unit tests:

```sh
make test
# or
.venv/bin/python -m unittest discover tests
```

> **Fallback (manual venv):** `make setup` creates `.venv` and installs deps without
> running the wrapper.  Use this if you need a predictable install step in CI or want
> to pin the venv separately.

---

## Discovery

Devices advertise under `_taipanminer._tcp.local.` (mDNS / zeroconf).

```sh
# discover all devices on the LAN
./fleet discover

# supply an explicit roster instead of mDNS
./fleet discover --hosts 172.16.1.71,172.16.1.81

# filter by board class (substring match)
./fleet discover --board bitaxe
```

The `--hosts` flag is accepted by every subcommand and bypasses mDNS.

**Unreachable host diagnostics** — when `--hosts` is used and a host fails
enrichment (busy httpd worker, wrong IP, timeout), the harness reports the
per-host reason on stderr rather than collapsing everything into a generic
"No devices found." message.  Hosts that do resolve are still used; only when
*none* resolve does the command fail, printing a summary such as:

```
2 host(s) specified; none reachable:
  172.16.1.250: unreachable (timeout after 5s)
  172.16.1.251: unreachable (connection refused)
```

Reason classes: `timeout` (socket/HTTP timeout), `refused` (connection
refused), `no_route` (host unreachable / DNS failure), `http_error` (non-2xx
HTTP response), `bad_response` (parse error or other).

When mDNS discovery returns nothing (no `--hosts` given), the message
explicitly says "No devices found via mDNS" so you know to check mDNS
advertisement rather than host connectivity.

---

## Subcommands

### `logs`

Retrieve the device kernel log via the SSE endpoint `GET /api/logs`.  Read-only,
CI-safe.

```sh
# tail the last 10 log lines then exit
./fleet logs --hosts 172.16.1.81 --lines 10

# stream for 30 seconds then exit
./fleet logs --hosts 172.16.1.81 --duration 30s

# stream until Ctrl-C
./fleet logs --hosts 172.16.1.81 --follow

# collect up to 20 lines and also write to a file
./fleet logs --hosts 172.16.1.81 --lines 20 --out /tmp/device.log

# multi-host: interleaved lines with per-host prefix
./fleet logs --hosts 172.16.1.81,172.16.1.68 --duration 10s
```

Flags:

| Flag | Default | Description |
|------|---------|-------------|
| `--follow` / `-f` | false | stream until Ctrl-C (clean exit 0) |
| `--duration DUR` | — | stop after: `30s`, `5m`, `1h`, or bare seconds |
| `--lines N` | — | stop after N log lines |
| `--out PATH` | — | tee captured lines to a file (stdout is also written) |

When neither `--follow`, `--duration`, nor `--lines` is given, the default is
**50 lines or 10 seconds**, whichever comes first.

**One-SSE-consumer-at-a-time caveat:** the device's log sink supports only one
active SSE consumer (TA-264/TA-265).  If another consumer already holds the
sink, `fleet logs` prints a clear error message naming the host and exits
non-zero rather than hanging.  With multiple `--hosts`, an unavailable host is
reported on stderr but the remaining hosts continue streaming.

**Early-bail on no-data (TA-458):** if no SSE line (including keepalive comments)
arrives within 10 seconds of opening the stream, `fleet logs` prints a warning to
stderr and disconnects that host rather than holding the connection for the full
`--duration` / `--follow` window.  With multiple `--hosts`, only the silent host
is disconnected; others continue streaming.  This protects boards like the S2 that
return 200 headers but emit nothing (firmware issue TA-463 tracks the root cause).

**Single-worker `--follow` warning (TA-458):** boards with limited httpd workers
(esp32-s2, esp32-c3) are flagged with `single_worker: true` in
`config/profiles.yaml`.  When `--follow` targets one of these boards, `fleet logs`
prints a prominent warning on stderr before proceeding:

```
WARNING: 172.16.1.107 (esp32-s2-mini) has limited httpd workers; --follow can
saturate it and block other endpoints — consider --lines/--duration instead
```

Use `--lines N` or `--duration T` on low-worker boards to release the httpd worker
promptly after collecting the desired log data.

**Related (out of scope here):** `GET /api/log/level` and
`PATCH /api/log/level` allow per-tag log-level control; see `fleet describe
/api/log/level` for the schema.

### `discover`

Print a table of discovered devices with board class, version, and uptime.

```sh
./fleet discover [--hosts H,H] [--board CLASS] [--discover-timeout SEC]
```

### `describe`

Inspect the OpenAPI spec served by a device.  Uses `GET /api/openapi.json` as
the source of truth — the device is the SSOT.

```sh
# list all endpoints served by the device
./fleet describe [--hosts H,H] [--board CLASS]

# show request and response schemas for all methods on a path
./fleet describe PATH [--hosts H,H]

# show schema for a specific method
./fleet describe PATH METHOD [--hosts H,H]

# dump raw JSON schema instead of the pretty table
./fleet describe PATH METHOD --json
```

CI-safe: read-only.

### `call`

Make an arbitrary API request to one or more devices.  GET and other read
methods execute directly.  Mutating methods (POST / PUT / PATCH / DELETE) are
safety-gated exactly like other destructive harness operations — they require
either `--dry-run` or `--yes`.

```sh
# read-only GET
./fleet call GET PATH [--fields F,F] [--hosts H,H]

# mutating request — dry-run shows intent, no HTTP sent
./fleet call PATCH PATH --json '{"key":value}' --dry-run [--hosts H,H]

# mutating request — live execution requires --yes
./fleet call PATCH PATH --json '{"key":value}' --yes [--hosts H,H]

# body from file
./fleet call POST PATH --json-file body.json --yes

# skip request-body schema validation
./fleet call PATCH PATH --json '...' --no-validate --dry-run
```

Flags specific to `call`:

| Flag | Description |
|------|-------------|
| `--json BODY` | inline JSON request body |
| `--json-file FILE` | request body from a JSON file |
| `--no-validate` | skip body schema validation |

`call` also accepts all common flags (`--fields`, `--out-json`, `--dry-run`,
`--yes`, `--hosts`, `--board`, etc.).

**Body validation:** if the served spec declares a `requestBody` schema for the
target path + method, `call` validates the supplied body against it before
sending any request.  Validation failures print each error and a hint:
`run ./fleet describe <PATH> <METHOD> to see the expected shape`.  Pass
`--no-validate` to bypass.

**Safety gate:** mutating calls follow the same `Guard` logic as OTA and
fault-injection — identity is re-verified at the target IP before acting, `--dry-run`
logs the intended operation without sending it, and `--yes` is required for live
execution.

CI-safe with `--dry-run`.

### `watch`

Poll a single endpoint on each device at a fixed interval and print time-series rows.
Read-only (GET only). CI-safe.

```sh
# stream /api/diag/heap every 5 seconds
./fleet watch /api/diag/heap [--hosts H,H]

# extract specific fields
./fleet watch /api/diag/heap --fields internal.free,internal.min_free

# stop after 10 ticks
./fleet watch /api/diag/heap --fields internal.free --count 10

# run for 5 minutes
./fleet watch /api/diag/heap --duration 5m

# only print rows when field values change
./fleet watch /api/diag/heap --fields internal.free --on-change

# exit 0 when condition is met on all devices, exit 1 on timeout
./fleet watch /api/diag/heap --fields internal.free --until 'internal.free > 60000' --count 20

# exit 0 when ANY device satisfies the condition
./fleet watch /api/diag/heap --until 'internal.free > 60000' --any --duration 2m

# flag rows when condition is true (non-terminating)
./fleet watch /api/diag/heap --alert 'internal.free < 50000' --duration 10m

# write time series to files
./fleet watch /api/diag/heap --fields internal.free --out-csv heap.csv --out-json heap.json
```

Flags specific to `watch`:

| Flag | Default | Description |
|------|---------|-------------|
| `--fields F,F` | — | comma-separated dotted fields to extract (default: whole response) |
| `--interval SEC` | 5 | seconds between polls |
| `--duration DUR` | — | stop after: `30s`, `5m`, `1h`, or bare seconds |
| `--count N` | — | stop after N poll ticks |
| `--on-change` | false | emit row only when field values change from previous tick |
| `--until EXPR` | — | exit 0 when condition satisfied; exit 1 if not met before bound |
| `--any` | false | with `--until`: satisfied when ANY device meets the condition (default: all) |
| `--alert EXPR` | — | prefix rows with `ALERT` when condition true (non-terminating) |
| `--out-json PATH` | — | write time-series records as a JSON list |
| `--out-csv PATH` | — | write time-series records as CSV |

**Exit codes** (scriptable wait pattern):

```sh
# block until all devices have >60 KB free heap (or 5 minutes elapse)
./fleet watch /api/diag/heap --until 'internal.free > 61440' --duration 5m && echo "ready"
```

- Exit 0: `--until` condition satisfied, or normal termination (count/duration/Ctrl-C)
- Exit 1: `--until` given but condition not met before count/duration bound; or no devices found

**Expression syntax** (`--until` / `--alert`):

Expressions use a safe AST-based evaluator (no `eval`/`exec`).  Supported:
- field paths: dotted names like `internal.free`, `uptime_ms`
- comparison ops: `<`, `<=`, `>`, `>=`, `==`, `!=`
- boolean ops: `and`, `or`, `not`
- literals: integers, floats, strings, `true`, `false`, `null`
- parentheses for grouping

Function calls, dunders, and Python builtins are rejected (`ExprError`).

**Output format:**

```
HH:MM:SS  192.0.2.81  internal.free=72432
HH:MM:SS  192.0.2.71  internal.free=68120
ALERT HH:MM:SS  192.0.2.81  internal.free=48000
```

When no `--fields` are given, the whole response is printed as compact JSON on each row.

### `status`

Fetch `/api/info` + `/api/health` per device and print a summary table.

```sh
./fleet status [--hosts H,H]
```

### `probe-endpoints` (TA-469)

Spec-driven endpoint crash probe.  Enumerates every GET path from the device's
served `/api/openapi.json`, hits each one once, and re-reads `/api/info` uptime
between hits.  Any endpoint after which uptime regresses (reboot/crash) or the
device goes unreachable is named and flagged in the output.

```sh
# probe all safe GET endpoints on one board (healthy board reports no crash)
./fleet probe-endpoints --hosts 172.16.1.81

# also probe POST/PUT/PATCH/DELETE (requires --yes guard for destructive ops)
./fleet probe-endpoints --hosts 172.16.1.81 --include-mutating --yes

# also probe streaming endpoints (/api/logs etc.) that would otherwise hang workers
./fleet probe-endpoints --hosts 172.16.1.81 --include-streaming
```

By default:
- Only **safe GET** endpoints are probed.
- **Streaming** endpoints (`/api/logs`, `/api/diag/events`, `/ws`) are skipped
  (they'd hold httpd workers — see TA-458).
- **Mutating** methods (POST/PUT/PATCH/DELETE) are skipped.

Flags specific to `probe-endpoints`:

| Flag | Default | Description |
|------|---------|-------------|
| `--include-mutating` | false | also probe POST/PUT/PATCH/DELETE endpoints |
| `--include-streaming` | false | also probe streaming endpoints |

Output: per-endpoint table (endpoint, HTTP status, ok / CRASH).  Exit 0 when no
crash detected; exit 1 when any endpoint causes a reboot or the device goes
unreachable.

CI-safe with default flags (GET-only, read-only).

### `functional`

Validate each device's REST API against its own served OpenAPI spec
(`GET /api/openapi.json`).  No hardcoded field lists — the device is the SSOT.

```sh
./fleet functional [--strict] [--fields F,F] [--gate NAME] [--skip NAME]
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

**Live per-tick reporting is on by default.**  Each tick prints a timestamped row per
device showing heap, uptime, mqtt/publish state, hashrate + % of expected (when available),
and vcore (ASIC).  Warmup ticks are prefixed with `~`.  Pass `--quiet` to suppress (e.g.
in unattended CI runs).

**Hashrate units:** the harness normalizes raw `/api/stats` `hashrate` (H/s) to GH/s
internally.  Display auto-scales: `359.82kH/s` for CPU miners, `915.60GH/s` for bitaxe.
`--samples-out` writes `hashrate` in GH/s; `Result.metrics` hashrate keys are also GH/s.

```sh
./fleet soak [--duration 1h] [--interval 60] [--target VERSION]
             [--expected-ghs N] [--settle [SEC]]
             [--gate NAME] [--skip NAME]
             [--quiet]
             [--samples-out PATH]
             [--attach-logs {anomaly,always,never}]
```

Duration accepts `30s`, `2m`, `1h`, or bare seconds.

Flags specific to `soak`:

| Flag | Default | Description |
|------|---------|-------------|
| `--duration DUR` | from criteria | soak window |
| `--interval SEC` | from criteria | poll interval |
| `--target VERSION` | — | fail devices not running this version |
| `--expected-ghs N` | from /api/stats | hashrate ceiling override (0 disables) |
| `--settle` | off | opt-in settle: bare flag uses criteria default (120 s) |
| `--settle N` | — | opt-in settle with explicit delay of N seconds |
| `--quiet` | false | suppress per-tick live rows (CI/unattended) |
| `--samples-out PATH` | — | write per-tick time-series: `.json` (NDJSON) or `.csv` |
| `--attach-logs MODE` | `anomaly` | attach device log tail to result: `anomaly`, `always`, `never` |

**Settle is opt-in.**  Without `--settle`, detectors are active from t=0 with no warmup
suppression.  Pass `--settle` (bare) to enable the criteria default warmup (120 s); pass
`--settle N` for an explicit delay.

**Settle uses a single readiness definition** shared with `ota verify` and `stress`.
Warmup ends when *both* conditions hold: (1) the settle floor has elapsed **and**
(2) the board passes the readiness predicate — heap ≥ floor, pool connected and
hashrate above min (mining boards only).  `settle_delay` is the *floor*, not the
whole window; a slow-starting board stays in warmup until it is actually ready.
When settle is off (`settle_delay=0`), behaviour is unchanged: detectors are active
from t=0.

**Hashrate coverage:** all mining boards that report `expected_ghs > 0` in `/api/stats`
are covered — not just ASICs.  Per-class floor percentages are configured in
`config/profiles.yaml` (`hashrate_floor_pct`):

| Board class | Floor |
|-------------|-------|
| bitaxe (ASIC) | 75% |
| esp32-wroom32 | 80% |
| tdongle-s3 | 80% |
| esp32-c3 | 70% |
| esp32-s3 | 80% |
| esp32-s2 | 75% |

**Baseline metrics:** `Result.metrics` is populated with per-run summary statistics for
every soak result, enabling regression detection via `--baseline`:

| Metric | Description | Direction |
|--------|-------------|-----------|
| `heap_free_min` | minimum heap.internal.free seen | higher is better |
| `heap_min_free_min` | minimum min_free_ever seen | higher is better |
| `largest_block_min` | minimum largest free block | higher is better |
| `hashrate_min` | minimum hashrate sample | higher is better |
| `hashrate_avg` | mean hashrate over run | higher is better |
| `hashrate_pct_expected_min` | min % of expected_ghs | higher is better |
| `temp_max` | peak temperature (°C) | lower is better |
| `reboot_count` | detected mid-run reboots | lower is better |
| `anomaly_count` | total anomalies fired | lower is better |
| `duration_s` | soak window duration | — |

Examples:

```sh
# 1-hour soak of all discovered devices
./fleet soak

# 30-minute soak targeting one board class
./fleet soak --board bitaxe --duration 30m

# soak with specific version assertion and faster poll
./fleet soak --hosts 172.16.1.71 --duration 1h --interval 30 --target v0.70.0

# soak with live rows suppressed (CI/unattended)
./fleet soak --hosts 172.16.1.81 --duration 2h --quiet

# write full per-tick time-series for offline analysis
./fleet soak --hosts 172.16.1.81 --duration 1h --samples-out /tmp/soak.csv

# attach log tail on anomaly (default) and save JSON results
./fleet soak --hosts 172.16.1.81,172.16.1.68 --duration 1h \
    --out-json /tmp/run.json --attach-logs anomaly

# always attach log tail for post-run analysis
./fleet soak --hosts 172.16.1.81 --duration 30m \
    --out-json /tmp/run.json --attach-logs always

# baseline regression check
./fleet soak --duration 30m --out-json /tmp/run.json  # save run
./fleet soak --duration 30m --baseline /tmp/run.json  # compare
```

Operator-only (long duration, consumes device resources).

---

## Run metrics publishing (TA-455)

After every suite run (`soak`, `stress`, and any suite that populates
`Result.metrics`), the harness publishes per-result metrics to a mosquitto topic.
This feeds the same mosquitto → InfluxDB → Grafana pipeline as device telemetry
(TA-270), enabling run-over-run tracking in dashboards.

**Default ON** when a broker is configured; silently skipped (one-line note on
stderr) when no broker is configured.  Publish failures are warnings — they never
change the run exit code.

### Topic convention

```
<prefix>/<suite>/<board>
```

Default prefix is `fleettest` (distinct from device telemetry `metrics/` namespace).
Examples:
- `fleettest/soak/bitaxe-601`
- `fleettest/stress/esp32-wroom32`

### Payload (JSON)

```json
{
  "suite":   "soak",
  "host":    "192.168.1.81",
  "board":   "bitaxe-601",
  "ts":      "2025-01-01T12:00:00Z",
  "status":  "pass",
  "metrics": { "heap_free_min": 72000, "hashrate_avg": 485.2, ... }
}
```

One message per device result that has a non-empty `metrics` dict.

### Configuration

| Flag | Env var | Description |
|------|---------|-------------|
| `--metrics-mqtt-url HOST[:PORT]` | `BB_TEST_METRICS_BROKER` | broker for run metrics (also falls back to `BB_TEST_RECEIVER`) |
| `--metrics-topic PREFIX` | — | topic prefix (default: `fleettest`) |
| `--no-publish-metrics` | — | opt-out: disable publishing entirely |

No credentials or broker IPs are hardcoded.  TLS reuses the existing `--certs` /
`BB_TEST_CERTS` cert infrastructure when connecting to a TLS broker.

```sh
# explicit broker
./fleet soak --hosts 172.16.1.81 --metrics-mqtt-url 172.16.1.100:1883

# via env var
BB_TEST_METRICS_BROKER=172.16.1.100:1883 ./fleet soak

# opt-out
./fleet soak --no-publish-metrics
```

---

### `stress`

Concurrent HTTP load generator.  Respects `max_concurrent` and `max_rps` from the
board profile so no board is over-driven.

```sh
./fleet stress [--duration DURATION] [--level LEVEL]
```

Operator-only: can expose heap pressure / panic bugs.

### `faults`

Fault-injection scenarios: socket exhaustion, broker outage, broker-outage cycles
(TA-432).

```sh
./fleet faults [--scenario NAME] [--yes] [--dry-run]
```

**Destructive / operator-only.**  Requires `--yes` to execute mutating operations;
`--dry-run` logs actions without applying them.

### `telemetry`

Telemetry transport suite — configure each telemetry transport, settle, and verify
publish health per the no-false-sinks rule.

**Default row: `mqtt_plain`** (the deployed plaintext MQTT transport).  Other rows
(`mqtt_stls`, `mqtt_mtls`, `http_plain`, `http_tls`) are available via `--rows` but
are not run by default — they require certs, are not deployed in production, and are
opt-in for targeted testing.

**MQTT validation: broker-subscribe (mosquitto receipt).**  After the device-side
publisher health check passes, the suite subscribes to the same broker endpoint using
`paho-mqtt` and waits for a message on `<topic_prefix>/<hostname>/#` (default prefix
`metrics`; matches any subtopic the device publishes).  A received message is positive
confirmation.  `paho-mqtt` is a soft dependency — if not installed, the broker-subscribe
check is skipped with a note and the device-side signal is the only validation.

**HTTP rows: device-side signal only.**  `publisher.last_publish_ok` plus fresh-age
check.  No broker is reachable for HTTP sink validation.

**InfluxDB docker-exec removed.**  The old `docker exec influx ping` path was removed —
it assumed a local container and is useless on a separate-server stack.  Network-based
InfluxDB validation is a follow-up (TA-455-adjacent).

**`paho-mqtt`** is listed in `requirements.txt` and installed by the `fleet` wrapper.

```sh
# default: mqtt_plain only
./fleet telemetry [--receiver HOST] [--yes] [--dry-run]

# opt-in to other rows
./fleet telemetry --rows mqtt_plain,mqtt_stls [--receiver HOST] [--certs DIR] [--yes]

# full set
./fleet telemetry --rows mqtt_plain,mqtt_stls,mqtt_mtls,http_plain,http_tls \
    [--receiver HOST] [--certs DIR] [--yes] [--dry-run]
```

Flags:

| Flag | Default | Description |
|------|---------|-------------|
| `--rows R,R` | `mqtt_plain` | comma-separated transport rows to run |
| `--receiver HOST` | `BB_TEST_RECEIVER` | broker/receiver host |
| `--certs DIR` | `BB_TEST_CERTS` | dir with `ca.crt` / `client.crt` / `client.key` |
| `--broker-timeout SEC` | 15 | seconds to wait for broker message receipt |
| `--topic-prefix PREFIX` | `metrics` | MQTT topic prefix (`BB_PUB_TOPIC_PREFIX`) |

Environment variables (can be overridden by flags):
- `BB_TEST_RECEIVER` — telemetry receiver / broker host
- `BB_TEST_CERTS` — directory with `ca.crt` / `client.crt` / `client.key`

**Mutating (PATCHes /api/telemetry) / operator-only.**  A real broker round-trip
requires the device to be pointed at a live broker; use `--dry-run` for intent
verification without mutating device state.

### `ota`

OTA firmware operations.  All sub-operations require `--yes` or `--dry-run` for
mutating actions.

```sh
# push a local .bin to devices
./fleet ota push --bin .pio/build/bitaxe-601/firmware.bin \
    --target v0.70.0 [--yes] [--dry-run] [--mark-valid]

# trigger pull-OTA
./fleet ota pull [--mode auto|pull] [--target VERSION] [--yes]

# mark current image valid (cancel rollback timer)
./fleet ota mark-valid [--yes]

# rollback to previous image
./fleet ota recover [--yes]

# read OTA status + progress (read-only)
./fleet ota status

# verify version + mining state post-settle
./fleet ota verify --target v0.70.0 [--settle [SEC]]
```

**Destructive / operator-only.**

#### Post-push readiness grace

After a push, `fleet ota push` always waits for the device to finish rebooting
and for mining to spin up before judging success or failure (up to
`_POST_OTA_READINESS_TIMEOUT` seconds, default 120 s).  This is unconditional —
the OTA reboot always needs a grace window for the miner task to initialise and
hashrate to come up, regardless of whether `--settle` was passed.

`reset_reason='software'` after an OTA reboot is **expected** — it is not
treated as a failure.

#### PENDING vs FAILED

`fleet ota push` distinguishes two post-reboot states:

| State | Condition | Exit |
|-------|-----------|------|
| **PENDING** | Mining healthy (hashrate > 0, no abnormal reset), but firmware has not yet self-validated (`validated:false` in `/api/health`). | 0 — success |
| **FAILED** | Abnormal reset (`panic`/`brownout`/`wdt`), mining never resumed, wrong version, or device unreachable. | 1 — failure |

The default policy is **PENDING = success**: the firmware self-validates on the
first accepted share, or after the 15-minute stratum-auth timer in
`ota_validator`.  The harness does not fight this.

#### `--mark-valid` (force validation)

Pass `--mark-valid` to force-validate the image immediately after readiness
confirms healthy mining:

```sh
./fleet ota push --bin firmware.bin --target v0.70.0 --yes --mark-valid
```

This POSTs `/api/update/mark-valid` via Guard, then verifies `validated:true` in
`/api/health`.  On success the result is **VALIDATED** (exit 0).  Use this for
targeted test runs where the pushed build must stick and not roll back to the
previous version.

**Auto-archive on push:** `fleet ota push` automatically archives the sibling
`.elf` file (`.pio/build/<env>/firmware.elf` alongside `firmware.bin`) into the
ELF archive store before uploading.  This means any panic from the pushed build
can be decoded with `fleet decode <host>` without any manual archival step.

### `elf`

ELF archive management.  The archive lives at `.elf-archive/` in the repo root
(gitignored).  Each entry consists of `<sha256>.elf` + `<sha256>.json` (sidecar
with board, version, archived_at).

#### SHA format: archive key vs panic prefix

The archive key is the full 64-hex-char `SHA256(elf_file_bytes)`.  This is the
same hash that `esp_app_desc_t.app_elf_sha256` holds in the firmware image (and
what `esptool image_info` reports as "ELF file SHA256").

The `/api/diag/panic` response carries `app_sha256` as a **truncated** hex string
whose length is `CONFIG_APP_RETRIEVE_LEN_ELF_SHA` (range 8–64, default **9**).
Observed live: `"b268e2426"` (9 chars).

`fleet decode` and `fleet elf list` resolve archived ELFs via **prefix match**:
```
archive_key.startswith(panic_app_sha256)   # e.g. full[0:9] == "b268e2426"
```

9 hex chars ≈ 4.5 bytes of SHA256; collision probability is negligible for any
realistic build corpus.

#### `fleet elf archive`

Manually archive a firmware ELF (e.g. from a serial flash).

```sh
./fleet elf archive .pio/build/esp32-wroom32/firmware.elf \
    --board esp32-wroom32 --version v0.71.0
```

**Auto-metadata from `esp_app_desc_t` (TA-461):** when `--board` or `--version`
are omitted, the harness parses the `esp_app_desc_t` struct embedded in the ELF
(magic `0xABCD5432`) and populates `board` from `project_name` (e.g.
`taipanminer-esp32-wroom32`) and `version` from the version string.  `build_time`
is set to the combined `build_date + build_time` from the struct.  Caller-supplied
`--board`/`--version` always take precedence.

```
Archived: .pio/build/esp32-wroom32/firmware.elf
  sha256     : 11321471cfe45e2bf89a03dc7c2c750041081186a301acc1ec142e85b488a997
  board      : taipanminer-esp32-wroom32
  version    : dev-main-bb-7043c1e
  build_time : Jun 26 2026 16:26:09
  (board auto-populated from esp_app_desc_t)
  (version auto-populated from esp_app_desc_t)
```

#### `fleet elf list`

Show all archived ELFs with metadata and an IN-USE column cross-referenced
against the live fleet's running builds (`/api/diag/panic` short sha).

```sh
./fleet elf list [--hosts H,H] [--board CLASS]
```

Output columns: SHA256 prefix, board, version, dirty flag, archived timestamp,
ELF size, IN-USE (yes/no/? when fleet is unreachable).

#### `fleet elf prune`

Remove old ELF entries.  Two modes:

**Budget prune (default):** removes oldest entries exceeding the keep count or
max-age window.

```sh
./fleet elf prune --keep 10
./fleet elf prune --max-age 30d
./fleet elf prune --keep 10 --max-age 7d --dry-run
```

**Fleet-aware GC (`--in-use`):** discovers the fleet, collects running build
shas, and removes archived ELFs that are NOT currently running on any device.

```sh
./fleet elf prune --in-use [--hosts H,H] [--dry-run] [--yes]
```

**Two mandatory safety guards:**

1. **Grace window** (`--grace-keep N`, default 5): the N most-recently archived
   entries are ALWAYS protected regardless of in-use status.  Prevents a
   crashed-then-rolled-back build's ELF from being deleted.

2. **Conservative on incomplete discovery**: if any fleet target is unreachable
   and `--hosts` was not given, the prune is REFUSED.  When `--hosts` provides
   an authoritative set, the prune proceeds with a warning about unreachable
   devices.

| Flag | Default | Description |
|------|---------|-------------|
| `--keep N` | 20 | keep N most-recent entries |
| `--max-age DUR` | — | delete entries older than DUR (`30d`, `7d`, `24h`) |
| `--in-use` | false | fleet-aware GC: delete ELFs not running on any device |
| `--grace-keep N` | 5 | always protect N most-recently archived entries |
| `--hosts H,H` | mDNS | authoritative fleet set for `--in-use` GC |
| `--dry-run` | false | show what would be deleted without deleting |
| `--yes` | false | skip confirmation prompt |

**Retention default:** keep 20 entries; prune-on-write fires after every
`fleet ota push` and `fleet elf archive` call.

### `decode`

Decode a panic backtrace from a live device using an archived ELF.

```sh
# auto-discover ELF from archive (matches app_sha256 from /api/diag/panic)
./fleet decode 172.16.1.81

# explicit ELF override (e.g. for a serial-flashed build not in the archive)
./fleet decode 172.16.1.81 --elf .pio/build/esp32-wroom32/firmware.elf

# explicit toolchain override (auto-detected from ~/.platformio by default)
./fleet decode 172.16.1.81 --toolchain-path /custom/xtensa-esp-elf-addr2line
```

**Toolchain selection:** XTENSA chips (ESP32, S2, S3) use
`xtensa-esp-elf-addr2line`; RISCV chips (C3, C6, H2) use
`riscv32-esp-elf-addr2line`.  Both are searched under
`~/.platformio/packages/toolchain-*/bin/` then `PATH`.  Override with
`FLEET_ADDR2LINE` env var or `--toolchain-path`.

**Read-only:** never modifies device state or the archive.

**Phase 2 (follow-up):** rebuild-from-commit to reproduce any missing ELF.
Live-crash decode (triggering a panic on a test device) is a manual follow-up.

Output:

```
Panic decode for 172.16.1.81
  ELF     : .elf-archive/<sha>.elf
  arch    : xtensa
  task    : httpd
  cause   : 28 (LoadProhibited)
  sha256  : b268e2426 (truncated; 9 chars)

  LABEL      PC             FUNCTION @ FILE:LINE
  -----------------------------------------------------------------------
  exc_pc     0x004008b385   crash_fn @ /components/main.c:42
  bt[0]      0x004008b351   caller @ /components/util.c:7
  ...
```

When `available=false` and there is no backtrace/pc, the command prints
`no panic available` and exits 0.  When no archived ELF matches the panic sha,
it prints a clear actionable message:
```
ERROR: no archived ELF for 'b268e2426'; reflash with a tracked build
       (fleet ota push) or pass --elf <path>
```

---

## Ad-hoc API access

`describe` and `call` together provide a workflow for exploring and invoking
arbitrary API endpoints without writing bespoke scripts.

### Workflow: describe then call

**Step 1 — discover what endpoints exist:**

```sh
./fleet describe --hosts 172.16.1.81
```

```
  PATH                                     METHODS
  ---------------------------------------------------------------
  /api/diag/heap                           GET
  /api/health                              GET
  /api/info                                GET
  /api/settings                            GET, PATCH
  ...
```

**Step 2 — learn the schema for an endpoint:**

```sh
./fleet describe /api/settings --hosts 172.16.1.81
```

```
PATCH /api/settings  [172.16.1.81]

  Request body:
    FIELD                            TYPE           REQ  NOTES
    ----------------------------------------------------------------------
    led_heartbeat_en                 boolean
    display_en                       boolean
    hostname                         string
    ...

  200 response:
    FIELD                            TYPE           REQ  NOTES
    ...
```

**Step 3 — inspect one method in detail (raw JSON schema):**

```sh
./fleet describe /api/settings PATCH --json --hosts 172.16.1.81
```

**Step 4 — read-only GET:**

```sh
./fleet call GET /api/diag/heap --hosts 172.16.1.81
# extract just the fields you care about
./fleet call GET /api/diag/heap --fields internal.free --hosts 172.16.1.81
```

**Step 5 — mutating request, dry-run first:**

```sh
# validate the body and log intent, NO HTTP sent
./fleet call PATCH /api/settings --json '{"led_heartbeat_en":false}' \
    --hosts 172.16.1.81 --dry-run
```

```
[DRY-RUN] 172.16.1.81: PATCH /api/settings body={"led_heartbeat_en": false}
```

**Step 6 — live mutation with `--yes`:**

```sh
./fleet call PATCH /api/settings --json '{"led_heartbeat_en":false}' \
    --hosts 172.16.1.81 --yes
```

**Validation failure example** (wrong type → caught before sending):

```sh
./fleet call PATCH /api/settings --json '{"display_en":"notabool"}' \
    --hosts 172.16.1.81 --dry-run
```

```
ERROR [172.16.1.81]: body does not match PATCH /api/settings schema:
  display_en: 'notabool' is not of type 'boolean'
  Hint: run ./fleet describe /api/settings PATCH to see the expected shape.
```

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
| `--settle [SEC]` | off | opt-in warmup settle; bare = criteria default (120 s) |
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
| `hashrate_floor_pct` | 80.0 | % of `expected_ghs` floor (all mining boards; per-class override in profiles.yaml) |
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
| `hashrate_floor_pct` | % of `expected_ghs` floor for hashrate detector (all mining boards) |

---

## Results

`--out-json PATH` writes a JSON file with per-test status, detail, and metrics.
`--out-junit PATH` writes JUnit XML for CI integration.
`--baseline PATH` compares the current run against a prior JSON result and prints regressions.

---

## Suite CI safety

| Suite | CI-safe | Notes |
|-------|---------|-------|
| `describe` | yes | read-only; fetches `/api/openapi.json` |
| `call GET` | yes | read-only |
| `call PATCH/POST/PUT/DELETE` | no | mutating; requires `--yes` or `--dry-run` |
| `functional` | yes | read-only; validates OpenAPI responses |
| `probe-endpoints` (default) | yes | read-only; GET-only by default |
| `probe-endpoints --include-mutating` | no | probes mutating methods; requires `--yes` |
| `probe-endpoints --include-streaming` | no | holds httpd workers; operator use |
| `soak` | no | long-duration; operator use |
| `stress` | no | applies load; may expose instability |
| `faults` | no | destructive; requires `--yes` |
| `telemetry` | no | mutates transport config; requires receiver + certs |
| `ota push/pull/recover` | no | flashes firmware; requires `--yes` |
| `ota status/verify` | yes | read-only |
| `decode` | yes | read-only; fetches `/api/diag/panic` |
| `elf list` | yes | read-only; queries fleet + local archive |
| `elf archive` | yes | local write only; no device mutation |
| `elf prune` | no | deletes local archive files; requires `--yes` or `--dry-run` |

---

## Identity verification

OTA push and other destructive operations call `fleetlib.ota` which re-fetches
`/api/info` at the target IP immediately before acting and verifies board identity.
This guards against stale-IP hazards on DHCP churn (a reassigned IP otherwise risks
flashing the wrong board).

---

## /api/info field layout (B1-360)

B1-360 moved 13 static build fields under `info["build"]`:
`version`, `idf_version`, `build_date`, `build_time`, `project_name`,
`chip_model`, `chip_revision`, `cores`, `cpu_freq_mhz`, `flash_size`,
`app_size`, `board`, `app_sha256`.

Dynamic runtime fields (`mac`, `uptime_ms`, `heap_internal`, `network`,
`ota_validated`, `reset_reason`, …) remain top-level.

The harness reads all moved fields via `fleetlib.client.info_field(info, key)`,
which reads `info["build"][key]` exclusively (requires B1-360 firmware).
Discovery (`board`, `version`), OTA version checks, status columns, and ELF
IN-USE correlation (`app_sha256`) all go through this accessor.
