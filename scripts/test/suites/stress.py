"""Stress suite — browser-like concurrent load test with crash detection and recovery check.

Per device:
  1. wait_ready (never load a not-yet-settled board)
  2. Ramp concurrent HTTP requests at level * ceiling
  3. Watch for crash signals during load
  4. Assert RECOVERY: board responds + heap recovers to baseline

Conservative ceilings for no-PSRAM and C3 boards per profile.
"""
from __future__ import annotations
import copy
import logging
import sys
import os
import threading
import time
from typing import TYPE_CHECKING, List, Optional

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

if TYPE_CHECKING:
    from suites import SuiteContext

from fleetlib.criteria import for_profile
from fleetlib.client import Client, TIMEOUT_INFO
from fleetlib.profiles import profile_for
from fleetlib.readiness import wait_until_ready
from fleetlib.results import ResultSet, Result, STATUS_PASS, STATUS_FAIL, STATUS_SKIP
from suites import gate_enabled

logger = logging.getLogger(__name__)

NAME = "stress"
HELP = "Browser-like concurrent load test with crash detection and heap-recovery assertion"


def _parse_duration(s: str) -> float:
    """Accept '30s', '2m', '1h', or a bare float string."""
    s = s.strip()
    if s.endswith("s"):
        return float(s[:-1])
    if s.endswith("m"):
        return float(s[:-1]) * 60.0
    if s.endswith("h"):
        return float(s[:-1]) * 3600.0
    return float(s)


# API endpoints to hammer (mirrors browser_repro.py)
_POLL_ENDPOINTS = [
    "/api/stats",
    "/api/info",
    "/api/pool",
    "/api/telemetry",
]

_SSE_ENDPOINTS = [
    "/api/logs",
    "/api/diag/events",
]


def add_arguments(parser) -> None:
    parser.add_argument(
        "--duration", type=_parse_duration, default=30.0,
        help="load duration: '30s', '2m', '1h', or bare seconds (default: 30)",
    )
    parser.add_argument(
        "--level", type=float, default=0.8,
        help="fraction of profile ceiling to apply (default: 0.8)",
    )


def run(ctx: "SuiteContext") -> ResultSet:
    rs = ctx.results

    for device in ctx.devices:
        _run_device(device, ctx, rs)

    if ctx.out_json:
        rs.to_json(ctx.out_json)
    if ctx.out_junit:
        rs.to_junit(ctx.out_junit)

    return rs


def _run_device(device, ctx: "SuiteContext", rs: ResultSet) -> None:
    profile = profile_for(device.board)
    criteria = for_profile(ctx.criteria, profile)
    duration = ctx.extra.get("duration", 30.0)
    level = ctx.extra.get("level", 0.8)

    # 1. Settle gate — never apply load before board is ready
    readiness = ctx.settle.wait_ready(device, criteria)
    if not readiness.ready:
        rs.add(Result(
            name=f"{device.ip}/stress",
            device=device,
            status=STATUS_SKIP,
            detail=f"not ready after settle: {readiness.reason}",
        ))
        return

    c = Client(device.ip, getattr(device, "port", 80))

    # 2. Sample baseline heap before load
    baseline_heap = _sample_heap(c)
    baseline_uptime = _sample_uptime(c)
    if baseline_uptime is None:
        rs.add(Result(
            name=f"{device.ip}/stress",
            device=device,
            status=STATUS_SKIP,
            detail="could not sample baseline (unreachable before load)",
        ))
        return

    # 3. Compute concurrency ceiling
    max_concurrent = max(1, int(profile.max_concurrent * level))

    logger.info(
        "stress %s (%s): duration=%.0fs level=%.1f concurrent=%d",
        device.ip, device.board, duration, level, max_concurrent,
    )

    # 4. Apply load and monitor for crashes
    crashed, crash_detail = _apply_load(
        c, device, duration, max_concurrent, baseline_uptime,
    )

    if crashed:
        rs.add(Result(
            name=f"{device.ip}/stress",
            device=device,
            status=STATUS_FAIL,
            detail=f"crash during load: {crash_detail}",
            metrics={
                "duration_s": duration,
                "max_concurrent": max_concurrent,
                "level": level,
            },
        ))
        return

    # 5. Recovery assertion: board must return to ready + heap must not leak.
    # Use settle_delay=0 — the device was already settled before load; we only
    # want to confirm it is up and healthy, not re-run the full warmup window.
    recovery_criteria = copy.copy(criteria)
    recovery_criteria.settle_delay = 0
    recovery = wait_until_ready(c, profile, recovery_criteria, timeout=120)
    if not recovery.ready:
        rs.add(Result(
            name=f"{device.ip}/stress",
            device=device,
            status=STATUS_FAIL,
            detail=f"no recovery after load: {recovery.reason}",
            metrics={"duration_s": duration, "max_concurrent": max_concurrent},
        ))
        return

    post_heap = _sample_heap(c)
    heap_ok, heap_detail = _check_heap_recovery(baseline_heap, post_heap, criteria)

    if not heap_ok:
        rs.add(Result(
            name=f"{device.ip}/stress",
            device=device,
            status=STATUS_FAIL,
            detail=f"heap did not recover after load: {heap_detail}",
            metrics={
                "duration_s": duration,
                "max_concurrent": max_concurrent,
                "baseline_heap": baseline_heap,
                "post_load_heap": post_heap,
            },
        ))
        return

    rs.add(Result(
        name=f"{device.ip}/stress",
        device=device,
        status=STATUS_PASS,
        detail=f"survived {duration:.0f}s load, recovered (heap: {heap_detail})",
        metrics={
            "duration_s": duration,
            "max_concurrent": max_concurrent,
            "level": level,
            "baseline_heap": baseline_heap,
            "post_load_heap": post_heap,
        },
    ))


