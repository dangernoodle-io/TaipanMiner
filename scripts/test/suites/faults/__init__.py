"""Faults suite (NAME="faults") — recovery-asserting fault injection.

Each scenario INJECTS a fault then asserts RECOVERY: settle, wait_until_ready,
heap/mining back to baseline, no new panic. Every destructive step is gated by
ctx.guard (identity-verify + dry-run + confirm).

Scenarios (one submodule each):
  socket        — drive /api/diag/sockets in_use toward lwip_max_sockets via many
                  short connections; assert no crash + drain + recovery (folds socket_soak)
  broker-outage — cycle a local mosquitto broker down/up (docker); watch uptime/panic/heap
                  across cycles; assert recovery (folds broker_outage_repro/corepin_validate)
  vcore-drop    — ASIC only; POST /api/diag/vcore-drop debug hook; assert latch/recovery
"""
from __future__ import annotations
import logging
import os
import sys
from typing import TYPE_CHECKING

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

if TYPE_CHECKING:
    from suites import SuiteContext

from fleetlib.results import ResultSet
from suites import gate_enabled

from . import socket as _socket_mod
from . import broker_outage as _broker_mod
from . import vcore_drop as _vcore_mod

logger = logging.getLogger(__name__)

NAME = "faults"
HELP = "Recovery-asserting fault injection (socket exhaustion, broker outage, vcore drop)"

# cli scenario name -> submodule (each exposes run_device(device, ctx, rs))
SCENARIOS = {
    _socket_mod.SCENARIO: _socket_mod,
    _broker_mod.SCENARIO: _broker_mod,
    _vcore_mod.SCENARIO: _vcore_mod,
}


def add_arguments(parser) -> None:
    parser.add_argument(
        "--scenario", action="append", dest="scenarios",
        choices=["socket", "broker-outage", "vcore-drop", "all"],
        help="fault scenario to run (repeatable; default: all)",
    )
    parser.add_argument(
        "--cycles", type=int, default=3,
        help="broker-outage cycle count (default: 3)",
    )
    parser.add_argument(
        "--outage-duration", type=float, default=60.0,
        help="broker-down seconds per cycle (default: 60)",
    )
    parser.add_argument(
        "--reconnect-duration", type=float, default=30.0,
        help="broker-up/reconnect seconds per cycle (default: 30)",
    )
    parser.add_argument(
        "--socket-connections", type=int, default=None,
        help="connections per socket-churn cycle (default: 2x lwip_max_sockets)",
    )
    parser.add_argument(
        "--socket-cycles", type=int, default=1,
        help="socket-churn repetitions (default: 1)",
    )
    parser.add_argument(
        "--broker-container", default="mosquitto",
        help="docker container name for the MQTT broker (default: mosquitto)",
    )


def _active_scenarios(ctx) -> list:
    """Resolve selected scenarios honoring --scenario selection and check gates."""
    sel = ctx.extra.get("scenarios")
    if not sel or "all" in sel:
        sel = list(SCENARIOS.keys())
    out = []
    for name in sel:
        mod = SCENARIOS.get(name)
        if mod is None:
            continue
        if not gate_enabled(ctx, name):
            continue
        out.append((name, mod))
    return out


def run(ctx: "SuiteContext") -> ResultSet:
    rs = ctx.results
    active = _active_scenarios(ctx)
    logger.info("faults: %d scenario(s) on %d device(s)", len(active), len(ctx.devices))

    for device in ctx.devices:
        for _name, mod in active:
            mod.run_device(device, ctx, rs)

    if ctx.out_json:
        rs.to_json(ctx.out_json)
    if ctx.out_junit:
        rs.to_junit(ctx.out_junit)
    return rs
