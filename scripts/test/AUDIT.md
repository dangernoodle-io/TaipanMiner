# TA-433 — Audit of `scripts/test/` fleet/monitor scripts

Status: audit complete; consolidation plan pending approval.
Date: 2026-06-25.

## 1. Inventory (32 files, two populations)

The pile spans two git states:

- **12 tracked in `main`:** `README.md`, `fleet_mqtt_reset.sh`, `fleetmon.py`, `fleetpull.py`,
  `fleetsoak.py`, `ota_push.sh`, `otaflash.py`, `pulltest.py`, `run_matrix.sh`,
  `soak_monitor.sh`, `tlsrow.py`, `updatecheck.py`.
- **20 untracked** (WIP in a working tree, snapshotted into this worktree): `asic_pow2.py`,
  `asic_power_watch.py`, `broker_outage_repro.py`, `browser_repro.py`, `canary_76.py`,
  `corepin_validate.py`, `fleet_knob_flash.py`, `fleet_soak.py`, `heap_watch.py`,
  `migrate_board.py`, `ota_push70.py`, `overnight_watch.py`, `probe_dashboard.py`,
  `pubdisabled_reach.py`, `reflash_fleet.py`, `repoint_fleet.py`, `soak70.py`,
  `soak_monitor.py`, `socket_soak.py`, `validate70.py`.

30 are executable scripts (Python + a few `.sh`); `README.md` documents them.

## 2. Core findings

### 2a. ~80%+ duplication
Every script reimplements the same primitives with small variations:

| Primitive | Reimplemented in | Variation |
|---|---|---|
| HTTP GET→json (`gj`/`get`) | ~27 scripts | arg order, timeout 4–10s |
| Fleet IP roster | ~18 scripts | dict/list/tuple, 5–15 boards, all hardcoded |
| OTA push/pull/mark-valid | ~9 scripts | inline vs functions, poll windows 120–360s |
| Retry/poll loop | all | `range(N)`+`sleep`, N and interval ad hoc |
| Anomaly log + `sys.exit(1)` | all monitors | prefixes `ANOMALY`/`!`/`!!`/`<<<`, exit 0/1/2 |
| Board-type logic | scattered | no registry; `NOPSRAM`/`PULL_SKIP`/per-IP specials |

### 2b. Inconsistent threshold values (no standard exists)
Poll interval 3s–300s; soak window 4min–8h; reboot tolerance 5/20/30s; heap floor enforced
in exactly one script (2048 B, far too low). The KB has no canonical soak criteria beyond
**TA-269** draft (≥1h, share-rate ≥80%, `free_heap_min` >50 KB, 0 reboots). We are defining
the standard, not inheriting one.

### 2c. Breakage from B1-310 (structured heap)
`/api/info` dropped flat heap fields (`free_heap`, `heap_minimum_ever`,
`heap_largest_free_block`) for structured `heap.internal.{free,min_free,largest_block}`.
**Broken:** `heap_watch.py`, `canary_76.py`, `overnight_watch.py`, `fleetmon.py`,
`soak_monitor.py`, `fleet_knob_flash.py`. The fix is a shared `get_info` reading structured
fields with flat fallback — but the systemic fix is a contract test (see plan).

### 2d. Dead-endpoint fossils
`probe_dashboard.py` probes `/api/power` and `/api/fan` — **deleted** (merged into
`/api/sensors`). It is a v0.31.0 crash-hunt fossil. `browser_repro.py` references them too.

### 2e. Config-surface sprawl (everything hardcoded)
Fleet rosters, broker URIs (`mqtt://172.16.1.5:1883`), cert paths
(`../../../mqtt-stack/certs`), bin paths (`/tmp/fleet2`, `/tmp/vcore-650-*.bin`), target
versions (`ge79f1ba`, `vcorefix4`), InfluxDB token (`dev-token-please-change`, a low-grade
secret) all live inline. None is config-driven.

### 2f. Stale-IP safety hazard (real near-miss, per TA-433 notes)
Hardcoded IP rosters go stale on DHCP churn; `172.16.1.67`→dead, `172.16.1.12`→reassigned.
A reassigned IP nearly caused an OTA flash of the wrong board. Destructive ops must
re-verify board identity at the target immediately before acting.

### 2g. Resolved investigation (TA-433 "INVESTIGATE" note)
`dns-sd -B _http._tcp` finds nothing yet `taipan discover` works because devices advertise
under **`_taipanminer._tcp`**, not `_http._tcp`. Discovery uses native `python-zeroconf` on that
service type (+ explicit `--hosts`). No coupling to the `taipan` binary.

### 2h. The device serves its own OpenAPI spec — the missing SSOT
Each miner serves a firmware-generated **OpenAPI 3.1.0** doc at `GET /api/openapi.json` (via
breadboard `bb_openapi`). Verified live: 42 paths, rich inline response/request schemas with
types (incl. structured `heap_internal.*`, typed settings-PATCH toggles). It is **board-class
aware** — bitaxe advertises `/api/diag/asic`, wroom32 advertises `/ws`. Implications:

- **Client SSOT:** the harness fetches each device's served spec at runtime — no hand-maintained
  field lists. "On-demand fields" = read any field the spec declares.
