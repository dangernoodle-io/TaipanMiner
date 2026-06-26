"""Soak suite — long-duration health monitoring across the fleet.

Drives monitor.poll() per device with the full detector set, suppressing
anomalies during the warmup/settle window.  TA-426: publisher verdict is
withheld until mqtt.connected is confirmed at least once.
"""
from __future__ import annotations
import logging
import sys
import os
from typing import TYPE_CHECKING

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

if TYPE_CHECKING:
    from suites import SuiteContext

from fleetlib.criteria import for_profile
from fleetlib.monitor import (
    poll,
    detectors_from_criteria,
    Anomaly,
    Sample,
    Detector,
)
from fleetlib.profiles import profile_for
from fleetlib.results import ResultSet, Result, STATUS_PASS, STATUS_FAIL
from suites import gate_enabled

logger = logging.getLogger(__name__)

NAME = "soak"
HELP = "Long-duration fleet health soak (heap, reboot, WDT, hashrate, publisher)"


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


def add_arguments(parser) -> None:
    parser.add_argument(
        "--duration", type=_parse_duration, default=None,
        help="soak window: '30s', '2m', '1h', or bare seconds (default: criteria.duration)",
    )
    parser.add_argument(
        "--interval", type=float, default=None,
        help="poll interval in seconds (default: criteria.poll_interval)",
    )
    parser.add_argument(
        "--target", metavar="VERSION", default=None,
        help="expected firmware version; fail devices not running this version",
    )
    parser.add_argument(
        "--expected-ghs", type=float, default=None, dest="expected_ghs",
        help="hashrate floor override in GH/s (default: read from device /api/stats); 0 disables floor check",
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


def _build_publisher_gate_detector(criteria, gate_name: str) -> Detector:
    """TA-426: wrap publisher detector to suppress until mqtt.connected is seen once."""
    from fleetlib.monitor import make_publisher_detector
    inner = make_publisher_detector(criteria)

    def _detect(sample: Sample, state: dict):
        if sample.ok and sample.telemetry is not None:
            mqtt = sample.telemetry.get("mqtt") or {}
            if mqtt.get("connected"):
                state["mqtt_ever_connected"] = True

        # Suppress publisher verdict until broker connection confirmed at least once
        if not state.get("mqtt_ever_connected"):
            return None

        return inner(sample, state)

    return _detect


def _run_device(device, ctx: "SuiteContext", rs: ResultSet) -> None:
    profile = profile_for(device.board)
    criteria = for_profile(ctx.criteria, profile)

    duration = ctx.extra.get("duration") or criteria.duration
    interval = ctx.extra.get("interval") or criteria.poll_interval
    settle_delay = ctx.settle.settle_delay if ctx.settle.enabled else 0

    # Build detectors, honouring gates
    dets: list[Detector] = []

    if gate_enabled(ctx, "downtime"):
        from fleetlib.monitor import make_downtime_detector
        dets.append(make_downtime_detector(criteria))

    if gate_enabled(ctx, "reboot"):
        from fleetlib.monitor import make_reboot_detector
        dets.append(make_reboot_detector(criteria))

    if gate_enabled(ctx, "reset_reason"):
        from fleetlib.monitor import make_reset_reason_detector
        dets.append(make_reset_reason_detector(criteria))

    if gate_enabled(ctx, "wdt"):
        from fleetlib.monitor import make_wdt_detector
        dets.append(make_wdt_detector(criteria))

    if gate_enabled(ctx, "heap_floor"):
        from fleetlib.monitor import make_heap_floor_detector
        dets.append(make_heap_floor_detector(criteria))

    if gate_enabled(ctx, "heap_leak"):
        from fleetlib.monitor import make_heap_leak_detector
        dets.append(make_heap_leak_detector(criteria))

    if gate_enabled(ctx, "publisher"):
        dets.append(_build_publisher_gate_detector(criteria, "publisher"))

    if gate_enabled(ctx, "hashrate") and profile.is_asic:
        from fleetlib.monitor import make_hashrate_detector
        # CLI override takes precedence; otherwise read expected_ghs per sample from /api/stats.
        # The floor check is enabled only when the resolved value > 0.
        _override_ghs = ctx.extra.get("expected_ghs") or 0.0

        def _live_hashrate_detector(sample: Sample, state: dict,
                                    _override: float = _override_ghs) -> object:
            if _override > 0:
                eghs = _override
            elif sample.stats is not None:
                eghs = float(sample.stats.get("expected_ghs") or 0)
            else:
                eghs = 0.0
            if eghs <= 0:
                return None
            inner_key = "_hr_inner"
            eghs_key = "_hr_eghs"
            if inner_key not in state or state.get(eghs_key) != eghs:
                state[inner_key] = make_hashrate_detector(criteria, eghs)
                state[eghs_key] = eghs
            return state[inner_key](sample, state)

        dets.append(_live_hashrate_detector)

    if gate_enabled(ctx, "vcore") and profile.is_asic:
        from fleetlib.monitor import make_vcore_detector
        dets.append(make_vcore_detector(criteria))

    # Determine fields to fetch
    fields = list(ctx.fields) if ctx.fields else ["info", "heap", "telemetry"]
    if profile.is_asic and "sensors" not in fields:
        fields.append("sensors")
    if "stats" not in fields and gate_enabled(ctx, "hashrate") and profile.is_asic:
        fields.append("stats")

    # Version check
    target = ctx.extra.get("target")
    if target and device.version != target:
        rs.add(Result(
            name=f"{device.ip}/soak",
            device=device,
            status=STATUS_FAIL,
            detail=f"version mismatch: running {device.version!r}, expected {target!r}",
        ))
        return

    logger.info(
        "soak %s (%s): duration=%.0fs interval=%.0fs settle=%ds %d detectors",
        device.ip, device.board, duration, interval, settle_delay, len(dets),
    )

    anomalies = poll(
        devices=[device],
        interval=interval,
        duration=duration,
        detectors=dets,
        fields=fields,
        settle_delay=settle_delay,
    )

    if anomalies:
        first = anomalies[0]
        detail = f"[{first.detector}] {first.message}"
        if len(anomalies) > 1:
            detail += f" (+ {len(anomalies) - 1} more)"
        rs.add(Result(
            name=f"{device.ip}/soak",
            device=device,
            status=STATUS_FAIL,
            detail=detail,
            metrics={
                "anomalies": len(anomalies),
                "duration_s": duration,
            },
        ))
    else:
        rs.add(Result(
            name=f"{device.ip}/soak",
            device=device,
            status=STATUS_PASS,
            detail=f"healthy for {duration:.0f}s",
            metrics={"duration_s": duration},
        ))
