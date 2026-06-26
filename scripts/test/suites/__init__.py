"""
Suite contract — all suites conform to this interface.

Each suite module must expose:
  NAME: str
  HELP: str
  add_arguments(parser: argparse.ArgumentParser) -> None
  run(ctx: SuiteContext) -> ResultSet
"""
from __future__ import annotations
import argparse
import importlib
from dataclasses import dataclass, field
from typing import List, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from fleetlib.results import ResultSet
    from fleetlib.safety import Guard
    from fleetlib.criteria import Criteria
    from fleetlib.profiles import Profiles

# Registry: subcommand name -> dotted module path (lazy import)
SUITES: dict[str, str] = {
    "functional": "suites.functional",
    "soak": "suites.soak",
    "stress": "suites.stress",
    "faults": "suites.faults",
    "telemetry": "suites.telemetry",
}


@dataclass
class SettleConfig:
    settle_delay: int = 0
    enabled: bool = False

    def wait_ready(self, device, criteria=None) -> "Readiness":  # noqa: F821
        from fleetlib.readiness import wait_until_ready
        from fleetlib.criteria import Criteria as C
        crit = criteria or C()
        return wait_until_ready(device, None, crit)


@dataclass
class SuiteContext:
    """Shared context passed to every suite's run() function."""
    devices: list                        # list[Device] — already resolved + filtered
    criteria: "Criteria"
    guard: "Guard"
    results: "ResultSet"
    fields: Optional[List[str]]          # on-demand field selection; None = all
    gates: set                           # enabled checks; empty = all
    settle: SettleConfig
    out_json: Optional[str]
    out_junit: Optional[str]
    baseline: Optional[str]
    profiles: Optional["Profiles"] = None       # board-class overrides from profiles.yaml
    extra: dict = field(default_factory=dict)  # suite-specific parsed args


def resolve_devices(args):
    """Discover (zeroconf) or --hosts, then filter by --board.

    Returns a :class:`~fleetlib.discovery.ResolveResult` containing the
    successfully-enriched devices and any per-host enrichment failures.
    Callers render user-facing messages from the result; this function is
    intentionally message-free so tests can assert on the structured output.
    """
    import sys
    import os
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    from fleetlib.discovery import discover, from_hosts_detailed, ResolveResult

    if hasattr(args, "hosts") and args.hosts:
        hosts = [h.strip() for h in args.hosts.split(",") if h.strip()]
        result = from_hosts_detailed(hosts)
    else:
        timeout = getattr(args, "discover_timeout", 10)
        devices = discover(timeout=timeout)
        result = ResolveResult(devices=devices, failures=[], from_mdns=True)

    board_filter = getattr(args, "board", None)
    if board_filter:
        result.devices = [d for d in result.devices if _matches_board(d, board_filter)]

    return result


def _matches_board(device, board_class: str) -> bool:
    """Return True if device.board contains board_class (case-insensitive)."""
    try:
        board = device.board or ""
        return board_class.lower() in board.lower()
    except Exception:
        return True  # include on error; let suite handle it


def gate_enabled(ctx: SuiteContext, name: str) -> bool:
    """Return True if the named gate/check is enabled."""
    return not ctx.gates or name in ctx.gates


def load_suite(name: str):
    """Lazily import a suite module by subcommand name."""
    module_path = SUITES.get(name)
    if not module_path:
        raise ValueError(f"Unknown suite: {name}")
    return importlib.import_module(module_path)
