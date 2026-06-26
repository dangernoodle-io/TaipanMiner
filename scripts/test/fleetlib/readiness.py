"""Post-reboot settle and readiness gate."""
from __future__ import annotations
import logging
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple, TYPE_CHECKING

from .client import Client, TIMEOUT_INFO

if TYPE_CHECKING:
    from .criteria import Criteria

logger = logging.getLogger(__name__)


@dataclass
class Readiness:
    ready: bool
    elapsed_s: float
    reason: str


@dataclass
class ReadinessSnapshot:
    """Normalised view of one device poll used by the shared readiness predicate.

    Fields:
        heap_free:      heap.internal.free (or free_heap flat fallback); None when unavailable.
        is_mining:      True when /api/stats reports expected_ghs > 0.
        hashrate_ghs:   current hashrate in GH/s; None when unavailable or non-mining.
        pool_connected: True/False when /api/pool returned a definitive answer; None when the
                        pool endpoint is absent or the key is missing (do not penalise).
    """
    heap_free: Optional[int]
    is_mining: bool
    hashrate_ghs: Optional[float]
    pool_connected: Optional[bool]


def is_ready(
    snapshot: ReadinessSnapshot,
    criteria: "Criteria",
) -> Tuple[bool, List[str]]:
    """Shared readiness predicate.  Pure function — no I/O, no side-effects.

    Returns (ready: bool, reasons: list[str]).  `reasons` is empty when ready.

    Logic (identical to what wait_until_ready previously computed inline):
    - heap_free >= criteria.readiness_heap_floor (required; None → not ready)
    - if is_mining:
        - pool_connected is not False  (None = unknown, not penalised)
        - hashrate_ghs > criteria.readiness_hashrate_min (only when min > 0)
    - non-mining boards: ready on heap + reachability alone

    Note: vcore_floor is NOT evaluated here because the snapshot doesn't carry
    sensor data.  wait_until_ready evaluates vcore separately (it has full API
    access); monitor warmup doesn't fetch /api/sensors during warmup evaluation
    and therefore omits vcore from the snapshot.
    """
    reasons: List[str] = []

    # 1. Heap floor
    if snapshot.heap_free is None:
        reasons.append("heap_free unavailable")
    elif snapshot.heap_free < criteria.readiness_heap_floor:
        reasons.append(
            f"heap_free {snapshot.heap_free} < floor {criteria.readiness_heap_floor}"
        )

    # 2 + 3. Pool connected + hashrate — mining boards only.
    # A board is considered mining-capable when expected_ghs > 0.
    # Non-mining boards (SW hash disabled, display-only, etc.) are ready on heap +
    # reachability alone — do not penalise them for an idle pool connection.
    if snapshot.is_mining:
        # pool_connected=None means the pool endpoint was absent or had no useful key;
        # treat as unknown (don't penalise).
        if snapshot.pool_connected is False:
            reasons.append("pool not connected")

        if criteria.readiness_hashrate_min > 0:
            hr = snapshot.hashrate_ghs or 0.0
            if hr <= criteria.readiness_hashrate_min:
                reasons.append(
                    f"hashrate {hr} <= min {criteria.readiness_hashrate_min}"
                )

    return len(reasons) == 0, reasons


