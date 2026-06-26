#!/usr/bin/env python3
"""fleet.py — TaipanMiner fleet test harness CLI (TA-433).

Subcommands:
  discover   — discover devices via mDNS, print table
  status     — GET /api/info + /api/health per device, print summary
  functional — run functional suite (schema validation per device)
  soak       — run soak suite (long-running monitor)
  stress     — run stress suite (concurrent load)
  faults     — run fault-injection suite
  matrix     — run transport-matrix suite
  ota        — OTA operations (push/pull/mark-valid/recover/status/verify)
"""
from __future__ import annotations
import argparse
import logging
import os
import sys
import time

# Ensure fleetlib and suites are importable when run directly
sys.path.insert(0, os.path.dirname(__file__))

from fleetlib.criteria import Criteria, load as load_criteria
from fleetlib.safety import Guard
from fleetlib.results import ResultSet
from suites import SuiteContext, SettleConfig, resolve_devices


def _add_common_flags(p: argparse.ArgumentParser) -> None:
    """Add shared flags to a parser (main or subcommand)."""
    g = p.add_argument_group("targeting")
    g.add_argument("--hosts", metavar="H,H,…",
                   help="comma-separated IPs/hostnames (skip mDNS discovery)")
    g.add_argument("--discover-timeout", type=int, default=10, metavar="SEC",
                   help="mDNS browse window in seconds (default: 10)")
    g.add_argument("--board", metavar="CLASS",
                   help="filter devices by board class substring (e.g. bitaxe)")

    o = p.add_argument_group("output")
    o.add_argument("--fields", metavar="F,F,…",
                   help="comma-separated field subset for on-demand validation")
    o.add_argument("--gate", action="append", dest="gates", metavar="NAME", default=[],
                   help="enable a specific check gate (repeatable; default: all)")
    o.add_argument("--skip", action="append", dest="skip_gates", metavar="NAME", default=[],
                   help="disable a specific check gate (repeatable)")
    o.add_argument("--out-json", metavar="PATH", help="write JSON results to file")
    o.add_argument("--out-junit", metavar="PATH", help="write JUnit XML results to file")
    o.add_argument("--baseline", metavar="PATH",
                   help="compare results against a prior JSON result file")
    o.add_argument("--criteria", metavar="PATH",
                   help="YAML criteria override file")

    r = p.add_argument_group("run control")
    r.add_argument("--settle", type=int, default=None, metavar="SEC",
                   help="warmup settle delay in seconds (default: from criteria)")
    r.add_argument("--no-settle", action="store_true",
                   help="disable settle/readiness gate")
    r.add_argument("--dry-run", action="store_true",
                   help="log mutating operations but don't execute them")
    r.add_argument("--yes", action="store_true",
                   help="auto-confirm guard for mutating operations")


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="fleet",
        description="TaipanMiner fleet test harness",
    )
    p.add_argument("--log-level", default="WARNING",
                   choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                   help="log level (default: WARNING)")
    _add_common_flags(p)

    sub = p.add_subparsers(dest="subcommand", metavar="SUBCOMMAND")
    sub.required = True

    # discover
    disc = sub.add_parser("discover", help="discover devices via mDNS and print table")
    _add_common_flags(disc)
    disc.set_defaults(func=cmd_discover)

    # status
    st = sub.add_parser("status", help="GET /api/info + /api/health per device")
    _add_common_flags(st)
    st.set_defaults(func=cmd_status)

    # functional
    fn = sub.add_parser("functional", help=_suite_help("functional"))
    _add_common_flags(fn)
    fn.add_argument("--strict", action="store_true",
                    help="fail on nullable/empty-enum schema issues (default: downgrade to skip)")
    fn.set_defaults(func=lambda a: cmd_suite(a, "functional"))

    # soak
    sk = sub.add_parser("soak", help=_suite_help("soak"))
    _add_common_flags(sk)
    sk.set_defaults(func=lambda a: cmd_suite(a, "soak"))

    # stress
    sr = sub.add_parser("stress", help=_suite_help("stress"))
    _add_common_flags(sr)
    sr.set_defaults(func=lambda a: cmd_suite(a, "stress"))

    # faults
    fa = sub.add_parser("faults", help=_suite_help("faults"))
    _add_common_flags(fa)
    fa.set_defaults(func=lambda a: cmd_suite(a, "faults"))

    # matrix
    mx = sub.add_parser("matrix", help=_suite_help("matrix"))
    _add_common_flags(mx)
    mx.set_defaults(func=lambda a: cmd_suite(a, "matrix"))

    # ota
    ota_p = sub.add_parser("ota", help="OTA firmware operations")
    _add_common_flags(ota_p)
    ota_sub = ota_p.add_subparsers(dest="ota_op", metavar="OP")
    ota_sub.required = True

    op_push = ota_sub.add_parser("push", help="push a local .bin to devices")
    _add_common_flags(op_push)
    op_push.add_argument("--bin", dest="binfile", metavar="PATH", required=True,
                         help="path to firmware .bin")
    op_push.add_argument("--target", metavar="VER", dest="target_version",
                         help="expected version after flashing")
    op_push.set_defaults(func=cmd_ota_push)

    op_pull = ota_sub.add_parser("pull", help="trigger pull-OTA on devices")
    _add_common_flags(op_pull)
    op_pull.add_argument("--mode", default="auto", choices=["auto", "pull"],
                         help="OTA mode: auto=detect boot/pull mode (default), pull=force")
    op_pull.add_argument("--target", metavar="VER", dest="target_version",
                         help="assert devices land on this version")
    op_pull.set_defaults(func=cmd_ota_pull)

    op_mark = ota_sub.add_parser("mark-valid", help="mark current image valid")
    _add_common_flags(op_mark)
    op_mark.set_defaults(func=cmd_ota_mark_valid)

    op_recover = ota_sub.add_parser("recover", help="rollback to previous image")
    _add_common_flags(op_recover)
    op_recover.set_defaults(func=cmd_ota_recover)

    op_ostatus = ota_sub.add_parser("status", help="read /api/update/status + /api/update/progress")
    _add_common_flags(op_ostatus)
    op_ostatus.set_defaults(func=cmd_ota_status)

    op_verify = ota_sub.add_parser("verify", help="verify version + ota_validated")
    _add_common_flags(op_verify)
    op_verify.add_argument("--target", metavar="VER", dest="target_version", required=True,
                           help="expected version string")
    op_verify.set_defaults(func=cmd_ota_verify)

    return p


