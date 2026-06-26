"""Generic fleet poll loop with pluggable detector framework.

Usage:
    detectors = detectors_from_criteria(criteria, profile=p, expected_ghs=485)
    anomalies = poll(devices, interval=60, duration=3600, detectors=detectors)
    if anomalies:
        sys.exit(1)
"""
from __future__ import annotations
import logging
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from .criteria import Criteria
    from .discovery import Device
    from .profiles import Profile

logger = logging.getLogger(__name__)


@dataclass
class Sample:
    device: "Device"
    timestamp: float
    info: Optional[dict]
    heap: Optional[dict]
    telemetry: Optional[dict]
    sensors: Optional[dict]
    stats: Optional[dict]
    ok: bool  # False when /api/info was unreachable


@dataclass
class Anomaly:
    device: "Device"
    detector: str   # detector name (e.g. "heap_floor", "reboot")
    message: str
    sample: Sample


# Detector signature: (sample, per-device-state-dict) -> Anomaly | None
Detector = Callable[[Sample, Dict[str, Any]], Optional[Anomaly]]

_ALL_FIELDS = ("info", "heap", "telemetry", "sensors", "stats")


def _sample_device(device: "Device", fields: tuple) -> Sample:
    from .client import Client, TIMEOUT_INFO, TIMEOUT_TELEMETRY
    c = Client(device.ip, device.port)
    info     = c.get_json("/api/info",       timeout=TIMEOUT_INFO)      if "info"      in fields else None
    heap     = c.get_json("/api/diag/heap",  timeout=TIMEOUT_INFO)      if "heap"      in fields else None
    tel      = c.get_json("/api/telemetry",  timeout=TIMEOUT_TELEMETRY) if "telemetry" in fields else None
    sensors  = c.get_json("/api/sensors",    timeout=TIMEOUT_INFO)      if "sensors"   in fields else None
    stats    = c.get_json("/api/stats",      timeout=TIMEOUT_INFO)      if "stats"     in fields else None
    return Sample(
        device=device,
        timestamp=time.time(),
        info=info,
        heap=heap,
        telemetry=tel,
        sensors=sensors,
        stats=stats,
        ok=info is not None,
    )


def poll(
    devices: List["Device"],
    interval: float,
    duration: float,
    detectors: List[Detector],
    fields: Optional[List[str]] = None,
) -> List[Anomaly]:
    """Poll all devices for `duration` seconds at `interval` second ticks.

    Each tick: sample each device then run all detectors against the sample.
    Anomalies are accumulated; polling continues to completion (does not stop
    on first anomaly — callers decide what to do with the list).

    Args:
        devices:   list of Device to monitor
        interval:  seconds between poll ticks (including sampling time)
        duration:  total seconds to run
        detectors: list of Detector callables
        fields:    subset of ["info","heap","telemetry","sensors","stats"]
                   (default: ["info","heap","telemetry"])

    Returns:
        List of Anomaly detected across the run (may be empty).
    """
    if fields is None:
        fields = ["info", "heap", "telemetry"]
    fset = tuple(fields)

    # Per-device state dict persists across ticks for stateful detectors
    state: Dict[str, Dict[str, Any]] = {d.ip: {} for d in devices}
    anomalies: List[Anomaly] = []
    t0 = time.time()

    while time.time() - t0 < duration:
        tick_start = time.time()
        for device in devices:
            sample = _sample_device(device, fset)
            st = state[device.ip]
            for det in detectors:
                try:
                    a = det(sample, st)
                    if a is not None:
                        logger.warning(
                            "ANOMALY [%s] %s (%s): %s",
                            a.detector, device.ip, device.board, a.message,
                        )
                        anomalies.append(a)
                except Exception as exc:
                    logger.error("detector error on %s: %s", device.ip, exc)

        elapsed = time.time() - tick_start
        sleep_time = max(0.0, interval - elapsed)
        if sleep_time > 0:
            time.sleep(sleep_time)

    return anomalies


# ---------------------------------------------------------------------------
# Detector factories
# ---------------------------------------------------------------------------

def make_heap_floor_detector(criteria: "Criteria") -> Detector:
    """Anomaly when heap.internal.free drops below the floor."""
    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not sample.ok:
            return None
        heap = sample.heap or {}
        free = (heap.get("internal") or {}).get("free")
        # flat-field fallback for firmware older than B1-310
        if free is None and sample.info:
            free = sample.info.get("free_heap")
        if free is not None and free < criteria.heap_floor:
            return Anomaly(
                sample.device, "heap_floor",
                f"heap.internal.free={free} < floor {criteria.heap_floor}", sample,
            )
        return None
    return _detect


def make_heap_leak_detector(criteria: "Criteria") -> Detector:
    """Anomaly when min_free_ever declines (memory is being leaked)."""
    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not sample.ok or not criteria.heap_leak_check:
            return None
        heap = sample.heap or {}
        min_free = (heap.get("internal") or {}).get("min_free")
        if min_free is None:
            return None
        prev = state.get("min_free_ever")
        if prev is not None and min_free < prev:
            return Anomaly(
                sample.device, "heap_leak",
                f"min_free declined: {min_free} < prev {prev}", sample,
            )
        # only update the watermark; never allow it to climb (tracks the true minimum)
        state["min_free_ever"] = min_free if prev is None else min(prev, min_free)
        return None
    return _detect


