#!/usr/bin/env python3
"""fleet.py — TaipanMiner fleet test harness CLI (TA-433).

Subcommands:
  discover         — discover devices via mDNS, print table
  status           — GET /api/info + /api/health per device, print summary
  probe-endpoints  — spec-driven endpoint crash probe (TA-469)
  describe         — inspect the served OpenAPI spec (paths, request/response schemas)
  call             — make an arbitrary API request (safety-gated for mutating methods)
  logs             — retrieve device kernel log via GET /api/logs (SSE)
  functional       — run functional suite (schema validation per device)
  soak             — run soak suite (long-running monitor)
  stress           — run stress suite (concurrent load)
  faults           — run fault-injection suite
  telemetry        — run telemetry transport suite
  ota              — OTA operations (push/pull/mark-valid/recover/status/verify)
  decode           — decode a panic backtrace from a live device using an archived ELF
  elf              — ELF archive management (archive/list/prune)
"""
from __future__ import annotations

# Python version floor (TA-450): fail fast with a clear message.
# CI target is 3.11; walrus operator (:=) requires 3.8+ and dataclasses 3.7+,
# but 3.11 is the tested baseline and matches CI.
import sys as _sys
if _sys.version_info < (3, 11):
    print(
        f"ERROR: fleet requires Python >= 3.11 "
        f"(found {_sys.version_info.major}.{_sys.version_info.minor}). "
        f"Install Python 3.11+ and retry.",
        file=_sys.stderr,
    )
    _sys.exit(1)

import argparse
import logging
import os
import sys
import time

# Ensure fleetlib and suites are importable when run directly
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fleetlib.criteria import Criteria, load as load_criteria
from fleetlib.profiles import Profiles
from fleetlib.safety import Guard
from fleetlib.results import ResultSet
from suites import SuiteContext, SettleConfig, resolve_devices

# Import shared helpers from core (moved there; keep private aliases for
# backward compatibility so existing cmd_* bodies don't need changes).
from core import (
    SETTLE_BARE as _SETTLE_BARE,
    emit_resolve_warnings as _emit_resolve_warnings,
    no_devices_message as _no_devices_message,
    unwrap_devices as _unwrap_devices,
    ota_guard as _ota_guard,
    ota_settle as _ota_settle,
    write_outputs as _write_outputs,
    build_suite_context as _build_context,
)


# ---------------------------------------------------------------------------
# Backward-compat aliases: tests reach into fleet.* by private name.
# Keep these thin wrappers so existing tests don't need changes.
# ---------------------------------------------------------------------------

def _add_common_flags(p) -> None:
    from core import add_common_flags
    add_common_flags(p)


def _build_context(args, suite_name: str = "fleet"):
    from core import build_suite_context
    return build_suite_context(args, name=suite_name)


def _build_parser():
    """Build the root argparse parser (backward compat for tests)."""
    from cli import _build_cli_parser
    return _build_cli_parser()


# _ota_guard, _ota_settle, _write_outputs already imported from core above.


def _parse_duration(s: str) -> float:
    """Parse '30s', '5m', '1h', or bare seconds (int/float)."""
    s = s.strip()
    if s.endswith("s"):
        return float(s[:-1])
    if s.endswith("m"):
        return float(s[:-1]) * 60
    if s.endswith("h"):
        return float(s[:-1]) * 3600
    return float(s)


# ---------------------------------------------------------------------------
# Subcommand handlers
# ---------------------------------------------------------------------------

def cmd_discover(args) -> int:
    """Discover devices via mDNS and print a table."""
    print("Discovering devices (mDNS _taipanminer._tcp.local.)…")
    result = resolve_devices(args)
    devices = _unwrap_devices(result)
    if not devices:
        print(_no_devices_message(result))
        return 0
    _print_device_table(devices, extra_headers=["uptime"])
    return 0


def cmd_status(args) -> int:
    """Fetch /api/info + /api/health for each device and print summary."""
    from fleetlib.client import Client, TIMEOUT_INFO, TIMEOUT_HEALTH, info_field

    result = resolve_devices(args)
    devices = _unwrap_devices(result)
    if not devices:
        print(_no_devices_message(result), file=sys.stderr)
        return 1

    print(f"{'HOST':<20} {'BOARD':<20} {'VERSION':<16} {'UPTIME':>12}  {'HEAP FREE':>12}  HEALTH")
    print("-" * 90)
    all_ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
        health = c.get_json("/api/health", timeout=TIMEOUT_HEALTH)

        if info is None:
            print(f"{d.ip:<20} {'??':<20} {'??':<16} {'UNREACHABLE':>12}")
            all_ok = False
            continue

        uptime_ms = info.get("uptime_ms", 0)
        uptime_str = _fmt_uptime(uptime_ms)

        heap = c.get_json("/api/diag/heap", timeout=TIMEOUT_INFO)
        heap_free: object = None
        if heap is not None:
            heap_free = (heap.get("internal") or {}).get("free")
        if heap_free is None:
            heap_free = info.get("free_heap")
        heap_str = f"{heap_free:,}" if isinstance(heap_free, int) else "??"

        board = info_field(info, "board") or d.board
        version = info_field(info, "version") or d.version

        if health is None:
            health_str = "??"
        elif health.get("ok") is True:
            health_str = "ok"
        elif health.get("ok") is False:
            health_str = "unhealthy"
            all_ok = False
        else:
            health_str = "??"

        print(f"{d.ip:<20} {board:<20} {version:<16} {uptime_str:>12}  {heap_str:>12}  {health_str}")

    return 0 if all_ok else 1