def _suite_help(name: str) -> str:
    try:
        from suites import SUITES, load_suite
        if name in SUITES:
            mod = load_suite(name)
            return getattr(mod, "HELP", f"run {name} suite")
    except Exception:
        pass
    return f"run {name} suite"


def _build_context(args, suite_name: str = "fleet") -> SuiteContext:
    """Construct SuiteContext from parsed args."""
    # Load criteria
    criteria_path = getattr(args, "criteria", None)
    if criteria_path:
        criteria = load_criteria(criteria_path)
    else:
        criteria = Criteria()

    # Settle config
    if getattr(args, "no_settle", False):
        settle = SettleConfig(settle_delay=0, enabled=False)
    elif args.settle is not None:
        settle = SettleConfig(settle_delay=args.settle, enabled=True)
    else:
        settle = SettleConfig(settle_delay=criteria.settle_delay, enabled=True)

    # Guard
    guard = Guard(
        dry_run=getattr(args, "dry_run", False),
        confirm=getattr(args, "yes", False),
    )

    # Fields
    fields_raw = getattr(args, "fields", None)
    fields = [f.strip() for f in fields_raw.split(",") if f.strip()] if fields_raw else None

    # Gates
    enabled_gates: set = set(args.gates)
    # Note: --skip removes from gates; if no --gate specified, empty = all enabled
    # so --skip only makes sense with explicit --gate or when all are enabled.
    for name in args.skip_gates:
        enabled_gates.discard(name)

    results = ResultSet(suite_name=suite_name)

    return SuiteContext(
        devices=[],          # filled in by resolve_devices before suite run
        criteria=criteria,
        guard=guard,
        results=results,
        fields=fields,
        gates=enabled_gates,
        settle=settle,
        out_json=getattr(args, "out_json", None),
        out_junit=getattr(args, "out_junit", None),
        baseline=getattr(args, "baseline", None),
    )


# ---------------------------------------------------------------------------
# Subcommand handlers
# ---------------------------------------------------------------------------

def cmd_discover(args) -> int:
    """Discover devices via mDNS and print a table."""
    print("Discovering devices (mDNS _taipanminer._tcp.local.)…")
    devices = resolve_devices(args)
    if not devices:
        print("No devices found.")
        return 0
    _print_device_table(devices, extra_headers=["uptime"])
    return 0


def cmd_status(args) -> int:
    """Fetch /api/info + /api/health for each device and print summary."""
    from fleetlib.client import Client, TIMEOUT_INFO, TIMEOUT_HEALTH

    devices = resolve_devices(args)
    if not devices:
        print("No devices found.")
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

        board = info.get("board", d.board)
        version = info.get("version", d.version)

        health_str = "ok" if health and health.get("status") == "ok" else "??"
        if health and health.get("status") != "ok":
            all_ok = False

        print(f"{d.ip:<20} {board:<20} {version:<16} {uptime_str:>12}  {heap_str:>12}  {health_str}")

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
        # Suite module doesn't exist yet (soak/stress/faults/matrix are stubs)
        print(f"Suite '{suite_name}' not available: {exc}")
        return 1

    ctx = _build_context(args, suite_name=suite_name)
    ctx.devices = resolve_devices(args)

    # Forward suite-specific flags into ctx.extra
    if suite_name == "functional":
        ctx.extra["strict"] = getattr(args, "strict", False)

    if not ctx.devices:
        print("No devices found.")
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

    return 1 if summary["fail"] > 0 else 0


def _ota_guard(args) -> Guard:
    return Guard(
        dry_run=getattr(args, "dry_run", False),
        confirm=getattr(args, "yes", False),
    )