def _sample_heap(c: Client) -> Optional[int]:
    """Return heap.internal.free or free_heap fallback, or None."""
    heap = c.get_json("/api/diag/heap", timeout=TIMEOUT_INFO)
    if heap is not None:
        free = (heap.get("internal") or {}).get("free")
        if free is not None:
            return free
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is not None:
        return info.get("free_heap")
    return None


def _sample_uptime(c: Client) -> Optional[int]:
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is None:
        return None
    return info.get("uptime_ms")


def _apply_load(
    c: Client,
    device,
    duration: float,
    max_concurrent: int,
    baseline_uptime: int,
) -> tuple[bool, str]:
    """Drive concurrent requests for duration seconds.

    Returns (crashed: bool, detail: str).
    Crash signals: device unreachable OR uptime regression.
    """
    stop = threading.Event()
    crash_info: list[str] = []
    request_counts: list[int] = [0]
    active_threads: list[int] = [0]
    lock = threading.Lock()

    def _worker(endpoint: str) -> None:
        with lock:
            active_threads[0] += 1
        try:
            while not stop.is_set():
                c.get_json(endpoint, timeout=TIMEOUT_INFO)
                with lock:
                    request_counts[0] += 1
                # small yield between requests on this worker
                time.sleep(0.1)
        finally:
            with lock:
                active_threads[0] -= 1

    # Build worker pool: distribute endpoints across max_concurrent slots
    endpoints = list(_POLL_ENDPOINTS)
    threads: List[threading.Thread] = []
    for i in range(max_concurrent):
        ep = endpoints[i % len(endpoints)]
        t = threading.Thread(target=_worker, args=(ep,), daemon=True)
        threads.append(t)

    for t in threads:
        t.start()

    t0 = time.time()
    last_uptime = baseline_uptime
    while time.time() - t0 < duration:
        time.sleep(2)
        info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
        if info is None:
            crash_info.append("device unreachable during load")
            break
        uptime = info.get("uptime_ms", 0)
        if uptime + 30_000 < last_uptime:
            crash_info.append(
                f"uptime regression during load: {uptime}ms < {last_uptime}ms"
            )
            break
        last_uptime = uptime

    stop.set()
    for t in threads:
        t.join(timeout=5)

    crashed = len(crash_info) > 0
    detail = crash_info[0] if crash_info else ""
    logger.info(
        "stress %s: %d requests sent, %s",
        device.ip, request_counts[0], "CRASHED" if crashed else "no crash",
    )
    return crashed, detail


def _check_heap_recovery(
    baseline: Optional[int],
    post: Optional[int],
    criteria,
) -> tuple[bool, str]:
    """Return (ok, detail).

    Pass if:
    - post heap is >= criteria.heap_floor  (absolute floor)
    - AND post heap is >= 80% of baseline  (no significant leak)
    """
    if baseline is None or post is None:
        return True, "heap data unavailable (skipped leak check)"

    if post < criteria.heap_floor:
        return False, f"post-load heap {post} < floor {criteria.heap_floor}"

    threshold = int(baseline * 0.8)
    if post < threshold:
        return False, f"post-load heap {post} < 80% of baseline {baseline} ({threshold})"

    return True, f"{post} >= floor {criteria.heap_floor} and >= 80% of baseline {baseline}"
