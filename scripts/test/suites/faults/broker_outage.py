"""Broker-outage fault scenario (folds broker_outage_repro / corepin_validate).

Cycles a local mosquitto broker down/up via docker; watches /api/info uptime,
/api/diag/panic and heap across cycles; asserts recovery each cycle. Requires
docker + the broker container on localhost — SKIPs (gated external dep) if absent.
"""
from __future__ import annotations
import logging
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from fleetlib.client import Client
from fleetlib.criteria import for_profile
from fleetlib.profiles import profile_for
from fleetlib.results import Result, STATUS_PASS, STATUS_FAIL, STATUS_SKIP

from ._common import (
    guard_step, sample_heap, sample_uptime, panic_signature, assert_recovery,
    DRYRUN, REFUSED,
)

logger = logging.getLogger(__name__)

SCENARIO = "broker-outage"
DEFAULT_CONTAINER = "mosquitto"


def _default_docker(action: str, container: str, timeout: int = 30):
    """Run `docker <action> <container>`. Returns (rc, output); rc None on error."""
    try:
        r = subprocess.run(["docker", action, container],
                           capture_output=True, text=True, timeout=timeout)
        return r.returncode, (r.stdout + r.stderr).strip()
    except FileNotFoundError:
        return None, "docker not found"
    except Exception as exc:  # noqa: BLE001
        return None, str(exc)


def _docker_runner(ctx):
    return ctx.extra.get("docker_runner") or _default_docker


def _broker_available(docker, container: str) -> bool:
    rc, _out = docker("inspect", container)
    return rc == 0


def run_device(device, ctx, rs) -> None:
    name = f"{device.ip}/faults/broker-outage"
    profile = profile_for(device.board, ctx.profiles)
    criteria = for_profile(ctx.criteria, profile)
    docker = _docker_runner(ctx)
    container = ctx.extra.get("broker_container") or DEFAULT_CONTAINER

    if not _broker_available(docker, container):
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail=f"docker/mosquitto unavailable (container {container!r}); "
                             f"gated external dep"))
        return

    readiness = ctx.settle.wait_ready(device, criteria)
    if not readiness.ready:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail=f"not ready after settle: {readiness.reason}"))
        return

    c = Client(device.ip, getattr(device, "port", 80))
    baseline_heap = sample_heap(c)
    baseline_uptime = sample_uptime(c)
    baseline_panic = panic_signature(c)
    if baseline_uptime is None:
        rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                      detail="baseline unavailable (unreachable)"))
        return

    cycles = int(ctx.extra.get("cycles") or 3)
    outage_s = float(ctx.extra.get("outage_duration") if ctx.extra.get("outage_duration") is not None else 60.0)
    reconnect_s = float(ctx.extra.get("reconnect_duration") if ctx.extra.get("reconnect_duration") is not None else 30.0)

    for cyc in range(1, cycles + 1):
        outcome, gdetail = guard_step(ctx, device, "POST", f"/__broker_outage__/{container}")
        if outcome == DRYRUN:
            rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                          detail=f"{gdetail} (cycle {cyc})"))
            return
        if outcome == REFUSED:
            rs.add(Result(name=name, device=device, status=STATUS_SKIP,
                          detail=f"refused: {gdetail}"))
            return

        docker("stop", container)
        time.sleep(outage_s)
        docker("start", container)
        time.sleep(reconnect_s)

        ok, detail, metrics = assert_recovery(c, profile, criteria, baseline_heap, baseline_panic)
        if not ok:
            metrics["cycle"] = cyc
            rs.add(Result(name=name, device=device, status=STATUS_FAIL,
                          detail=f"cycle {cyc}/{cycles}: {detail}", metrics=metrics))
            return

    rs.add(Result(name=name, device=device, status=STATUS_PASS,
                  detail=f"survived {cycles} broker-outage cycle(s) with recovery",
                  metrics={"cycles": cycles}))
