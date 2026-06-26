"""Vcore-drop fault scenario — ASIC boards only.

Uses the debug hook POST /api/diag/vcore-drop to collapse vcore, then asserts the
latch/recovery behavior. MUTATING debug op → MUST go through ctx.guard
(identity-verify + dry-run + confirm). Skips on non-ASIC boards or when the hook
is absent from the device spec.
"""
from __future__ import annotations
import logging
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from fleetlib.client import Client, TIMEOUT_WRITE
from fleetlib.criteria import for_profile
from fleetlib.profiles import profile_for
from fleetlib.results import Result, STATUS_PASS, STATUS_FAIL, STATUS_SKIP

from ._common import (
    guard_step, sample_heap, sample_uptime, sample_vcore, panic_signature,
    assert_recovery, DRYRUN, REFUSED,
)

logger = logging.getLogger(__name__)

SCENARIO = "vcore-drop"
HOOK_PATH = "/api/diag/vcore-drop"


def _hook_present(c):
    """True/False if the device spec advertises the hook; None if spec unknown."""
    spec = c.spec
    if not spec:
        return None
    paths = spec.get("paths") or {}
    return HOOK_PATH in paths


def run_device(device, ctx, rs) -> None:
    name = f"{device.ip}/faults/vcore-drop"
    profile = profile_for(device.board, ctx.profiles)

    if not profile.is_asic:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail="non-ASIC board; vcore-drop refused"))
        return

    criteria = for_profile(ctx.criteria, profile)
    # Assert vcore recovers to floor as part of readiness.
    if criteria.vcore_floor_mv:
        criteria.readiness_vcore_floor = criteria.vcore_floor_mv

    readiness = ctx.settle.wait_ready(device, criteria)
    if not readiness.ready:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail=f"not ready after settle: {readiness.reason}"))
        return

    c = Client(device.ip, getattr(device, "port", 80))

    present = _hook_present(c)
    if present is False:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail=f"debug hook {HOOK_PATH} absent"))
        return

    baseline_heap = sample_heap(c)
    baseline_uptime = sample_uptime(c)
    baseline_panic = panic_signature(c)
    baseline_vcore = sample_vcore(c)
    if baseline_uptime is None:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail="baseline unavailable (unreachable)"))
        return

    # MUTATING debug op — gate through guard.
    outcome, gdetail = guard_step(ctx, device, "POST", HOOK_PATH)
    if outcome == DRYRUN:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP, detail=gdetail))
        return
    if outcome == REFUSED:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail=f"refused: {gdetail}"))
        return

    # Inject: collapse vcore via the debug hook (board may briefly reset).
    c.request("POST", HOOK_PATH, body={"collapse": True}, timeout=TIMEOUT_WRITE)

    ok, detail, metrics = assert_recovery(c, profile, criteria, baseline_heap, baseline_panic)
    metrics["baseline_vcore_mv"] = baseline_vcore
    metrics["post_vcore_mv"] = sample_vcore(c)
    rs.add(Result(
        name=name, device=device,
        status=STATUS_PASS if ok else STATUS_FAIL,
        detail=(f"vcore-drop latched + recovered; {detail}" if ok
                else f"vcore-drop injected but {detail}"),
        metrics=metrics,
    ))