def make_reboot_detector(criteria: "Criteria") -> Detector:
    """Anomaly when uptime_ms regresses by more than reboot_tolerance_ms."""
    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not sample.ok or sample.info is None:
            return None
        uptime = sample.info.get("uptime_ms", 0)
        max_up = state.get("max_uptime_ms", 0)
        if uptime + criteria.reboot_tolerance_ms < max_up:
            return Anomaly(
                sample.device, "reboot",
                f"uptime {uptime}ms < seen {max_up}ms "
                f"(regression > {criteria.reboot_tolerance_ms}ms)", sample,
            )
        state["max_uptime_ms"] = max(max_up, uptime)
        return None
    return _detect


def make_reset_reason_detector(criteria: "Criteria") -> Detector:
    """Anomaly when a bad reset_reason appears on a freshly-booted device."""
    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not sample.ok or sample.info is None:
            return None
        rr = sample.info.get("reset_reason")
        uptime = sample.info.get("uptime_ms", 0)
        if rr in criteria.bad_reset_reasons and uptime < 150_000:
            return Anomaly(
                sample.device, "reset_reason",
                f"reset_reason={rr!r} with uptime {uptime}ms", sample,
            )
        return None
    return _detect


def make_wdt_detector(criteria: "Criteria") -> Detector:
    """Anomaly when wdt_resets count increases between polls."""
    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not criteria.wdt_resets_flat or not sample.ok or sample.info is None:
            return None
        wdt = sample.info.get("wdt_resets")
        if wdt is None:
            return None
        prev = state.get("wdt_resets")
        if prev is not None and wdt > prev:
            return Anomaly(
                sample.device, "wdt_increase",
                f"wdt_resets increased: {prev} -> {wdt}", sample,
            )
        state["wdt_resets"] = wdt
        return None
    return _detect


def make_publisher_detector(criteria: "Criteria") -> Detector:
    """Anomaly when telemetry publisher has last_publish_ok=false for too many polls."""
    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not sample.ok or sample.telemetry is None:
            return None
        tel = sample.telemetry
        mqtt = tel.get("mqtt") or {}
        http_cfg = tel.get("http") or {}
        if not mqtt.get("enabled") and not http_cfg.get("enabled"):
            return None  # no sink configured — skip
        pub = tel.get("publisher") or {}
        pub_ok = pub.get("last_publish_ok")
        if pub_ok is False:
            count = state.get("pubfail", 0) + 1
            state["pubfail"] = count
            if count >= criteria.publisher_max_polls:
                return Anomaly(
                    sample.device, "publisher_down",
                    f"last_publish_ok=false for {count} consecutive polls", sample,
                )
        else:
            state["pubfail"] = 0
        return None
    return _detect


def make_hashrate_detector(criteria: "Criteria", expected_ghs: float) -> Detector:
    """Anomaly when reported hashrate falls below the floor."""
    floor = expected_ghs * criteria.hashrate_floor_pct / 100.0

    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not sample.ok or sample.stats is None:
            return None
        hr = sample.stats.get("hashrate_ghs") or sample.stats.get("hashrate")
        if hr is not None and hr < floor:
            return Anomaly(
                sample.device, "hashrate_floor",
                f"hashrate {hr:.2f} GH/s < floor {floor:.2f} "
                f"({criteria.hashrate_floor_pct}% of {expected_ghs} GH/s)", sample,
            )
        return None
    return _detect


def make_vcore_detector(criteria: "Criteria") -> Detector:
    """ASIC: anomaly on vcore_mv below floor or vcore_restart_count increase."""
    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not sample.ok or sample.sensors is None:
            return None
        miner = (sample.sensors.get("miner") or {})
        vcore = miner.get("vcore_mv")
        if vcore is not None and vcore < criteria.vcore_floor_mv:
            return Anomaly(
                sample.device, "vcore",
                f"vcore_mv={vcore} < floor {criteria.vcore_floor_mv}mV", sample,
            )
        if criteria.vcore_restart_flat:
            rc = miner.get("vcore_restart_count")
            if rc is not None:
                prev = state.get("vcore_restart_count")
                if prev is not None and rc > prev:
                    return Anomaly(
                        sample.device, "vcore_restart",
                        f"vcore_restart_count increased: {prev} -> {rc}", sample,
                    )
                state["vcore_restart_count"] = rc
        return None
    return _detect


def make_downtime_detector(criteria: "Criteria") -> Detector:
    """Anomaly when a device misses too many consecutive polls."""
    def _detect(sample: Sample, state: Dict) -> Optional[Anomaly]:
        if not sample.ok:
            miss = state.get("missed_polls", 0) + 1
            state["missed_polls"] = miss
            if miss >= criteria.max_missed_polls:
                return Anomaly(
                    sample.device, "downtime",
                    f"{miss} consecutive missed polls", sample,
                )
        else:
            state["missed_polls"] = 0
        return None
    return _detect


def detectors_from_criteria(
    criteria: "Criteria",
    profile: Optional["Profile"] = None,
    expected_ghs: float = 0.0,
) -> List[Detector]:
    """Build the standard detector list from a Criteria (+ optional Profile).

    Always included: downtime, reboot, reset_reason, wdt, heap_floor, heap_leak, publisher.
    ASIC profile: adds vcore detector.
    expected_ghs > 0: adds hashrate detector.
    """
    dets: List[Detector] = [
        make_downtime_detector(criteria),
        make_reboot_detector(criteria),
        make_reset_reason_detector(criteria),
        make_wdt_detector(criteria),
        make_heap_floor_detector(criteria),
        make_heap_leak_detector(criteria),
        make_publisher_detector(criteria),
    ]
    if profile is not None and profile.is_asic:
        dets.append(make_vcore_detector(criteria))
    if expected_ghs > 0:
        dets.append(make_hashrate_detector(criteria, expected_ghs))
    return dets