def _ota_settle(args) -> "SettleConfig":
    no_settle = getattr(args, "no_settle", False)
    delay = getattr(args, "settle", None)
    from suites import SettleConfig
    if no_settle:
        return SettleConfig(enabled=False)
    return SettleConfig(settle_delay=delay if delay is not None else 120, enabled=True)


def cmd_ota_push(args) -> int:
    """OTA push a local binary to devices.

    Calls: fleetlib.ota.push(client, guard, binfile, target_version=None, settle=None)
    Falls back to legacy signature if the new one is not yet present.
    """
    from fleetlib import ota

    devices = resolve_devices(args)
    if not devices:
        print("No devices found.")
        return 1

    guard = _ota_guard(args)
    settle = _ota_settle(args)
    target = getattr(args, "target_version", None)
    binfile = args.binfile

    _push = getattr(ota, "push", None)
    if _push is None:
        print("ERROR: fleetlib.ota.push not available")
        return 1

    ok = True
    for d in devices:
        print(f"Pushing {binfile} to {d.ip}…")
        try:
            success = _push(d, guard, binfile, target_version=target, settle=settle)
        except TypeError:
            # legacy signature: push(device, binfile, guard=None)
            success = _push(d, binfile, guard=guard)
        label = "OK" if success else "FAILED"
        print(f"  {d.ip}: push {label}")
        if not success:
            ok = False

    return 0 if ok else 1


def cmd_ota_pull(args) -> int:
    """Trigger pull-OTA on devices.

    Calls: fleetlib.ota.pull(client, guard, mode='auto', target_version=None, settle=None)
    """
    from fleetlib import ota

    devices = resolve_devices(args)
    if not devices:
        print("No devices found.")
        return 1

    guard = _ota_guard(args)
    settle = _ota_settle(args)
    target = getattr(args, "target_version", None)
    mode = getattr(args, "mode", "auto")

    _pull = getattr(ota, "pull", None)
    if _pull is None:
        print("ERROR: fleetlib.ota.pull not available")
        return 1

    ok = True
    for d in devices:
        print(f"Triggering pull ({mode}) on {d.ip}…")
        try:
            version = _pull(d, guard, mode=mode, target_version=target, settle=settle)
        except TypeError:
            # legacy signature: pull(device, guard=None, expected_version=None)
            version = _pull(d, guard=guard, expected_version=target)
        if version:
            print(f"  {d.ip}: pulled to {version}")
            if target and version != target:
                print(f"  {d.ip}: WARNING expected {target}, got {version}")
        else:
            print(f"  {d.ip}: pull FAILED")
            ok = False

    return 0 if ok else 1


def cmd_ota_mark_valid(args) -> int:
    """Mark current image valid on devices.

    Calls: fleetlib.ota.mark_valid(client, guard)
    """
    from fleetlib import ota

    devices = resolve_devices(args)
    if not devices:
        print("No devices found.")
        return 1

    guard = _ota_guard(args)

    _mark = getattr(ota, "mark_valid", None)
    if _mark is None:
        print("ERROR: fleetlib.ota.mark_valid not available")
        return 1

    ok = True
    for d in devices:
        try:
            success = _mark(d, guard)
        except TypeError:
            success = _mark(d, guard=guard)
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

    devices = resolve_devices(args)
    if not devices:
        print("No devices found.")
        return 1

    guard = _ota_guard(args)

    _recover = getattr(ota, "recover", None)
    if _recover is None:
        print("ERROR: fleetlib.ota.recover not yet available (OTA agent pending)")
        return 1

    ok = True
    for d in devices:
        success = _recover(d, guard)
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

    devices = resolve_devices(args)
    if not devices:
        print("No devices found.")
        return 1

    _status = getattr(ota, "status", None)
    if _status is None:
        print("ERROR: fleetlib.ota.status not yet available (OTA agent pending)")
        return 1

    for d in devices:
        result = _status(d)
        if result is None:
            print(f"  {d.ip}: unreachable")
        else:
            print(f"  {d.ip}: {result}")

    return 0


def cmd_ota_verify(args) -> int:
    """Verify version + mining state post-settle on devices.

    Calls: fleetlib.ota.verify(client, profile, criteria, target_version, settle)
    Falls back to legacy verify(device, target_version) if new signature absent.
    """
    from fleetlib import ota

    devices = resolve_devices(args)
    if not devices:
        print("No devices found.")
        return 1

    target = args.target_version
    settle = _ota_settle(args)
    criteria_path = getattr(args, "criteria", None)
    from fleetlib.criteria import load as load_criteria, Criteria
    criteria = load_criteria(criteria_path) if criteria_path else Criteria()

    _verify = getattr(ota, "verify", None)
    if _verify is None:
        print("ERROR: fleetlib.ota.verify not available")
        return 1

    ok = True
    for d in devices:
        try:
            verified = _verify(d, None, criteria, target, settle)
        except TypeError:
            # legacy signature: verify(device, target_version)
            verified = _verify(d, target)
        label = "OK" if verified else "FAIL"
        print(f"  {d.ip}: verify {target} -> {label}")
        if not verified:
            ok = False

    return 0 if ok else 1


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
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    parser = _build_parser()
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(levelname)s %(name)s: %(message)s",
    )

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
