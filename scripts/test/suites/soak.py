"""Soak suite — long-duration health monitoring across the fleet.

Drives monitor.poll() per device with the full detector set, suppressing
anomalies during the warmup/settle window.  TA-426: publisher verdict is
withheld until mqtt.connected is confirmed at least once.
"""
from __future__ import annotations
import csv as _csv_mod
import datetime
import json as _json_mod
import logging
import os
import sys
from typing import TYPE_CHECKING, List, Optional

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
    parser.add_argument(
        "--quiet", "--no-progress", dest="quiet", action="store_true", default=False,
        help="suppress per-tick live reporting (default: reporting on)",
    )
    parser.add_argument(
        "--samples-out", dest="samples_out", metavar="PATH", default=None,
        help="write per-tick samples to file (.json = NDJSON, .csv with header)",
    )
    parser.add_argument(
        "--attach-logs", dest="attach_logs",
        choices=["anomaly", "always", "never"], default="anomaly",
        help="attach device log tail to result: anomaly (default), always, never",
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


def _print_tick_row(sample: Sample, multi_host: bool) -> None:
    """Print a single per-tick row to stdout."""
    ts = datetime.datetime.now().strftime("%H:%M:%S")
    prefix = "~" if sample.warmup else " "
    host_tag = f"{sample.device.ip}  " if multi_host else ""

    parts: List[str] = []
    # heap
    if sample.heap is not None:
        internal = sample.heap.get("internal") or {}
        free = internal.get("free")
        min_free = internal.get("min_free")
        if free is not None:
            parts.append(f"heap={free:,}")
        if min_free is not None:
            parts.append(f"min_free={min_free:,}")
    elif sample.info is not None:
        free = sample.info.get("free_heap")
        if free is not None:
            parts.append(f"heap={free:,}")
    # uptime
    if sample.info is not None:
        uptime = sample.info.get("uptime_ms")
        if uptime is not None:
            parts.append(f"uptime={uptime // 1000}s")
        rr = sample.info.get("reset_reason")
        if rr and rr != "normal":
            parts.append(f"reset={rr}")
    # publisher/mqtt
    if sample.telemetry is not None:
        mqtt = sample.telemetry.get("mqtt") or {}
        pub = sample.telemetry.get("publisher") or {}
        if mqtt.get("enabled"):
            conn = "yes" if mqtt.get("connected") else "no"
            parts.append(f"mqtt={conn}")
        pub_ok = pub.get("last_publish_ok")
        if pub_ok is not None:
            parts.append(f"pub_ok={pub_ok}")
    # hashrate
    if sample.stats is not None:
        hr = sample.stats.get("hashrate_ghs") or sample.stats.get("hashrate")
        eghs = float(sample.stats.get("expected_ghs") or 0)
        if hr is not None:
            if eghs > 0:
                pct = hr / eghs * 100.0
                parts.append(f"hr={hr:.3f}GH/s({pct:.1f}%)")
            else:
                parts.append(f"hr={hr:.3f}GH/s")
    # vcore (ASIC)
    if sample.sensors is not None:
        miner = (sample.sensors.get("miner") or {})
        vcore = miner.get("vcore_mv")
        if vcore is not None:
            parts.append(f"vcore={vcore}mV")

    if not sample.ok:
        row = f"{prefix}{ts}  {host_tag}UNREACHABLE"
    else:
        row = f"{prefix}{ts}  {host_tag}{' '.join(parts) or 'ok'}"

    print(row, flush=True)


def _append_sample_to_file(sample: Sample, path: str) -> None:
    """Append one tick to the --samples-out file (NDJSON or CSV by extension)."""
    ts = datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    row: dict = {"ts": ts, "host": sample.device.ip, "warmup": sample.warmup}
    if sample.info:
        row["uptime_ms"] = sample.info.get("uptime_ms")
        row["reset_reason"] = sample.info.get("reset_reason")
    if sample.heap:
        internal = sample.heap.get("internal") or {}
        row["heap_free"] = internal.get("free")
        row["heap_min_free"] = internal.get("min_free")
        row["heap_largest_block"] = internal.get("largest_free_block")
    if sample.stats:
        row["hashrate"] = sample.stats.get("hashrate_ghs") or sample.stats.get("hashrate")
        row["expected_ghs"] = sample.stats.get("expected_ghs")
    if sample.sensors:
        miner = (sample.sensors.get("miner") or {})
        row["vcore_mv"] = miner.get("vcore_mv")
        row["temp_c"] = miner.get("temp_c") or (sample.sensors.get("thermal") or {}).get("temp_c")

    if path.endswith(".csv"):
        is_new = not os.path.exists(path) or os.path.getsize(path) == 0
        with open(path, "a", newline="") as f:
            writer = _csv_mod.DictWriter(f, fieldnames=list(row.keys()), extrasaction="ignore")
            if is_new:
                writer.writeheader()
            writer.writerow({k: ("" if v is None else v) for k, v in row.items()})
    else:
        # NDJSON: one JSON object per line for streaming
        with open(path, "a") as f:
            f.write(_json_mod.dumps(row) + "\n")


def _compute_summary_metrics(samples: list, duration: float) -> dict:
    """Compute per-run summary metrics from collected samples."""
    metrics: dict = {"duration_s": duration}

    heap_frees: List[float] = []
    heap_min_frees: List[float] = []
    heap_largest: List[float] = []
    hashrates: List[float] = []
    hashrate_pcts: List[float] = []
    temps: List[float] = []
    reboots = 0
    prev_uptime: Optional[float] = None

    for s in samples:
        if not s.ok:
            continue
        if s.heap:
            internal = s.heap.get("internal") or {}
            hf = internal.get("free")
            hmf = internal.get("min_free")
            hlb = internal.get("largest_free_block")
            if hf is not None:
                heap_frees.append(hf)
            if hmf is not None:
                heap_min_frees.append(hmf)
            if hlb is not None:
                heap_largest.append(hlb)
        if s.info:
            uptime = s.info.get("uptime_ms")
            if uptime is not None and prev_uptime is not None and uptime < prev_uptime:
                reboots += 1
            if uptime is not None:
                prev_uptime = uptime
        if s.stats:
            hr = s.stats.get("hashrate_ghs") or s.stats.get("hashrate")
            eghs = float(s.stats.get("expected_ghs") or 0)
            if hr is not None:
                hashrates.append(hr)
                if eghs > 0:
                    hashrate_pcts.append(hr / eghs * 100.0)
        if s.sensors:
            miner = (s.sensors.get("miner") or {})
            tc = miner.get("temp_c") or (s.sensors.get("thermal") or {}).get("temp_c")
            if tc is not None:
                temps.append(tc)

    if heap_frees:
        metrics["heap_free_min"] = min(heap_frees)
    if heap_min_frees:
        metrics["heap_min_free_min"] = min(heap_min_frees)
    if heap_largest:
        metrics["largest_block_min"] = min(heap_largest)
    if hashrates:
        metrics["hashrate_min"] = min(hashrates)
        metrics["hashrate_avg"] = sum(hashrates) / len(hashrates)
    if hashrate_pcts:
        metrics["hashrate_pct_expected_min"] = min(hashrate_pcts)
    if temps:
        metrics["temp_max"] = max(temps)
    metrics["reboot_count"] = reboots

    return metrics


def _run_device(device, ctx: "SuiteContext", rs: ResultSet) -> None:
    profile = profile_for(device.board, ctx.profiles)
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

    if gate_enabled(ctx, "hashrate"):
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
    if "stats" not in fields and gate_enabled(ctx, "hashrate"):
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

    quiet = ctx.extra.get("quiet", False)
    samples_out_path = ctx.extra.get("samples_out")
    attach_logs_mode = ctx.extra.get("attach_logs", "anomaly")

    _collected_samples: List[Sample] = []

    def _on_sample(sample: Sample) -> None:
        _collected_samples.append(sample)
        if not quiet:
            _print_tick_row(sample, len(ctx.devices) > 1)
        if samples_out_path:
            _append_sample_to_file(sample, samples_out_path)

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
        on_sample=_on_sample,
    )

    # Per-run summary metrics
    summary_metrics = _compute_summary_metrics(_collected_samples, duration)
    summary_metrics["anomaly_count"] = len(anomalies)

    # Log tail
    logs_attached: Optional[List[str]] = None
    if attach_logs_mode != "never":
        should_tail = (attach_logs_mode == "always") or (attach_logs_mode == "anomaly" and bool(anomalies))
        if should_tail:
            from fleetlib.client import Client
            from fleetlib.sse import tail_lines, SSEUnavailable
            c = Client(device.ip, device.port)
            try:
                logs_attached = tail_lines(c, max_lines=50, max_seconds=10.0)
            except SSEUnavailable as e:
                logger.warning("log tail unavailable on %s: %s", device.ip, e)
                logs_attached = None

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
            metrics=summary_metrics,
            logs=logs_attached,
        ))
    else:
        rs.add(Result(
            name=f"{device.ip}/soak",
            device=device,
            status=STATUS_PASS,
            detail=f"healthy for {duration:.0f}s",
            metrics=summary_metrics,
            logs=logs_attached,
        ))