def cmd_probe_endpoints(args) -> int:
    """Spec-driven endpoint crash probe (TA-469).

    Enumerates GET paths from /api/openapi.json, probes each once, and
    detects uptime regressions (crash/reboot) after each hit.  Mutating
    methods (POST/PUT/PATCH/DELETE) and streaming endpoints (/api/logs,
    /api/diag/events, /ws) are skipped by default.
    """
    from fleetlib.client import Client, TIMEOUT_INFO
    from fleetlib.spec import Spec

    # Endpoints that block indefinitely — skip unless opt-in
    _STREAMING = {"/api/logs", "/api/diag/events", "/ws"}
    # Mutating HTTP methods — skip unless --include-mutating
    _MUTATING = {"post", "put", "patch", "delete"}

    include_mutating = getattr(args, "include_mutating", False)
    include_streaming = getattr(args, "include_streaming", False)

    result = resolve_devices(args)
    devices = _unwrap_devices(result)
    if not devices:
        print(_no_devices_message(result), file=sys.stderr)
        return 1

    all_ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        print(f"\nProbing {d.ip} ({d.board} {d.version})")

        # Fetch spec
        spec_doc = c.get_json("/api/openapi.json", timeout=TIMEOUT_INFO)
        if spec_doc is None:
            print(f"  ERROR: could not fetch /api/openapi.json — device unreachable?")
            all_ok = False
            continue
        spec = Spec(spec_doc)

        # Collect probe targets: safe GETs only by default
        targets = []
        for path in sorted(spec.paths()):
            methods = spec.methods(path)
            is_streaming = path in _STREAMING
            has_get = "get" in methods

            if is_streaming and not include_streaming:
                continue
            if not has_get and not include_mutating:
                continue
            # Determine method to use
            if has_get and (not is_streaming or include_streaming):
                targets.append((path, "GET"))
            elif include_mutating:
                for m in methods:
                    if m in _MUTATING:
                        targets.append((path, m.upper()))
                        break

        if not targets:
            print("  no probeable endpoints found")
            continue

        # Read baseline uptime
        def _uptime(client: Client):
            info = client.get_json("/api/info", timeout=TIMEOUT_INFO)
            if info is None:
                return None
            return info.get("uptime_ms")

        baseline_uptime = _uptime(c)
        if baseline_uptime is None:
            print("  ERROR: could not read baseline uptime from /api/info")
            all_ok = False
            continue

        print(f"  baseline uptime: {_fmt_uptime(baseline_uptime)}  "
              f"({len(targets)} endpoints to probe)")
        print()
        print(f"  {'ENDPOINT':<40} {'STATUS':>8}  RESULT")
        print(f"  {'-' * 60}")

        prev_uptime = baseline_uptime
        for path, method in targets:
            status_code = None
            result_str = "ok"
            if method == "GET":
                resp = c.get_json(path, timeout=TIMEOUT_INFO)
                if resp is None:
                    # Could be a genuine error response (e.g. 404) or unreachable
                    status_code, _ = c.request("GET", path, timeout=TIMEOUT_INFO)
                    result_str = "err" if status_code is not None else "unreachable"
                else:
                    status_code = 200
            else:
                status_code, _ = c.request(method, path, timeout=TIMEOUT_INFO)
                result_str = "ok" if (status_code is not None and status_code < 500) else "err"

            # Re-read uptime to detect crash
            new_uptime = _uptime(c)
            if new_uptime is None:
                result_str = "CRASH (unreachable after)"
                all_ok = False
            elif new_uptime < prev_uptime:
                result_str = f"CRASH (uptime regressed: {_fmt_uptime(prev_uptime)} -> {_fmt_uptime(new_uptime)})"
                all_ok = False
            else:
                prev_uptime = new_uptime

            status_str = str(status_code) if status_code is not None else "??"
            print(f"  {path:<40} {status_str:>8}  {result_str}")

        # Final uptime
        final_uptime = _uptime(c)
        if final_uptime is not None:
            print(f"\n  final uptime: {_fmt_uptime(final_uptime)}")
            if final_uptime < baseline_uptime:
                print(f"  WARNING: overall uptime regressed — device rebooted during probe")
                all_ok = False
            else:
                print(f"  no crashes detected")

    return 0 if all_ok else 1


def cmd_suite(args, suite_name: str) -> int:
    """Generic suite runner."""
    from suites import load_suite, SUITES

    if suite_name not in SUITES:
        print(f"Suite '{suite_name}' is not yet implemented.")
        return 1

    try:
        mod = load_suite(suite_name)
    except ImportError as exc:
        # Suite module doesn't exist yet (soak/stress/faults/telemetry are stubs)
        print(f"Suite '{suite_name}' not available: {exc}")
        return 1

    ctx = _build_context(args, suite_name=suite_name)
    _resolve_result = resolve_devices(args)
    ctx.devices = _unwrap_devices(_resolve_result)

    # Forward all suite-specific flags into ctx.extra generically.
    # COMMON_DEST enumerates every dest that _add_common_flags and the main parser register,
    # so anything left over must belong to the suite subparser.
    _COMMON_DEST = {
        "hosts", "discover_timeout", "board",
        "fields", "gates", "skip_gates", "out_json", "out_junit", "baseline", "criteria",
        "settle", "dry_run", "yes",
        "log_level", "subcommand", "func",
        "metrics_mqtt_url", "metrics_topic", "no_publish_metrics",
    }
    ctx.extra = {k: v for k, v in vars(args).items() if k not in _COMMON_DEST}

    if not ctx.devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    print(f"Running {suite_name} suite on {len(ctx.devices)} device(s)…")

    rs = mod.run(ctx)

    # Print summary
    summary = {
        "pass": sum(1 for r in rs.results if r.status == "pass"),
        "fail": sum(1 for r in rs.results if r.status == "fail"),
        "skip": sum(1 for r in rs.results if r.status == "skip"),
    }
    total = sum(summary.values())
    print(f"\nResults: {total} tests — {summary['pass']} pass, {summary['fail']} fail, {summary['skip']} skip")

    # Print failures
    failures = [r for r in rs.results if r.status == "fail"]
    if failures:
        print("\nFAILURES:")
        for r in failures:
            print(f"  FAIL  {r.name}")
            if r.detail:
                print(f"        {r.detail}")

    # Baseline comparison
    if ctx.baseline:
        regressions = rs.compare_baseline(ctx.baseline)
        if regressions:
            print("\nBASELINE REGRESSIONS:")
            for reg in regressions:
                print(f"  {reg}")

    # Metrics publishing — default ON when a broker is configured; opt-out via --no-publish-metrics.
    # Never fails the run: publish errors are warned, exit code is unchanged.
    no_publish = getattr(args, "no_publish_metrics", False)
    if not no_publish:
        broker_url = (
            getattr(args, "metrics_mqtt_url", None)
            or os.environ.get("BB_TEST_METRICS_BROKER")
            or os.environ.get("BB_TEST_RECEIVER")
        )
        if broker_url:
            topic_prefix = getattr(args, "metrics_topic", "fleettest") or "fleettest"
            try:
                rs.push_telemetry(broker_url, topic_prefix=topic_prefix)
            except Exception as _pub_exc:
                logging.getLogger(__name__).warning(
                    "run metrics publish failed (non-fatal): %s", _pub_exc
                )
        else:
            print(
                "note: run metrics not published (no broker configured; "
                "set --metrics-mqtt-url or BB_TEST_METRICS_BROKER to enable)",
                file=sys.stderr,
            )

    return 1 if summary["fail"] > 0 else 0


