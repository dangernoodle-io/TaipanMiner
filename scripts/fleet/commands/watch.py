"""watch command — poll an endpoint per-device and observe field values."""
from __future__ import annotations

import sys

from core import resolve_devices, unwrap_devices, no_devices_message, write_outputs

NAME = "watch"
HELP = "poll an endpoint per-device and observe field values (CI-safe, read-only)"


def add_arguments(parser) -> None:
    from core import add_watch_flags
    add_watch_flags(parser)
    parser.add_argument("watch_path", metavar="PATH",
                        help="endpoint path to poll (e.g. /api/diag/heap)")
    parser.add_argument("--fields", metavar="F,F,…",
                        help="comma-separated dotted fields to extract (default: whole response)")
    parser.add_argument("--interval", type=float, default=5.0, metavar="SEC",
                        help="seconds between polls (default: 5)")
    parser.add_argument("--duration", metavar="DUR",
                        help="stop after this duration: 30s / 5m / 1h / bare seconds")
    parser.add_argument("--count", type=int, default=None, metavar="N",
                        help="stop after N ticks per device")
    parser.add_argument("--on-change", dest="on_change", action="store_true",
                        help="only emit a row when tracked fields change")
    parser.add_argument("--until", metavar="EXPR",
                        help="exit 0 when expression is satisfied (all devices, or --any)")
    parser.add_argument("--any", dest="any_device", action="store_true",
                        help="with --until/--alert: condition met when ANY device satisfies")
    parser.add_argument("--alert", metavar="EXPR",
                        help="flag rows when expression is true (non-terminating)")
    parser.add_argument("--out-json", metavar="PATH", dest="out_json",
                        help="write time-series records as JSON list to file")
    parser.add_argument("--out-csv", metavar="PATH", dest="out_csv",
                        help="write time-series records as CSV to file")


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


def run(args) -> int:
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
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
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
                    write_outputs(records, fields, devices, out_json_path, out_csv_path)
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
        write_outputs(records, fields, devices, out_json_path, out_csv_path)
        return 0

    write_outputs(records, fields, devices, out_json_path, out_csv_path)

    # --until was given but not satisfied within bound
    if until_pred is not None:
        print("until condition not satisfied within time/count bound", file=sys.stderr)
        return 1

    return 0