- **Live contract validation:** validate each response against that device's own served schema,
  catching field drift *in the field*, not just CI. (B1-310 would have tripped this.)
- **Board-class gating for free:** the device declares its own surface; no hand-maintained
  capability table needed for *which endpoints/fields exist*.

### 2i. Contract drift guard largely already exists (C side)
breadboard ships `bb_openapi_validate()` (JSON-Schema validator) and host tests
`test_openapi_emit.c` / `test_route_schemas.c` / `test_route_fidelity.c`. TM has golden tests in
`test_routes_json.c` for 7 of ~11 emitters. **Gap = 4 uncovered TM emitters**:
`emit_diag_bench_json`, `emit_mining_rates_json`, `emit_pool_pub_json`,
`emit_sensors_miner_json`. The CI contract work is adding fidelity/schema tests for those four to
the existing `native-test` job — not a new Python validator.

## 3. Per-script disposition

Legend: **FOLD** = logic absorbed into the shared harness, script deleted · **DELETE** =
removed outright (fossil/superseded) · **SCENARIO** = becomes a named fault-injection or
suite scenario · **UPDATE** = rewritten.

| Script | Disposition | Target / note |
|---|---|---|
| `soak_monitor.py` | FOLD | canonical `soak` (heap/reset/publisher/downtime) |
| `soak_monitor.sh` | DELETE | duplicate of `soak_monitor.py` intent |
| `fleet_soak.py` | FOLD | `soak` (reboot/rollback/mqtt) |
| `fleetsoak.py` | FOLD | `soak` transport rotation; fix TA-426 (false sinks=0: poll until mqtt up) |
| `heap_watch.py` | FOLD | `soak` heap monitor (fixes B1-310) |
| `overnight_watch.py` | FOLD | `soak` (fixes B1-310) |
| `canary_76.py` | FOLD | `soak` single-board target filter |
| `pubdisabled_reach.py` | FOLD | `soak` with publisher-disabled toggle |
| `fleetmon.py` | FOLD | `fleet status` (fixes B1-310) |
| `asic_pow2.py` | FOLD | `soak` ASIC-power profile |
| `asic_power_watch.py` | FOLD | `soak` ASIC-power profile |
| `socket_soak.py` | SCENARIO | fault-injection: socket exhaustion |
| `broker_outage_repro.py` | SCENARIO | fault-injection: broker outage (docker/mosquitto) |
| `corepin_validate.py` | SCENARIO | fault-injection: broker-outage cycles (TA-432 landed) |
| `browser_repro.py` | FOLD | `stress` browser-like load generator |
| `probe_dashboard.py` | DELETE | dead `/api/power`,`/api/fan`; superseded by functional+stress suites |
| `updatecheck.py` | FOLD | `stress` update-check heap stress |
| `reflash_fleet.py` | FOLD | `ota` orchestration (mDNS roster + identity verify) |
| `fleet_knob_flash.py` | FOLD | `ota` canary-order roll |
| `fleetpull.py` | FOLD | `ota` serialized pull driver |
| `pulltest.py` | FOLD | `ota` pull-fragmentation heap probe |
| `otaflash.py` | FOLD | `ota` push wrapper |
| `ota_push.sh` | DELETE | duplicate of `otaflash.py` |
| `ota_push70.py` | DELETE | board-specific vcorefix one-off; pattern → `ota`+`soak` |
| `validate70.py` | DELETE | board-specific vcorefix one-off |
| `soak70.py` | DELETE | board-specific vcore soak one-off |
| `migrate_board.py` | FOLD | `fleet` single-board flash + telemetry enable |
| `repoint_fleet.py` | FOLD | `fleet` broker repoint (settings op) |
| `fleet_mqtt_reset.sh` | FOLD | `fleet` reset-to-plaintext + NVS clear (destructive → gated) |
| `tlsrow.py` | FOLD | `transport-matrix` functional suite (cert + InfluxDB deps) |
| `run_matrix.sh` | FOLD | `transport-matrix` driver |
| `README.md` | UPDATE | document new harness |

Net: ~22 FOLD, 7 DELETE, 3 SCENARIO, 1 UPDATE. No script survives as-is.

## 4. Proposed canonical soak criteria (defaults; board-profile overridable)

Derived from de-facto values + TA-269. These become committed config, the single source of truth.

| Criterion | Default | Override scope |
|---|---|---|
| poll interval | 60 s | profile (3 s ASIC-power) |
| soak duration | 1 h | flag |
| `heap.internal.free` floor | ≥ 50 KB | per board class |
| heap leak | `min_free` non-declining over window | global |
| reboot detection | uptime regression > 30 s | global |
| `reset_reason` | not in {panic,task_wdt,int_wdt,brownout} | global |
| `wdt_resets` | must not increase | global |
| publisher | `mqtt.connected && last_publish_ok` within 6 polls | profile |
| hashrate | ≥ 80% `expected_ghs` | ASIC/CPU profile |
| vcore (ASIC) | ≥ 500 mV; `vcore_restart_count` flat | ASIC profile |
| version | running == target (no rollback) | flag |
| downtime | ≤ 4 consecutive missed polls | global |

See the consolidation plan for the target architecture, suite taxonomy, safety model, and
CI contract tier.