def cmd_describe(args) -> int:
    """Describe the OpenAPI spec served by a device."""
    import json as _json
    from fleetlib.client import Client
    from fleetlib.spec import Spec

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    d = devices[0]
    c = Client(d.ip, getattr(d, "port", 80))
    spec_doc = c.spec
    if spec_doc is None:
        print(f"ERROR: could not fetch OpenAPI spec from {d.ip}")
        return 1

    spec = Spec(spec_doc)

    # Warn if multiple devices have differing path sets
    if len(devices) > 1:
        first_paths = set(spec.paths())
        for other_d in devices[1:]:
            oc = Client(other_d.ip, getattr(other_d, "port", 80))
            other_doc = oc.spec
            if other_doc is None:
                continue
            other_paths = set(Spec(other_doc).paths())
            if other_paths != first_paths:
                extra = other_paths - first_paths
                missing = first_paths - other_paths
                note = []
                if extra:
                    note.append(f"+{sorted(extra)}")
                if missing:
                    note.append(f"-{sorted(missing)}")
                print(f"NOTE: {other_d.ip} has a different path set than {d.ip}: {'; '.join(note)}")

    path = getattr(args, "path", None)
    method = getattr(args, "method", None)
    raw_json = getattr(args, "json_raw", False)

    if path is None:
        # Table of all paths and their methods
        paths = sorted(spec.paths())
        if not paths:
            print(f"Spec from {d.ip} has no paths.")
            return 0
        print(f"\nOpenAPI spec from {d.ip}:\n")
        print(f"  {'PATH':<40} METHODS")
        print(f"  {'-' * 60}")
        for p in paths:
            methods_str = ", ".join(m.upper() for m in spec.methods(p))
            print(f"  {p:<40} {methods_str}")
        print(f"\n  {len(paths)} endpoint(s)")
        return 0

    if not spec.has_path(path):
        print(f"ERROR: {path!r} is not in the spec served by {d.ip}.")
        print("Run ./fleet describe to list available endpoints.")
        return 1

    methods_to_show = [method.lower()] if method else spec.methods(path)
    if not methods_to_show:
        print(f"No methods found for {path} in spec from {d.ip}.")
        return 0

    for m in methods_to_show:
        print(f"\n{m.upper()} {path}  [{d.ip}]")

        req_schema = spec.request_schema(path, m)
        if req_schema is not None:
            print("\n  Request body:")
            if raw_json:
                print(_json.dumps(req_schema, indent=4))
            else:
                _render_schema(req_schema, indent=4)
        else:
            print("  Request body: (none)")

        resp_schema = spec.response_schema(path, m)
        if resp_schema is not None:
            print("\n  200 response:")
            if raw_json:
                print(_json.dumps(resp_schema, indent=4))
            else:
                _render_schema(resp_schema, indent=4)
        else:
            print("  200 response: (no schema)")

    return 0


def _schema_type_str(schema: dict) -> str:
    """Return a short type string for a JSON Schema sub-schema."""
    if "type" in schema:
        return schema["type"]
    if "anyOf" in schema:
        parts = [s.get("type", "any") for s in schema["anyOf"]]
        non_null = [t for t in parts if t != "null"]
        nullable = len(parts) != len(non_null)
        base = " | ".join(non_null) if non_null else "any"
        return f"{base}?" if nullable else base
    return "any"


def _schema_notes(schema: dict) -> str:
    """Return a short notes string for a JSON Schema sub-schema (enum, min/max, desc)."""
    notes = []
    if "enum" in schema:
        notes.append(f"enum: {schema['enum']}")
    if "minimum" in schema:
        notes.append(f"min: {schema['minimum']}")
    if "maximum" in schema:
        notes.append(f"max: {schema['maximum']}")
    if "description" in schema:
        desc = schema["description"]
        if len(desc) > 60:
            desc = desc[:57] + "…"
        notes.append(desc)
    return ", ".join(notes)


def _render_schema(schema: dict, indent: int = 0) -> None:
    """Render a JSON Schema object as a human-readable field table (recursive)."""
    if schema is None:
        return
    props = schema.get("properties", {})
    required_set = set(schema.get("required", []))

    pad = " " * indent

    if not props:
        schema_type = schema.get("type", "any")
        print(f"{pad}(type: {schema_type})")
        return

    print(f"{pad}{'FIELD':<32} {'TYPE':<14} {'REQ':<4} NOTES")
    print(f"{pad}{'-' * 70}")

    for field, fschema in props.items():
        ftype = _schema_type_str(fschema)
        req_mark = "yes" if field in required_set else ""
        notes = _schema_notes(fschema)
        print(f"{pad}{field:<32} {ftype:<14} {req_mark:<4} {notes}")
        if fschema.get("type") == "object" and fschema.get("properties"):
            _render_schema(fschema, indent=indent + 2)