def wait_until_ready(client, profile, criteria: "Criteria", timeout: int = 300) -> Readiness:
    """Poll until device is ready. Never returns before settle_delay has elapsed.

    Tolerates transient 5xx / connection refused (treats as not-ready, keeps polling).

    Readiness conditions (all must pass):
    - heap_internal.free >= criteria.readiness_heap_floor  (via is_ready)
    - pool connected when is_mining                        (via is_ready)
    - hashrate > criteria.readiness_hashrate_min           (via is_ready, when > 0)
    - vcore_mv >= criteria.readiness_vcore_floor           (checked here, when > 0)

    Args:
        client: fleetlib.Client OR fleetlib.Device (duck-typed: needs get_json or .ip/.port)
        profile: fleetlib.Profile or None
        criteria: Criteria with settle_delay and readiness_* fields
        timeout: max seconds to wait before declaring not-ready

    Returns:
        Readiness(ready, elapsed_s, reason)
    """
    # Resolve to a Client
    if hasattr(client, "get_json"):
        c = client
    else:
        # Assume Device-like with .ip / .port
        c = Client(client.ip, getattr(client, "port", 80))

    settle = criteria.settle_delay
    t0 = time.monotonic()
    settle_deadline = t0 + settle
    poll_interval = 5  # seconds between polls

    last_reason = "polling not started"

    while True:
        elapsed = time.monotonic() - t0
        if elapsed >= timeout:
            return Readiness(ready=False, elapsed_s=elapsed, reason=last_reason)

        # Poll device
        info = _safe_get(c, "/api/info", TIMEOUT_INFO)
        heap_free: Optional[int] = None

        if info is not None:
            # structured heap (B1-310+)
            heap = _safe_get(c, "/api/diag/heap", TIMEOUT_INFO)
            if heap is not None:
                heap_free = (heap.get("internal") or {}).get("free")
            # flat fallback for older firmware
            if heap_free is None:
                heap_free = info.get("free_heap")

        # Build snapshot for the shared predicate
        stats = _safe_get(c, "/api/stats", TIMEOUT_INFO)
        is_mining = stats is not None and (stats.get("expected_ghs") or 0.0) > 0

        hashrate: Optional[float] = None
        pool_connected: Optional[bool] = None

        if is_mining:
            pool = _safe_get(c, "/api/pool", TIMEOUT_INFO)
            if pool is not None:
                # Prefer "connected"; fall back to "pool_connected".
                # Use explicit key presence check — `or` would convert False→None
                # (falsy short-circuit) and break the `is False` test below.
                if "connected" in pool:
                    pool_connected = pool["connected"]
                elif "pool_connected" in pool:
                    pool_connected = pool["pool_connected"]
                # else: pool_connected stays None (endpoint present but no useful key)

            raw_hr = stats.get("hashrate_ghs") or stats.get("hashrate") or 0.0
            hashrate = float(raw_hr)

        snapshot = ReadinessSnapshot(
            heap_free=heap_free,
            is_mining=is_mining,
            hashrate_ghs=hashrate,
            pool_connected=pool_connected,
        )

        ready_flag, reasons = is_ready(snapshot, criteria)

        # 4. Vcore floor (ASIC boards) — checked here since snapshot doesn't carry sensors
        if criteria.readiness_vcore_floor > 0:
            sensors = _safe_get(c, "/api/sensors", TIMEOUT_INFO)
            if sensors is None:
                reasons.append("sensors unavailable")
            else:
                miner = sensors.get("miner") or {}
                vcore = miner.get("vcore_mv")
                if vcore is None:
                    reasons.append("vcore_mv unavailable")
                elif vcore < criteria.readiness_vcore_floor:
                    reasons.append(
                        f"vcore_mv {vcore} < floor {criteria.readiness_vcore_floor}"
                    )
            ready_flag = len(reasons) == 0

        elapsed = time.monotonic() - t0

        if ready_flag and elapsed >= settle:
            logger.debug(
                "device ready after %.1fs (settle_delay=%ds)", elapsed, settle
            )
            return Readiness(ready=True, elapsed_s=elapsed, reason="ready")

        if ready_flag:
            last_reason = f"waiting for settle_delay ({settle - elapsed:.0f}s remaining)"
        else:
            last_reason = "; ".join(reasons)
            logger.debug("not ready at %.1fs: %s", elapsed, last_reason)

        # Sleep, but wake up at settle_deadline if that comes first
        sleep_until = min(
            time.monotonic() + poll_interval,
            settle_deadline,
        )
        remaining = sleep_until - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)


def _safe_get(client, path: str, timeout: float) -> Optional[dict]:
    """GET path, returning None on any error (network, 5xx, parse)."""
    try:
        return client.get_json(path, timeout=timeout)
    except Exception:
        return None
