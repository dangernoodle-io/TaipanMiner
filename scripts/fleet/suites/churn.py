"""Churn suite — SSE connect/disconnect load generation + heap-health sampling.

Purpose: actively fragment internal heap by repeatedly opening/closing SSE
connections against GET /api/events (breadboard bb_event_routes; topic filter
via ?topic=), while concurrently sampling heap health through the existing
monitor.poll()/_sample_device machinery. This validates the STATIC_POOL
anti-fragmentation fix under load, rather than passively observing (soak).

Reuses (no duplication):
  - fleetlib.monitor.poll / detectors_from_criteria / Sample / Anomaly — the
    same sampling + detector framework soak.py drives.
  - suites.soak._parse_duration / _append_sample_to_file — identical
    duration parsing and --samples-out NDJSON/CSV row shape as soak, so
    churn results are diffable against soak results.
"""
from __future__ import annotations
import logging
import os
import sys
import threading
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, List, Optional

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

if TYPE_CHECKING:
    from suites import SuiteContext

from fleetlib.criteria import for_profile
from fleetlib.monitor import poll, detectors_from_criteria, Sample
from fleetlib.profiles import profile_for
from fleetlib.results import ResultSet, Result, STATUS_PASS, STATUS_FAIL
from suites.soak import _parse_duration, _append_sample_to_file

logger = logging.getLogger(__name__)

NAME = "churn"
HELP = "SSE connect/disconnect churn load-gen + heap-health sampling (fragmentation validation)"

# Per-connection socket timeout for the churn SSE GET. Not exposed on the
# CLI: it bounds a single connect+read cycle, not the churn cadence.
_CHURN_CONNECT_TIMEOUT = 5.0
# Small read so the server's SSE buffer/task is forced into existence
# (real connect/read/close, not just a completed TCP handshake) without
# holding the connection open for the retained-topic replay window.
_CHURN_READ_CHUNK = 64
# Delay between churn cycles on each worker thread. Not exposed on the CLI
# (see module docstring); a fixed short cadence is sufficient stimulus.
_CHURN_CYCLE_SLEEP = 0.2


def add_arguments(parser) -> None:
    parser.add_argument(
        "--duration", type=_parse_duration, default=300.0,
        help="churn window: '30s', '2m', '1h', or bare seconds (default: 300s)",
    )
    parser.add_argument(
        "--interval", type=float, default=5.0,
        help="heap-sample poll interval in seconds (default: 5)",
    )
    parser.add_argument(
        "--sse-churn", type=int, default=2, dest="sse_churn",
        help="concurrent SSE churn connect/disconnect workers per host (default: 2)",
    )
    parser.add_argument(
        "--churn-topic", default="", dest="churn_topic",
        help="SSE topic filter for churn connections (default: '' = all attached "
             "topics). TaipanMiner attaches block.found, pool.notify, health.alerts "
             "to /api/events; health.alerts is the closest built-in analog to a "
             "'net.health' scope. See GET /api/diag/events on a live device for "
             "the authoritative attached-topic list.",
    )
    parser.add_argument(
        "--target", metavar="VERSION", default=None,
        help="expected firmware version; fail devices not running this version",
    )
    parser.add_argument(
        "--samples-out", dest="samples_out", metavar="PATH", default=None,
        help="write per-tick heap samples to file (.json = NDJSON, .csv with header); "
             "same row shape as `fleet soak --samples-out`",
    )