def cmd_call(args) -> int:
    """Make an arbitrary API request (safety-gated for mutating methods)."""
    import json as _json
    from fleetlib.client import Client, get_field, TIMEOUT_WRITE
    from fleetlib.spec import Spec
    from fleetlib.safety import Guard, MUTATING, DeviceUnreachable, IdentityMismatch, RefusedWithoutConfirmation

    method = args.call_method.upper()
    path = args.call_path

    # Parse body early — return before any network I/O on error
    body = None
    json_body = getattr(args, "json_body", None)
    json_file = getattr(args, "json_file", None)
    if json_body:
        try:
            body = _json.loads(json_body)
        except _json.JSONDecodeError as e:
            print(f"ERROR: --json body is not valid JSON: {e}")
            return 1
    elif json_file:
        try:
            with open(json_file) as fh:
                body = _json.load(fh)
        except FileNotFoundError:
            print(f"ERROR: --json-file not found: {json_file}")
            return 1
        except _json.JSONDecodeError as e:
            print(f"ERROR: --json-file is not valid JSON: {e}")
            return 1

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    # Fetch spec from first device for path-warning and body validation
    d0 = devices[0]
    c0 = Client(d0.ip, getattr(d0, "port", 80))
    spec_doc = c0.spec
    spec = Spec(spec_doc) if spec_doc else None

    # Warn if path unknown to served spec (non-fatal)
    if spec is not None and not spec.has_path(path):
        print(f"WARNING: {path!r} not found in served spec. Proceeding anyway.")

    # Pre-validate request body against served schema (unless --no-validate)
    if body is not None and not getattr(args, "no_validate", False) and spec is not None:
        req_schema = spec.request_schema(path, method)
        if req_schema is not None:
            try:
                import jsonschema
                validator = jsonschema.Draft202012Validator(req_schema)
                errors = list(validator.iter_errors(body))
                if errors:
                    print("ERROR: request body fails schema validation:")
                    for err in errors:
                        loc = ".".join(str(p) for p in err.absolute_path) or "<root>"
                        print(f"  {loc}: {err.message}")
                    print(f"  run ./fleet describe {path} {method} to see the expected shape")
                    return 1
            except ImportError:
                pass  # jsonschema not available; skip validation

    is_mutating = method in MUTATING
    guard = Guard(
        dry_run=getattr(args, "dry_run", False),
        confirm=getattr(args, "yes", False),
    )

    fields_raw = getattr(args, "fields", None)
    fields = [f.strip() for f in fields_raw.split(",") if f.strip()] if fields_raw else None
    out_json_path = getattr(args, "out_json", None)

    results = []
    all_ok = True

    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))

        if is_mutating:
            try:
                sentinel = guard.check(d, method, path)
            except DeviceUnreachable as e:
                print(f"ERROR: {e}")
                all_ok = False
                continue
            except IdentityMismatch as e:
                print(f"ERROR: {e}")
                all_ok = False
                continue
            except RefusedWithoutConfirmation as e:
                print(f"ERROR: {e}")
                all_ok = False
                continue

            if Guard.is_dry_run_skip(sentinel):
                print(f"DRY-RUN: would {method} {path} on {d.ip}")
                if body is not None:
                    print(f"  body: {_json.dumps(body, indent=2)}")
                results.append({"host": d.ip, "status": "dry-run", "body": body})
                continue

            status, resp_bytes = c.request(method, path, body=body)
            if status is None:
                msg = resp_bytes.decode(errors="replace")
                print(f"  {d.ip}: ERROR network error: {msg}")
                all_ok = False
                results.append({"host": d.ip, "status": None, "error": msg})
                continue

            print(f"  {d.ip}: HTTP {status}")
            try:
                resp_data = _json.loads(resp_bytes)
                print(_json.dumps(resp_data, indent=2))
                results.append({"host": d.ip, "status": status, "response": resp_data})
            except _json.JSONDecodeError:
                raw = resp_bytes.decode(errors="replace")
                print(raw)
                results.append({"host": d.ip, "status": status, "response": raw})

        else:
            # GET / HEAD / OPTIONS — no guard required
            resp_data = c.get_json(path)
            if resp_data is None:
                print(f"  {d.ip}: ERROR could not GET {path}")
                all_ok = False
                results.append({"host": d.ip, "status": None,
                                 "error": f"could not GET {path}"})
                continue

            print(f"  {d.ip}: HTTP 200")
            if fields:
                for field in fields:
                    val = get_field(resp_data, field)
                    print(f"  {field}: {val}")
            else:
                print(_json.dumps(resp_data, indent=2))
            results.append({"host": d.ip, "status": 200, "response": resp_data})

    if out_json_path:
        with open(out_json_path, "w") as fh:
            _json.dump(results, fh, indent=2)

    return 0 if all_ok else 1


def cmd_watch(args) -> int:
    """Poll a single endpoint per device and print time-series rows."""
    import csv as _csv
    import datetime
    import json as _json
    import time

    from fleetlib.client import Client, get_field, TIMEOUT_INFO
    from fleetlib.spec import Spec
    from fleetlib.expr import compile_expr, ExprError

    watch_path = args.watch_path
    fields_raw = getattr(args, "fields", None)
    fields = [f.strip() for f in fields_raw.split(",") if f.strip()] if fields_raw else None
    interval = float(getattr(args, "interval", 5.0))
    count = getattr(args, "count", None)
    duration_str = getattr(args, "duration", None)
    max_duration = _parse_duration(duration_str) if duration_str else None
    on_change = getattr(args, "on_change", False)
    until_expr_s = getattr(args, "until", None)
    any_device = getattr(args, "any_device", False)
    alert_expr_s = getattr(args, "alert", None)
    out_json_path = getattr(args, "out_json", None)
    out_csv_path = getattr(args, "out_csv", None)

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    # Compile expressions
    until_pred = None
    alert_pred = None
    if until_expr_s:
        try:
            until_pred = compile_expr(until_expr_s)
        except ExprError as e:
            print(f"ERROR: --until expression invalid: {e}", file=sys.stderr)
            return 1
    if alert_expr_s:
        try:
            alert_pred = compile_expr(alert_expr_s)
        except ExprError as e:
            print(f"ERROR: --alert expression invalid: {e}", file=sys.stderr)
            return 1

    # Spec-aware warning: check if path is served (once per device)
    for d in devices:
        try:
            c = Client(d.ip, getattr(d, "port", 80))
            spec_doc = c.spec
            if spec_doc is not None:
                spec = Spec(spec_doc)
                if not spec.has_path(watch_path):
                    print(
                        f"WARNING: {watch_path!r} not in spec served by {d.ip} "
                        f"(run ./fleet describe to list available endpoints)",
                        file=sys.stderr,
                    )
        except Exception:
            pass

    tick = 0
    t0 = time.time()
    prev_values: dict = {d.ip: {} for d in devices}
    records: list = []
    until_satisfied: set = set()

    try:
        while True:
            tick_start = time.time()

            for d in devices:
                c = Client(d.ip, getattr(d, "port", 80))
                data = c.get_json(watch_path, timeout=TIMEOUT_INFO)

                ts = datetime.datetime.now().strftime("%H:%M:%S")
                ts_iso = datetime.datetime.now().isoformat(timespec="seconds")

                if data is None:
                    print(f"{ts}  {d.ip}  ERROR", file=sys.stderr)
                    continue

                # Extract field values
                if fields:
                    fvals = {f: get_field(data, f) for f in fields}
                else:
                    fvals = None  # whole response

                # --on-change: skip if identical to previous tick
                if on_change:
                    prev = prev_values[d.ip]
                    if fields:
                        cur_snapshot = {f: fvals[f] for f in fields}
                    else:
                        cur_snapshot = data
                    if cur_snapshot == prev:
                        prev_values[d.ip] = cur_snapshot
                        continue
                    prev_values[d.ip] = cur_snapshot

                # --alert evaluation
                is_alert = alert_pred is not None and alert_pred.eval(data)

                # Build output row
                prefix = "ALERT " if is_alert else ""
                if fields:
                    parts = []
                    for f in fields:
                        v = fvals[f]
                        parts.append(f"{f}={v if v is not None else '?'}")
                    row_str = f"{prefix}{ts}  {d.ip}  {' '.join(parts)}"
                else:
                    compact = _json.dumps(data, separators=(",", ":"))
                    row_str = f"{prefix}{ts}  {d.ip}  {compact}"
                print(row_str)

                # Accumulate for file output
                if fields:
                    rec = {"ts": ts_iso, "host": d.ip, "fields": fvals}
                else:
                    rec = {"ts": ts_iso, "host": d.ip, "response": data}
                records.append(rec)

                # --until check
                if until_pred is not None and until_pred.eval(data):
                    until_satisfied.add(d.ip)

            # Check --until termination condition
            if until_pred is not None:
                all_ips = {d.ip for d in devices}
                if any_device:
                    condition_met = bool(until_satisfied)
                else:
                    condition_met = until_satisfied >= all_ips
                if condition_met:
                    print("until condition satisfied", file=sys.stderr)
                    _write_outputs(records, fields, devices, out_json_path, out_csv_path)
                    return 0

            tick += 1

            # Check count bound
            if count is not None and tick >= count:
                break

            # Check duration bound
            if max_duration is not None and (time.time() - t0) >= max_duration:
                break

            # Sleep remainder of interval
            elapsed = time.time() - tick_start
            sleep_time = max(0.0, interval - elapsed)
            time.sleep(sleep_time)

    except KeyboardInterrupt:
        print("", file=sys.stderr)
        print("interrupted", file=sys.stderr)
        _write_outputs(records, fields, devices, out_json_path, out_csv_path)
        return 0

    _write_outputs(records, fields, devices, out_json_path, out_csv_path)

    # --until was given but not satisfied within bound
    if until_pred is not None:
        print("until condition not satisfied within time/count bound", file=sys.stderr)
        return 1

    return 0


