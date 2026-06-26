"""Shared helpers for fault scenarios — guard gating, sampling, recovery assertion."""
from __future__ import annotations
import logging
import os
import sys
from typing import Optional, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from fleetlib.client import TIMEOUT_INFO

logger = logging.getLogger(__name__)

# guard_step outcomes
PROCEED = "proceed"
DRYRUN = "dryrun"
REFUSED = "refused"


def guard_step(ctx, device, method: str, path: str) -> Tuple[str, str]:
    """Run ctx.guard.check; classify outcome without mutating on dry-run/refusal.

    Returns (PROCEED|DRYRUN|REFUSED, detail). REFUSED covers IdentityMismatch and
    RefusedWithoutConfirmation (and any guard error) — the caller records a skip.
    """
    try:
        result = ctx.guard.check(device, method, path)
    except Exception as exc:
        return REFUSED, f"{type(exc).__name__}: {exc}"
    if ctx.guard.is_dry_run_skip(result):
        return DRYRUN, f"dry-run: {method} {path} skipped"
    return PROCEED, ""


def sample_heap(c) -> Optional[int]:
    """heap.internal.free, or free_heap fallback, or None."""
    heap = c.get_json("/api/diag/heap", timeout=TIMEOUT_INFO)
    if heap is not None:
        free = (heap.get("internal") or {}).get("free")
        if free is not None:
            return free
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is not None:
        return info.get("free_heap")
    return None


def sample_uptime(c) -> Optional[int]:
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is None:
        return None
    return info.get("uptime_ms")


def sample_vcore(c) -> Optional[int]:
    s = c.get_json("/api/sensors", timeout=TIMEOUT_INFO)
    if s is None:
        return None
    return (s.get("miner") or {}).get("vcore_mv")


def panic_signature(c):
    """Hashable signature of /api/diag/panic, or None if unavailable."""
    pan = c.get_json("/api/diag/panic", timeout=TIMEOUT_INFO)
    if pan is None:
        return None
    return (pan.get("task"), pan.get("panic_reason"), pan.get("count"), pan.get("reset_reason"))


def heap_recovered(baseline, post, criteria) -> Tuple[bool, str]:
    """Pass if post >= heap_floor AND post >= 80% of baseline."""
    if baseline is None or post is None:
        return True, "heap data unavailable (leak check skipped)"
    if post < criteria.heap_floor:
        return False, f"post heap {post} < floor {criteria.heap_floor}"
    threshold = int(baseline * 0.8)
    if post < threshold:
        return False, f"post heap {post} < 80% of baseline {baseline}"
    return True, f"heap {post} >= floor and >= 80% of baseline {baseline}"


def assert_recovery(c, profile, criteria, baseline_heap, baseline_panic, timeout: int = 120):
    """wait_until_ready + heap-recovery + no-new-panic. Returns (ok, detail, metrics)."""
    from fleetlib.readiness import wait_until_ready
    rec = wait_until_ready(c, profile, criteria, timeout=timeout)
    if not rec.ready:
        return False, f"no recovery: {rec.reason}", {}
    post_heap = sample_heap(c)
    ok, hdetail = heap_recovered(baseline_heap, post_heap, criteria)
    if not ok:
        return False, hdetail, {"post_heap": post_heap}
    post_panic = panic_signature(c)
    if baseline_panic is not None and post_panic is not None and post_panic != baseline_panic:
        return False, f"new panic after recovery: {post_panic}", {"post_heap": post_heap}
    return True, f"recovered ({hdetail})", {
        "post_heap": post_heap,
        "recovery_s": round(rec.elapsed_s, 1),
    }
