"""Shared helpers: config loading, flag-group builders, device resolution, suite context."""
from __future__ import annotations

import argparse
import importlib.util
import os
import sys
import tomllib
from pathlib import Path
from typing import Any, Iterable, Optional

# Sentinel: --settle given as a bare flag (no value).
# Exposed here so command modules and fleet.py can share it.
SETTLE_BARE = object()


# ---------------------------------------------------------------------------
# Config + plugin loading
# ---------------------------------------------------------------------------

def load_config(path: str = "fleet.toml") -> dict:
    """Load fleet.toml. Returns {} if absent or unreadable."""
    p = Path(path)
    if not p.exists():
        return {}
    try:
        with open(p, "rb") as fh:
            return tomllib.load(fh)
    except Exception as e:
        print(f"fleet: failed to load config {p}: {e}", file=sys.stderr)
        return {}


def load_plugins(paths: Iterable[str], config_dir: str, api: object) -> None:
    """Load plugin .py files; warn on failure, continue."""
    for p in paths:
        plugin_path = Path(p) if os.path.isabs(p) else Path(config_dir) / p
        try:
            spec = importlib.util.spec_from_file_location("_fleet_plugin", plugin_path)
            if spec is None or spec.loader is None:
                raise ImportError(f"cannot load spec from {plugin_path}")
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            if hasattr(mod, "register"):
                mod.register(api)
        except Exception as e:
            print(f"fleet: plugin load failed ({plugin_path}): {e}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Flag-group helpers (public names; mirrored from fleet.py _add_* helpers)
# ---------------------------------------------------------------------------

def add_common_flags(p: argparse.ArgumentParser) -> None:
    """Add shared flags to a parser (main or subcommand).

    --log-level is registered with default=SUPPRESS so a value given
    after the subcommand wins, but a value given before (on the root parser)
    is not silently discarded by the subparser default.
    """
    p.add_argument("--log-level",
                   default=argparse.SUPPRESS,
                   choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                   help="log level (default: WARNING)")

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
    r.add_argument("--settle", nargs="?", type=int, const=SETTLE_BARE, default=None,
                   metavar="SEC",
                   help="opt-in warmup settle: bare = criteria default (120s), --settle N = N seconds")
    r.add_argument("--dry-run", action="store_true",
                   help="log mutating operations but don't execute them")
    r.add_argument("--yes", action="store_true",
                   help="auto-confirm guard for mutating operations")

    m = p.add_argument_group("metrics publishing")
    m.add_argument("--metrics-mqtt-url", metavar="HOST[:PORT]", default=None,
                   dest="metrics_mqtt_url",
                   help="broker for run metrics (overrides BB_TEST_METRICS_BROKER / BB_TEST_RECEIVER); "
                        "default ON when configured, skip with note when absent")
    m.add_argument("--metrics-topic", metavar="PREFIX", default="fleettest",
                   dest="metrics_topic",
                   help="MQTT topic prefix for run metrics (default: fleettest); "
                        "full topic: <prefix>/<suite>/<board>")
    m.add_argument("--no-publish-metrics", action="store_true", default=False,
                   dest="no_publish_metrics",
                   help="disable automatic metrics publishing (opt-out)")


def add_watch_flags(p: argparse.ArgumentParser) -> None:
    """Minimal common flags for watch (targeting only)."""
    p.add_argument("--log-level", default=argparse.SUPPRESS,
                   choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                   help="log level (default: WARNING)")
    g = p.add_argument_group("targeting")
    g.add_argument("--hosts", metavar="H,H,…",
                   help="comma-separated IPs/hostnames (skip mDNS discovery)")
    g.add_argument("--discover-timeout", type=int, default=10, metavar="SEC",
                   help="mDNS browse window in seconds (default: 10)")
    g.add_argument("--board", metavar="CLASS",
                   help="filter devices by board class substring")


def add_logs_flags(p: argparse.ArgumentParser) -> None:
    """Minimal common flags for logs (targeting only)."""
    p.add_argument("--log-level", default=argparse.SUPPRESS,
                   choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                   help="log level (default: WARNING)")
    g = p.add_argument_group("targeting")
    g.add_argument("--hosts", metavar="H,H,…",
                   help="comma-separated IPs/hostnames (skip mDNS discovery)")
    g.add_argument("--discover-timeout", type=int, default=10, metavar="SEC",
                   help="mDNS browse window in seconds (default: 10)")
    g.add_argument("--board", metavar="CLASS",
                   help="filter devices by board class substring")


# ---------------------------------------------------------------------------
# Suite argument helpers
# ---------------------------------------------------------------------------

def add_suite_arguments(parser: argparse.ArgumentParser, suite_name: str) -> None:
    """Call suite.add_arguments(parser) if the suite module is importable."""
    try:
        from suites import load_suite
        mod = load_suite(suite_name)
        mod.add_arguments(parser)
    except ImportError as exc:
        import warnings
        warnings.warn(f"suite {suite_name}: add_arguments skipped ({exc})", stacklevel=2)
    except Exception:
        pass


def suite_help(name: str) -> str:
    try:
        from suites import SUITES, load_suite
        if name in SUITES:
            mod = load_suite(name)
            return getattr(mod, "HELP", f"run {name} suite")
    except Exception:
        pass
    return f"run {name} suite"


# ---------------------------------------------------------------------------
# Device resolution
# ---------------------------------------------------------------------------

def resolve_devices(args):
    """Resolve devices from args (delegates to suites.resolve_devices)."""
    from suites import resolve_devices as _resolve
    return _resolve(args)


def unwrap_devices(result, caller_name: str = ""):
    """Extract the device list from a ResolveResult (or plain list)."""
    from fleetlib.discovery import ResolveResult
    if isinstance(result, list):
        return result
    if result.devices and result.failures:
        emit_resolve_warnings(result)
    return result.devices


def emit_resolve_warnings(result, file=None) -> None:
    """Print per-host enrichment failures to stderr."""
    from fleetlib.discovery import ResolveResult
    if not isinstance(result, ResolveResult):
        return
    dest = file or sys.stderr
    for f in result.failures:
        print(f"  {f.host}: unreachable ({f.reason})", file=dest)


def no_devices_message(result) -> str:
    """Return the appropriate 'no devices' message given a ResolveResult."""
    from fleetlib.discovery import ResolveResult
    if not isinstance(result, ResolveResult):
        return "No devices found."
    if result.from_mdns:
        return "No devices found via mDNS (_taipanminer._tcp.local.)."
    n = len(result.failures)
    lines = [f"{n} host(s) specified; none reachable:"]
    for f in result.failures:
        lines.append(f"  {f.host}: unreachable ({f.reason})")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# OTA helpers
# ---------------------------------------------------------------------------

# Re-export SettleConfig from suites so command modules can import it from core.
def _get_settle_config_class():
    from suites import SettleConfig
    return SettleConfig


def ota_guard(args):
    """Build a Guard from args."""
    from fleetlib.safety import Guard
    return Guard(
        dry_run=getattr(args, "dry_run", False),
        confirm=getattr(args, "yes", False),
    )


def ota_settle(args):
    """Build a SettleConfig from args."""
    from suites import SettleConfig
    settle_arg = getattr(args, "settle", None)
    if settle_arg is None:
        return SettleConfig(enabled=False)
    if settle_arg is SETTLE_BARE:
        return SettleConfig(settle_delay=120, enabled=True)
    return SettleConfig(settle_delay=settle_arg, enabled=True)


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def write_outputs(records: list, fields, devices, out_json_path, out_csv_path) -> None:
    """Write accumulated records to JSON and/or CSV output files."""
    import csv as _csv
    import json as _json

    if out_json_path:
        with open(out_json_path, "w") as fh:
            _json.dump(records, fh, indent=2, default=str)

    if out_csv_path:
        if fields:
            header = ["ts", "host"] + list(fields)
        else:
            header = ["ts", "host", "response"]
        with open(out_csv_path, "w", newline="") as fh:
            writer = _csv.writer(fh)
            writer.writerow(header)
            for rec in records:
                if fields:
                    row = [rec["ts"], rec["host"]] + [
                        rec["fields"].get(f) for f in fields
                    ]
                else:
                    row = [rec["ts"], rec["host"], _json.dumps(rec.get("response", {}), separators=(",", ":"))]
                writer.writerow(row)


# ---------------------------------------------------------------------------
# Suite context builder
# ---------------------------------------------------------------------------

def build_suite_context(args, name: str = "fleet"):
    """Construct SuiteContext from parsed args."""
    from fleetlib.criteria import load as load_criteria
    from fleetlib.profiles import Profiles
    from fleetlib.results import ResultSet
    from fleetlib.safety import Guard
    from suites import SuiteContext, SettleConfig

    # Load criteria
    criteria_path = getattr(args, "criteria", None)
    criteria = load_criteria(criteria_path) if criteria_path else load_criteria()

    # Settle config
    settle_arg = getattr(args, "settle", None)
    if settle_arg is None:
        settle = SettleConfig(settle_delay=0, enabled=False)
    elif settle_arg is SETTLE_BARE:
        settle = SettleConfig(settle_delay=criteria.settle_delay, enabled=True)
    else:
        settle = SettleConfig(settle_delay=settle_arg, enabled=True)

    guard = Guard(
        dry_run=getattr(args, "dry_run", False),
        confirm=getattr(args, "yes", False),
    )

    fields_raw = getattr(args, "fields", None)
    fields = [f.strip() for f in fields_raw.split(",") if f.strip()] if fields_raw else None

    enabled_gates: set = set(args.gates)
    for gname in args.skip_gates:
        enabled_gates.discard(gname)

    results = ResultSet(suite_name=name)

    return SuiteContext(
        devices=[],
        criteria=criteria,
        guard=guard,
        results=results,
        fields=fields,
        gates=enabled_gates,
        settle=settle,
        out_json=getattr(args, "out_json", None),
        out_junit=getattr(args, "out_junit", None),
        baseline=getattr(args, "baseline", None),
        profiles=Profiles.load(),
    )