def cmd_logs(args) -> int:
    """Retrieve device kernel log via GET /api/logs (SSE)."""
    import threading

    from fleetlib.client import Client
    from fleetlib.profiles import Profiles, profile_for
    from fleetlib.sse import SSEUnavailable, stream_lines

    follow = getattr(args, "follow", False)
    duration_str = getattr(args, "duration", None)
    max_lines = getattr(args, "lines", None)
    out_path = getattr(args, "out_path", None)

    max_duration = _parse_duration(duration_str) if duration_str else None

    # Defaults when not following and no bounds given
    if not follow and max_duration is None and max_lines is None:
        max_lines = 50
        max_duration = 10.0

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    # Single-worker --follow warning: warn before holding a connection on heap-tight boards
    if follow:
        profiles = Profiles.load()
        for d in devices:
            p = profile_for(getattr(d, "board", "") or "", profiles)
            if p.single_worker:
                print(
                    f"WARNING: {d.ip} ({getattr(d, 'board', 'unknown')}) has limited httpd workers; "
                    f"--follow can saturate it and block other endpoints — "
                    f"consider --lines/--duration instead",
                    file=sys.stderr,
                )

    # Color support: assign short per-host labels when multiple devices
    use_color = len(devices) > 1 and sys.stdout.isatty()
    _COLORS = ["\033[36m", "\033[33m", "\033[35m", "\033[32m", "\033[34m", "\033[31m"]
    _RESET = "\033[0m"

    def _host_tag(ip: str) -> str:
        """Short label derived from last octet(s) of IP."""
        parts = ip.rsplit(".", 1)
        return f"[.{parts[-1]}]"

    host_tags: dict = {}
    host_colors: dict = {}
    for i, d in enumerate(devices):
        host_tags[d.ip] = _host_tag(d.ip)
        host_colors[d.ip] = _COLORS[i % len(_COLORS)] if use_color else ""

    out_fh = None
    if out_path:
        try:
            out_fh = open(out_path, "w")
        except OSError as e:
            print(f"ERROR: cannot open --out {out_path}: {e}", file=sys.stderr)
            return 1

    collected_lines: list = []

    try:
        if len(devices) == 1:
            # Single host — simple single-threaded stream
            d = devices[0]
            c = Client(d.ip, getattr(d, "port", 80))
            exit_code = _stream_one_device(
                c, d.ip, follow, max_duration, max_lines,
                prefix="", color="", reset="",
                out_fh=out_fh, collected=collected_lines,
            )
        else:
            # Multiple hosts — concurrent threads
            stop_event = threading.Event()
            results: dict = {}
            threads = []

            def _thread_target(d, idx):
                c = Client(d.ip, getattr(d, "port", 80))
                prefix = host_tags[d.ip] + " "
                color = host_colors[d.ip]
                results[d.ip] = _stream_one_device(
                    c, d.ip, follow, max_duration, max_lines,
                    prefix=prefix, color=color, reset=_RESET if color else "",
                    out_fh=out_fh, collected=collected_lines,
                    stop_event=stop_event,
                )

            for i, d in enumerate(devices):
                t = threading.Thread(target=_thread_target, args=(d, i), daemon=True)
                threads.append(t)
                t.start()

            try:
                for t in threads:
                    t.join()
            except KeyboardInterrupt:
                stop_event.set()
                for t in threads:
                    t.join(timeout=2)
                print("", file=sys.stderr)
                print("interrupted", file=sys.stderr)
                if out_fh:
                    out_fh.close()
                return 0

            exit_code = 0
            for ip, code in results.items():
                if code != 0:
                    exit_code = code
    except KeyboardInterrupt:
        print("", file=sys.stderr)
        print("interrupted", file=sys.stderr)
        if out_fh:
            out_fh.close()
        return 0
    finally:
        if out_fh:
            try:
                out_fh.close()
            except Exception:
                pass

    return exit_code