def run(ctx: "SuiteContext") -> ResultSet:
    """One thread per device (mirrors soak's TA-523 fix) so all hosts churn
    and sample concurrently for the whole --duration; a stuck/unreachable
    host cannot block the others.
    """
    rs = ctx.results

    if len(ctx.devices) <= 1:
        for device in ctx.devices:
            _run_device(device, ctx, rs)
    else:
        threads = [
            threading.Thread(target=_run_device, args=(device, ctx, rs), daemon=True)
            for device in ctx.devices
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

    if ctx.out_json:
        rs.to_json(ctx.out_json)
    if ctx.out_junit:
        rs.to_junit(ctx.out_junit)

    return rs


@dataclass
class _ChurnCounter:
    """Thread-safe successful-connect counter shared by a device's workers."""
    _lock: threading.Lock = field(default_factory=threading.Lock)
    successes: int = 0

    def inc(self) -> None:
        with self._lock:
            self.successes += 1


def _churn_once(url: str) -> bool:
    """Open one real SSE GET, read a small chunk, close the socket.

    Returns True on a successful connect — even if the follow-up read times
    out waiting for an event on a quiet/non-retained topic, the server has
    already allocated the per-client SSE buffer + task by the time urlopen()
    returns response headers, which is the actual fragmentation stimulus.
    Never raises: unreachable/erroring hosts return False so the caller can
    skip the cycle and keep looping (per-host isolation).
    """
    try:
        req = urllib.request.Request(url, headers={"Accept": "text/event-stream"})
        resp = urllib.request.urlopen(req, timeout=_CHURN_CONNECT_TIMEOUT)
    except Exception as exc:
        logger.debug("churn connect failed for %s: %s", url, exc)
        return False
    try:
        resp.read(_CHURN_READ_CHUNK)
    except Exception:
        pass  # idle topic / read timeout — connection was still established
    finally:
        try:
            resp.close()
        except Exception:
            pass
    return True


def _churn_worker(device, topic: str, stop_event: threading.Event, counter: _ChurnCounter) -> None:
    """Loop connect/read/close cycles against one device until stop_event fires.

    A failed cycle (unreachable device, refused connection, etc.) is skipped
    silently — the worker keeps looping and never aborts the run.
    """
    from fleetlib.client import Client
    c = Client(device.ip, device.port)
    path = "/api/events"
    if topic:
        path += f"?topic={urllib.parse.quote(topic)}"
    url = f"{c._base}{path}"

    while not stop_event.is_set():
        if _churn_once(url):
            counter.inc()
        stop_event.wait(_CHURN_CYCLE_SLEEP)


def _compute_summary_metrics(samples: list, duration: float, churn_conns: int) -> dict:
    """Per-run summary metrics from collected heap samples + churn connect count.

    free_min/min_free/largest_block_min mirror soak's heap_free_min /
    heap_min_free_min / largest_block_min (also set under those names so the
    two suites' --out-json / --samples-out are diffable against each other).
    """
    metrics: dict = {
        "duration_s": duration,
        "sample_count": len(samples),
        "churn_conns": churn_conns,
    }

    heap_frees: List[float] = []
    heap_min_frees: List[float] = []
    heap_largest: List[float] = []

    for s in samples:
        if not s.ok or not s.heap:
            continue
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

    if heap_frees:
        metrics["free_min"] = min(heap_frees)
        metrics["heap_free_min"] = metrics["free_min"]
    if heap_min_frees:
        metrics["min_free"] = min(heap_min_frees)
        metrics["heap_min_free_min"] = metrics["min_free"]
    if heap_largest:
        metrics["largest_block_min"] = min(heap_largest)

    return metrics


def _run_device(device, ctx: "SuiteContext", rs: ResultSet) -> None:
    profile = profile_for(device.board, ctx.profiles)
    criteria = for_profile(ctx.criteria, profile)

    duration = ctx.extra.get("duration") or 300.0
    interval = ctx.extra.get("interval") or 5.0
    sse_churn = ctx.extra.get("sse_churn")
    if sse_churn is None:
        sse_churn = 2
    churn_topic = ctx.extra.get("churn_topic") or ""
    samples_out_path = ctx.extra.get("samples_out")

    target = ctx.extra.get("target")
    if target and device.version != target:
        rs.add(Result(
            name=f"{device.ip}/churn",
            device=device,
            status=STATUS_FAIL,
            detail=f"version mismatch: running {device.version!r}, expected {target!r}",
        ))
        return

    # Reuse the monitor's aggregate detector factory (downtime/reboot/
    # reset_reason/wdt/heap_floor/heap_leak/publisher[+vcore if ASIC]) rather
    # than re-deriving anomaly signals — heap_floor/heap_leak are the
    # fragmentation-relevant ones; the rest are harmless no-ops without their
    # backing fields (we only fetch info+heap below).
    dets = detectors_from_criteria(criteria, profile=profile)

    counter = _ChurnCounter()
    stop_event = threading.Event()
    churn_threads = [
        threading.Thread(target=_churn_worker, args=(device, churn_topic, stop_event, counter), daemon=True)
        for _ in range(max(0, int(sse_churn)))
    ]
    for t in churn_threads:
        t.start()

    _collected_samples: List[Sample] = []

    def _on_sample(sample: Sample) -> None:
        _collected_samples.append(sample)
        if samples_out_path:
            _append_sample_to_file(sample, samples_out_path)

    logger.info(
        "churn %s (%s): duration=%.0fs interval=%.0fs sse_churn=%d topic=%r",
        device.ip, device.board, duration, interval, sse_churn, churn_topic or "<all>",
    )

    try:
        anomalies = poll(
            devices=[device],
            interval=interval,
            duration=duration,
            detectors=dets,
            fields=["info", "heap"],
            on_sample=_on_sample,
        )
    finally:
        # Always stop + join churn workers, even if poll() raises.
        stop_event.set()
        for t in churn_threads:
            t.join(timeout=5)

    summary_metrics = _compute_summary_metrics(_collected_samples, duration, counter.successes)
    summary_metrics["anomaly_count"] = len(anomalies)

    print(
        f"{device.ip} ({device.board}): samples={summary_metrics['sample_count']} "
        f"churn_conns={summary_metrics['churn_conns']} "
        f"free_min={summary_metrics.get('free_min')} "
        f"min_free={summary_metrics.get('min_free')} "
        f"largest_block_min={summary_metrics.get('largest_block_min')} "
        f"anomalies={len(anomalies)}",
        flush=True,
    )

    if anomalies:
        first = anomalies[0]
        detail = f"[{first.detector}] {first.message}"
        if len(anomalies) > 1:
            detail += f" (+ {len(anomalies) - 1} more)"
        rs.add(Result(
            name=f"{device.ip}/churn",
            device=device,
            status=STATUS_FAIL,
            detail=detail,
            metrics=summary_metrics,
        ))
    else:
        rs.add(Result(
            name=f"{device.ip}/churn",
            device=device,
            status=STATUS_PASS,
            detail=f"churned for {duration:.0f}s ({summary_metrics['churn_conns']} conns)",
            metrics=summary_metrics,
        ))
