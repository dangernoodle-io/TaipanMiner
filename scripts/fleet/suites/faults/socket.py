"""Socket-exhaustion fault scenario (folds socket_soak).

Drives /api/diag/sockets in_use toward lwip_max_sockets via many short-lived TCP
connections; asserts no crash, sockets drain, and recovery to baseline.
"""
from __future__ import annotations
import logging
import os
import socket as pysocket
import sys
import time
from typing import Optional, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from fleetlib.client import Client, TIMEOUT_INFO
from fleetlib.criteria import for_profile
from fleetlib.profiles import profile_for
from fleetlib.results import Result, STATUS_PASS, STATUS_FAIL, STATUS_SKIP

from ._common import (
    guard_step, sample_heap, sample_uptime, panic_signature, assert_recovery,
    DRYRUN, REFUSED,
)

logger = logging.getLogger(__name__)

SCENARIO = "socket"
UPTIME_REGRESSION_MS = 30_000


def _drive_sockets(ip: str, port: int, connections: int, hold: float = 0.05,
                   timeout: float = 2.0) -> int:
    """Open many short-lived TCP connections to churn the device's socket table.

    Returns the count of connections established. Stops early on refusal/exhaustion
    (expected as we approach the cap). Sockets are held briefly then all closed.
    """
    opened = 0
    socks = []
    try:
        for _ in range(connections):
            try:
                s = pysocket.create_connection((ip, port), timeout=timeout)
                socks.append(s)
                opened += 1
            except OSError:
                break
        time.sleep(hold)
    finally:
        for s in socks:
            try:
                s.close()
            except OSError:
                pass
    return opened


def _read_sockets(c) -> Optional[dict]:
    return c.get_json("/api/diag/sockets", timeout=TIMEOUT_INFO)


def _await_drain(c, max_sockets: int, attempts: int = 6, interval: float = 1.0,
                 floor_ratio: float = 0.75) -> Tuple[bool, str]:
    """Poll until in_use falls below floor_ratio * max_sockets."""
    threshold = max(2, int(max_sockets * floor_ratio))
    last = None
    for _ in range(attempts):
        s = _read_sockets(c)
        if s is not None:
            last = s.get("in_use")
            if last is not None and last < threshold:
                return True, f"in_use {last} < {threshold}"
        time.sleep(interval)
    return False, f"in_use stuck at {last} (>= {threshold})"


def run_device(device, ctx, rs) -> None:
    name = f"{device.ip}/faults/socket"
    profile = profile_for(device.board, ctx.profiles)
    criteria = for_profile(ctx.criteria, profile)

    readiness = ctx.settle.wait_ready(device, criteria)
    if not readiness.ready:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail=f"not ready after settle: {readiness.reason}"))
        return

    c = Client(device.ip, getattr(device, "port", 80))
    baseline_heap = sample_heap(c)
    baseline_uptime = sample_uptime(c)
    baseline_panic = panic_signature(c)
    sock0 = _read_sockets(c)
    if baseline_uptime is None or sock0 is None:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail="baseline unavailable (unreachable or no /api/diag/sockets)"))
        return

    # Destructive churn — gate through guard.
    outcome, gdetail = guard_step(ctx, device, "POST", "/api/diag/sockets")
    if outcome == DRYRUN:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP, detail=gdetail))
        return
    if outcome == REFUSED:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail=f"refused: {gdetail}"))
        return

    max_sockets = sock0.get("lwip_max_sockets") or 16
    connections = ctx.extra.get("socket_connections") or max(2 * max_sockets, 32)
    cycles = ctx.extra.get("socket_cycles") or 1
    port = getattr(device, "port", 80)

    peak_in_use = sock0.get("in_use", 0)
    for _ in range(cycles):
        _drive_sockets(device.ip, port, connections)
        s = _read_sockets(c)
        if s is None:
            up = sample_uptime(c)
            if up is None or (up + UPTIME_REGRESSION_MS < baseline_uptime):
                rs.add(Result(name=name, device=device, status=STATUS_FAIL,
                              detail="device crashed/unreachable during socket churn",
                              metrics={"peak_in_use": peak_in_use}))
                return
            continue
        peak_in_use = max(peak_in_use, s.get("in_use", 0))
        up = sample_uptime(c)
        if up is not None and up + UPTIME_REGRESSION_MS < baseline_uptime:
            rs.add(Result(name=name, device=device, status=STATUS_FAIL,
                          detail=f"uptime regression during churn ({up}ms < {baseline_uptime}ms)",
                          metrics={"peak_in_use": peak_in_use}))
            return

    drained, drain_detail = _await_drain(c, max_sockets)
    if not drained:
        rs.add(Result(name=name, device=device, status=STATUS_FAIL,
                      detail=f"sockets did not drain: {drain_detail}",
                      metrics={"peak_in_use": peak_in_use, "max_sockets": max_sockets}))
        return

    ok, detail, metrics = assert_recovery(c, profile, criteria, baseline_heap, baseline_panic)
    metrics["peak_in_use"] = peak_in_use
    metrics["max_sockets"] = max_sockets
    rs.add(Result(
        name=name, device=device,
        status=STATUS_PASS if ok else STATUS_FAIL,
        detail=(f"survived churn (peak {peak_in_use}/{max_sockets}), drained; {detail}"
                if ok else f"churn survived + drained but {detail}"),
        metrics=metrics,
    ))