def _stream_one_device(
    client,
    ip: str,
    follow: bool,
    max_duration,
    max_lines,
    prefix: str,
    color: str,
    reset: str,
    out_fh,
    collected: list,
    stop_event=None,
) -> int:
    """Stream log lines from one device. Returns exit code (0 or 1)."""
    import time
    import threading

    from fleetlib.sse import SSEIdleTimeout, SSEUnavailable, SSE_IDLE_TIMEOUT, stream_lines

    # Build a stop callable that checks both the caller's event and our deadline
    t0 = time.monotonic()
    local_stop = threading.Event()

    def _should_stop() -> bool:
        if local_stop.is_set():
            return True
        if stop_event is not None and stop_event.is_set():
            return True
        if max_duration is not None and (time.monotonic() - t0) >= max_duration:
            local_stop.set()
            return True
        return False

    try:
        count = 0
        for line in stream_lines(client, path="/api/logs", timeout=30.0, stop=_should_stop):
            if _should_stop():
                break
            out_line = f"{color}{prefix}{reset}{line}" if (prefix or color) else line
            print(out_line)
            if out_fh:
                out_fh.write(line + "\n")
                out_fh.flush()
            collected.append(line)
            count += 1
            if max_lines is not None and count >= max_lines:
                break
    except SSEIdleTimeout:
        print(
            f"WARNING: no log data from {ip} within {SSE_IDLE_TIMEOUT:.0f}s "
            f"(board may not support streaming or its worker is saturated); disconnecting",
            file=sys.stderr,
        )
        return 0
    except SSEUnavailable as e:
        print(f"ERROR: log sink unavailable on {ip}: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        local_stop.set()

    return 0


def cmd_ota_push(args) -> int:
    """OTA push a local binary to devices.

    Calls: fleetlib.ota.push(client, guard, binfile, target_version=None,
                             settle=None, do_mark_valid=False)

    Always waits for mining to spin up post-reboot (readiness grace) before
    judging success/failure.  Reports PENDING when mining is healthy but the
    firmware has not yet self-validated; reports VALIDATED when --mark-valid
    was passed and mark-valid confirmed.
    """
    import os as _os
    from fleetlib import ota
    from fleetlib.client import Client
    from fleetlib.profiles import profile_for
    from fleetlib.safety import DeviceUnreachable, IdentityMismatch

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = _ota_guard(args)
    settle_cfg = _ota_settle(args)
    settle_secs = settle_cfg.settle_delay if settle_cfg.enabled else None
    target = getattr(args, "target_version", None)
    binfile = args.binfile
    dry_run = getattr(args, "dry_run", False)
    do_mark_valid = getattr(args, "mark_valid", False)

    # Load criteria (from --criteria path or harness defaults).
    criteria_path = getattr(args, "criteria", None)
    criteria = load_criteria(criteria_path) if criteria_path else load_criteria()

    # Load profiles once for per-device profile resolution.
    _profiles = Profiles.load()

    _push = getattr(ota, "push", None)
    if _push is None:
        print("ERROR: fleetlib.ota.push not available")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        prof = profile_for(getattr(d, "board", "") or "", _profiles)

        if dry_run:
            # Run identity verify manually so dry-run still checks board identity.
            from fleetlib.discovery import verify_identity
            id_ok = verify_identity(d)
            id_str = "PASS" if id_ok else "FAIL"
            try:
                bin_size = _os.path.getsize(binfile)
                size_str = f"{bin_size:,} bytes"
            except OSError:
                size_str = "(file not found)"
            print(f"[DRY-RUN] push plan for {d.ip} ({d.board}):")
            print(f"  identity-verify : {id_str}")
            print(f"  bin file        : {binfile}")
            print(f"  image size      : {size_str}")
            print(f"  target host     : {d.ip}:{getattr(d, 'port', 80)}")
            print(f"  post-push expect: device reboots, boots new image, "
                  f"firmware self-validates")
            if do_mark_valid:
                print(f"  mark-valid      : will POST after readiness (--mark-valid)")
            if target:
                print(f"  target version  : {target}")
            print(f"  (no HTTP sent)")
            continue

        print(f"Pushing {binfile} to {d.ip}…")
        try:
            r = _push(c, guard, binfile, target_version=target, settle=settle_secs,
                      do_mark_valid=do_mark_valid, criteria=criteria, profile=prof)
        except DeviceUnreachable as exc:
            print(f"  {d.ip}: SKIPPED (unreachable: {exc})")
            ok = False
            continue
        except IdentityMismatch as exc:
            print(f"  {d.ip}: SKIPPED (identity mismatch: {exc})")
            ok = False
            continue
        except Exception as exc:
            print(f"  {d.ip}: FAILED (unexpected error: {exc})")
            ok = False
            continue

        if r.ok and r.pending:
            print(f"  {d.ip}: push PENDING — {r.detail}")
        elif r.ok:
            print(f"  {d.ip}: push OK" + (f" ({r.detail})" if r.detail not in ("ok", "") else ""))
        else:
            print(f"  {d.ip}: push FAILED ({r.detail})")
            ok = False

    return 0 if ok else 1


def cmd_ota_pull(args) -> int:
    """Trigger pull-OTA on devices.

    Calls: fleetlib.ota.pull(client, guard, mode='auto', target_version=None, settle=None)
    """
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = _ota_guard(args)
    settle_cfg = _ota_settle(args)
    settle_secs = settle_cfg.settle_delay if settle_cfg.enabled else None
    target = getattr(args, "target_version", None)
    mode = getattr(args, "mode", "auto")

    _pull = getattr(ota, "pull", None)
    if _pull is None:
        print("ERROR: fleetlib.ota.pull not available")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        print(f"Triggering pull ({mode}) on {d.ip}…")
        r = _pull(c, guard, mode=mode, target_version=target, settle=settle_secs)
        if r.ok:
            print(f"  {d.ip}: pulled to {r.version}")
            if target and r.version != target:
                print(f"  {d.ip}: WARNING expected {target}, got {r.version}")
        else:
            print(f"  {d.ip}: pull FAILED ({r.detail})")
            ok = False

    return 0 if ok else 1


def cmd_ota_mark_valid(args) -> int:
    """Mark current image valid on devices.

    Calls: fleetlib.ota.mark_valid(client, guard)
    """
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = _ota_guard(args)

    _mark = getattr(ota, "mark_valid", None)
    if _mark is None:
        print("ERROR: fleetlib.ota.mark_valid not available")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        success = _mark(c, guard)
        label = "OK" if success else "FAILED"
        print(f"  {d.ip}: mark-valid {label}")
        if not success:
            ok = False

    return 0 if ok else 1


def cmd_ota_recover(args) -> int:
    """Rollback to previous OTA image on devices.

    Calls: fleetlib.ota.recover(client, guard)
    """
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = _ota_guard(args)

    _recover = getattr(ota, "recover", None)
    if _recover is None:
        print("ERROR: fleetlib.ota.recover not yet available (OTA agent pending)")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        success = _recover(c, guard)
        label = "OK" if success else "FAILED"
        print(f"  {d.ip}: recover {label}")
        if not success:
            ok = False

    return 0 if ok else 1


def cmd_ota_status(args) -> int:
    """Read OTA status + progress from devices (read-only).

    Calls: fleetlib.ota.status(client)
    """
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    _status = getattr(ota, "status", None)
    if _status is None:
        print("ERROR: fleetlib.ota.status not yet available (OTA agent pending)")
        return 1

    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        result = _status(c)
        if result is None:
            print(f"  {d.ip}: unreachable")
        else:
            print(f"  {d.ip}: {result}")

    return 0


def cmd_ota_verify(args) -> int:
    """Verify version + mining state post-settle on devices.

    Calls: fleetlib.ota.verify(client, profile, criteria, target_version, settle)
    """
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = _unwrap_devices(_resolve_result)
    if not devices:
        print(_no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    target = args.target_version
    settle_cfg = _ota_settle(args)
    settle_secs = settle_cfg.settle_delay if settle_cfg.enabled else None
    criteria_path = getattr(args, "criteria", None)
    from fleetlib.criteria import load as load_criteria
    criteria = load_criteria(criteria_path) if criteria_path else load_criteria()

    _verify = getattr(ota, "verify", None)
    if _verify is None:
        print("ERROR: fleetlib.ota.verify not available")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        r = _verify(c, None, criteria, target, settle_secs)
        label = "OK" if r.ok else "FAIL"
        print(f"  {d.ip}: verify {target} -> {label}" + (f" ({r.detail})" if not r.ok else ""))
        if not r.ok:
            ok = False

    return 0 if ok else 1


# ---------------------------------------------------------------------------
# decode + elf subcommand handlers
# ---------------------------------------------------------------------------

def cmd_decode(args) -> int:
    """Decode a panic backtrace from a live device using an archived ELF.

    GET /api/diag/panic  ->  prefix-match ELF in archive  ->  addr2line decode.
    """
    import json as _json
    from fleetlib.client import Client, TIMEOUT_INFO
    from fleetlib.elfstore import find as elf_find
    from fleetlib.decode import chip_arch, decode_panic

    host = args.host
    port = 80
    if ":" in host:
        host, port_s = host.rsplit(":", 1)
        port = int(port_s)

    c = Client(host, port)
    panic = c.get_json("/api/diag/panic", timeout=TIMEOUT_INFO)
    if panic is None:
        print(f"ERROR: could not reach {host} or /api/diag/panic unavailable")
        return 1

    available = panic.get("available", False)
    app_sha = panic.get("app_sha256", "")

    if not available and not panic.get("backtrace") and not panic.get("exc_pc"):
        print(f"{host}: no panic available (available=false, no backtrace/pc)")
        return 0

    # Determine arch from /api/info build.chip_model (B1-360)
    from fleetlib.client import info_field as _info_field
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO) or {}
    chip_model = _info_field(info, "chip_model") or "ESP32"
    arch = chip_arch(chip_model)

    # Resolve ELF
    elf_path = getattr(args, "elf_path", None)
    if elf_path is None and app_sha:
        elf_path = elf_find(app_sha)
        if elf_path is None:
            print(f"ERROR: no archived ELF for '{app_sha}'; "
                  f"reflash with a tracked build (fleet ota push) or pass --elf <path>")
            return 1
    elif elf_path is None:
        print("ERROR: no app_sha256 in panic response and no --elf given")
        return 1

    toolchain_path = getattr(args, "toolchain_path", None)
    result = decode_panic(panic, elf_path, arch=arch, toolchain_path=toolchain_path)

    # Print result
    print(f"\nPanic decode for {host}")
    print(f"  ELF     : {elf_path}")
    print(f"  arch    : {arch}")
    print(f"  task    : {result.task or '?'}")
    print(f"  cause   : {result.exc_cause} ({result.cause_name_str})")
    if app_sha:
        print(f"  sha256  : {app_sha} (truncated; {len(app_sha)} chars)")
    if not result.ok:
        print(f"  ERROR   : {result.error}")
        return 1

    if not result.frames:
        print("  (no frames decoded)")
    else:
        print(f"\n  {'LABEL':<10} {'PC':>12}   FUNCTION @ FILE:LINE")
        print(f"  {'-' * 70}")
        for label, pc, frame in result.frames:
            print(f"  {label:<10} {pc:#012x}   {frame}")
    return 0


def cmd_elf_archive(args) -> int:
    """Manually archive a firmware ELF into the store.

    board and version are populated from esp_app_desc_t when not given (TA-461).
    """
    from fleetlib.elfstore import archive, sha256_of_elf, list_entries
    from pathlib import Path

    elf_path = args.elf_path
    board = getattr(args, "board", "")
    version = getattr(args, "version", "")

    try:
        key = archive(elf_path, board=board, version=version)

        # Read back sidecar to show final (possibly auto-populated) values
        from fleetlib.elfstore import _load_meta, _archive_root
        root = _archive_root()
        meta = _load_meta(root / f"{key}.json")

        print(f"Archived: {elf_path}")
        print(f"  sha256     : {key}")
        print(f"  board      : {meta.board or '(unset)'}")
        print(f"  version    : {meta.version or '(unset)'}")
        print(f"  build_time : {meta.build_time or '(unset)'}")
        if not board and meta.board:
            print(f"  (board auto-populated from esp_app_desc_t)")
        if not version and meta.version:
            print(f"  (version auto-populated from esp_app_desc_t)")
        return 0
    except FileNotFoundError:
        print(f"ERROR: ELF not found: {elf_path}")
        return 1
    except Exception as exc:
        print(f"ERROR: archive failed: {exc}")
        return 1


def cmd_elf_list(args) -> int:
    """List archived ELFs with in-use status from the live fleet."""
    import datetime
    from fleetlib.client import Client, TIMEOUT_INFO, info_field
    from fleetlib.elfstore import list_entries

    entries = list_entries()
    if not entries:
        print("No archived ELFs.")
        return 0

    # Collect running shas from live fleet (best-effort; failures -> empty).
    # Primary source: /api/info build.app_sha256 (B1-360 — running image sha).
    # Fallback: /api/diag/panic app_sha256 (only populated after a crash).
    # Both are truncated ELF sha256 prefixes matching the elfstore archive key.
    running_shas: set = set()
    unreachable: list = []
    try:
        devices = _unwrap_devices(resolve_devices(args))
        for d in devices:
            c = Client(d.ip, getattr(d, "port", 80))
            info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
            sha = info_field(info or {}, "app_sha256") or ""
            if sha:
                running_shas.add(sha.lower())
            else:
                # fallback: crash-build sha from /api/diag/panic
                panic = c.get_json("/api/diag/panic", timeout=TIMEOUT_INFO)
                sha = (panic or {}).get("app_sha256", "")
                if sha:
                    running_shas.add(sha.lower())
    except Exception:
        unreachable = []

    if unreachable:
        print(f"WARNING: {len(unreachable)} device(s) unreachable; IN-USE column may be incomplete")

    print(f"\n{'SHA256 (prefix)':<20} {'BOARD':<20} {'VERSION':<18} {'DIRTY':<6} "
          f"{'ARCHIVED':<22} {'SIZE':>10}  IN-USE")
    print("-" * 105)
    for meta, size in entries:
        sha_short = meta.sha256[:16]
        dirty_str = "yes" if meta.dirty else "no"
        size_str = f"{size:,}"
        # IN-USE: any running sha (truncated prefix) matches the start of the archived full sha.
        # Compare lower-case so dev/-dirty sha prefixes from /api/info match the archive key.
        in_use = any(meta.sha256.lower().startswith(s.lower()) for s in running_shas) if running_shas else "?"
        in_use_str = "yes" if in_use is True else ("no" if in_use is False else "?")
        print(f"{sha_short + '…':<20} {meta.board:<20} {meta.version:<18} {dirty_str:<6} "
              f"{meta.archived_at:<22} {size_str:>10}  {in_use_str}")

    total_size = sum(s for _, s in entries)
    print(f"\n{len(entries)} archived ELF(s), {total_size:,} bytes total")
    return 0


def cmd_elf_prune(args) -> int:
    """Prune archived ELFs by mtime budget or fleet-aware GC.

    SAFETY GUARDS (--in-use mode):
      1. The most-recently archived N entries (--grace-keep, default 5) are
         ALWAYS protected regardless of in-use status.
      2. If any fleet target is unreachable, prune is REFUSED unless --hosts
         supplies an authoritative set and all listed hosts are reachable.
    """
    from fleetlib.client import Client, TIMEOUT_INFO
    from fleetlib.elfstore import list_entries, prune as elf_prune

    keep = args.keep
    max_age_str = getattr(args, "max_age", None)
    in_use_mode = getattr(args, "in_use", False)
    grace_keep = getattr(args, "grace_keep", 5)
    dry_run = getattr(args, "dry_run", False)
    yes = getattr(args, "yes", False)

    max_age_secs: float = None
    if max_age_str:
        max_age_secs = _parse_age_duration(max_age_str)

    protected_shas: set = set()

    if in_use_mode:
        # Fleet-aware GC: collect running shas
        _elf_resolve = resolve_devices(args)
        devices = _unwrap_devices(_elf_resolve)
        if not devices:
            print("ERROR: no devices found; cannot perform fleet-aware GC safely")
            return 1

        unreachable = []
        running_shas: set = set()
        for d in devices:
            c = Client(d.ip, getattr(d, "port", 80))
            from fleetlib.client import info_field as _info_field_prune
            info_resp = c.get_json("/api/info", timeout=TIMEOUT_INFO)
            if info_resp is None:
                unreachable.append(d.ip)
            else:
                sha = _info_field_prune(info_resp, "app_sha256") or ""
                if sha:
                    running_shas.add(sha)
                else:
                    # fallback: crash-build sha from /api/diag/panic
                    panic = c.get_json("/api/diag/panic", timeout=TIMEOUT_INFO)
                    sha = (panic or {}).get("app_sha256", "")
                    if sha:
                        running_shas.add(sha)

        # SAFETY GUARD 2: refuse on incomplete discovery (unless --hosts authoritative)
        if unreachable and not getattr(args, "hosts", None):
            print("ERROR: the following devices are unreachable — cannot safely determine "
                  "which ELFs are in use:")
            for ip in unreachable:
                print(f"  {ip}")
            print("Pass --hosts with an authoritative list of ALL fleet devices, "
                  "or fix device connectivity first.")
            return 1
        elif unreachable:
            print(f"WARNING: {len(unreachable)} device(s) unreachable; proceeding because "
                  f"--hosts provides an authoritative set")
            for ip in unreachable:
                print(f"  UNREACHABLE: {ip}")

        # Expand running short-shas to full archive keys
        entries = list_entries()
        for meta, _ in entries:
            for short_sha in running_shas:
                if meta.sha256.startswith(short_sha):
                    protected_shas.add(meta.sha256)

        # SAFETY GUARD 1: always protect the N most-recently archived entries
        if grace_keep > 0:
            sorted_entries = sorted(entries, key=lambda t: t[0].archived_at, reverse=True)
            for meta, _ in sorted_entries[:grace_keep]:
                protected_shas.add(meta.sha256)

        print(f"Fleet-aware GC: {len(running_shas)} running sha(s) found, "
              f"{len(protected_shas)} entries protected")

    else:
        # Mtime budget prune: protect the grace_keep most recent
        entries = list_entries()
        if grace_keep > 0:
            sorted_entries = sorted(entries, key=lambda t: t[0].archived_at, reverse=True)
            for meta, _ in sorted_entries[:grace_keep]:
                protected_shas.add(meta.sha256)

    # Preview what will be deleted
    would_delete = elf_prune(
        keep=keep, max_age=max_age_secs,
        protected_shas=protected_shas, dry_run=True,
    )
    if not would_delete:
        print("Nothing to prune.")
        return 0

    print(f"{'[DRY-RUN] ' if dry_run else ''}Would delete {len(would_delete)} entry(ies):")
    for sha in would_delete:
        print(f"  {sha[:16]}…")

    if dry_run:
        return 0

    if not yes:
        try:
            ans = input(f"Delete {len(would_delete)} entry(ies)? [y/N] ")
        except EOFError:
            ans = ""
        if ans.strip().lower() not in ("y", "yes"):
            print("Aborted.")
            return 1

    deleted = elf_prune(
        keep=keep, max_age=max_age_secs,
        protected_shas=protected_shas, dry_run=False,
    )
    print(f"Deleted {len(deleted)} entry(ies).")
    return 0


def _parse_age_duration(s: str) -> float:
    """Parse '30d', '7d', '24h', '1h' to seconds."""
    s = s.strip()
    if s.endswith("d"):
        return float(s[:-1]) * 86400
    if s.endswith("h"):
        return float(s[:-1]) * 3600
    if s.endswith("m"):
        return float(s[:-1]) * 60
    if s.endswith("s"):
        return float(s[:-1])
    return float(s)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _print_device_table(devices, extra_headers=None) -> None:
    from fleetlib.client import Client, TIMEOUT_INFO
    print(f"{'HOST':<20} {'BOARD':<20} {'VERSION':<16} {'UPTIME':>12}")
    print("-" * 72)
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
        uptime_str = "??"
        if info:
            uptime_str = _fmt_uptime(info.get("uptime_ms", 0))
        print(f"{d.ip:<20} {d.board:<20} {d.version:<16} {uptime_str:>12}")
    print(f"\n{len(devices)} device(s) found.")


def _fmt_uptime(uptime_ms: int) -> str:
    s = uptime_ms // 1000
    h, rem = divmod(s, 3600)
    m, sec = divmod(rem, 60)
    if h > 0:
        return f"{h}h{m:02d}m{sec:02d}s"
    if m > 0:
        return f"{m}m{sec:02d}s"
    return f"{sec}s"


# ---------------------------------------------------------------------------
# Entry point — delegate to registry-driven dispatcher in cli.py
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    from cli import main
    sys.exit(main())
