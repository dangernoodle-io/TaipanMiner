"""Post-reboot settle and readiness gate."""
from __future__ import annotations
import logging
import time
from dataclasses import dataclass
from typing import Optional, TYPE_CHECKING

from .client import Client, TIMEOUT_INFO

if TYPE_CHECKING:
    from .criteria import Criteria

logger = logging.getLogger(__name__)


@dataclass
class Readiness:
    ready: bool
    elapsed_s: float
    reason: str


def wait_until_ready(client, profile, criteria: "Criteria", timeout: int = 300) -> Readiness:
    """Poll until device is ready. Never returns before settle_delay has elapsed.

    Tolerates transient 5xx / connection refused (treats as not-ready, keeps polling).

    Readiness conditions (all must pass):
    - heap_internal.free >= criteria.readiness_heap_floor
    - pool connected (if /api/pool exists and returns data)
    - hashrate > criteria.readiness_hashrate_min (if > 0)
    - vcore_mv >= criteria.readiness_vcore_floor (if > 0)

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

        # Evaluate conditions
        reasons = []

        # 1. Heap floor
        if heap_free is None:
            reasons.append("heap_free unavailable")
        elif heap_free < criteria.readiness_heap_floor:
            reasons.append(
                f"heap_free {heap_free} < floor {criteria.readiness_heap_floor}"
            )

        # 2. Pool connected (optional endpoint)
        if not reasons or True:  # always check
            pool = _safe_get(c, "/api/pool", TIMEOUT_INFO)
            if pool is not None:
                # Pool endpoint exists — check connection
                connected = pool.get("connected") or pool.get("pool_connected")
                if connected is False:
                    reasons.append("pool not connected")

        # 3. Hashrate minimum
        if criteria.readiness_hashrate_min > 0:
            stats = _safe_get(c, "/api/stats", TIMEOUT_INFO)
            if stats is None:
                reasons.append("stats unavailable")
            else:
                hr = stats.get("hashrate_ghs") or stats.get("hashrate") or 0.0
                if hr <= criteria.readiness_hashrate_min:
                    reasons.append(f"hashrate {hr} <= min {criteria.readiness_hashrate_min}")

        # 4. Vcore floor (ASIC boards)
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

        conditions_met = len(reasons) == 0
        elapsed = time.monotonic() - t0

        if conditions_met and elapsed >= settle:
            logger.debug(
                "device ready after %.1fs (settle_delay=%ds)", elapsed, settle
            )
            return Readiness(ready=True, elapsed_s=elapsed, reason="ready")

        if conditions_met:
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
