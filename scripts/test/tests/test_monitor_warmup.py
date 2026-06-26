"""Tests for fleetlib.monitor warmup suppression."""
import os
import sys
import time
import unittest
from unittest.mock import patch
from typing import Any, Dict, Optional

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.criteria import Criteria
from fleetlib.monitor import (
    Anomaly,
    Sample,
    poll,
    make_heap_floor_detector,
)
from fleetlib.discovery import Device


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_device(ip: str = "192.0.2.1", board: str = "esp32-wroom32") -> Device:
    return Device(hostname=board, ip=ip, port=80, board=board, version="v0.69.0")


def _make_sample(device: Device, ok: bool = True, free: int = 30_000, uptime_ms: int = 5000) -> Sample:
    return Sample(
        device=device,
        timestamp=time.time(),
        info={"uptime_ms": uptime_ms, "reset_reason": "normal", "wdt_resets": 0} if ok else None,
        heap={"internal": {"free": free, "min_free": free - 1000}} if ok else None,
        telemetry=None,
        sensors=None,
        stats=None,
        ok=ok,
    )


def _always_anomaly(sample: Sample, state: Dict[str, Any]) -> Optional[Anomaly]:
    """Test detector that always fires when device is reachable."""
    if not sample.ok:
        return None
    return Anomaly(
        device=sample.device,
        detector="always_fire",
        message="test anomaly",
        sample=sample,
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestNoAnomalyDuringWarmup(unittest.TestCase):
    """Anomalies fired during the warmup window must be suppressed."""

    def test_suppressed_during_warmup(self):
        """settle_delay=3600 → entire 0.3s run is inside warmup → 0 anomalies."""
        device = _make_device()

        def _fake_sample(dev, fset):
            # Low heap — would trigger heap_floor if not suppressed
            return _make_sample(dev, free=10_000)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            criteria = Criteria(heap_floor=50_000)
            anomalies = poll(
                devices=[device],
                interval=0.05,
                duration=0.3,
                detectors=[make_heap_floor_detector(criteria)],
                settle_delay=3600,  # 1h warmup — never expires during 0.3s run
            )

        self.assertEqual(
            len(anomalies), 0,
            f"expected 0 anomalies during warmup, got {len(anomalies)}: {anomalies}",
        )

    def test_anomaly_fires_after_warmup(self):
        """settle_delay=0 → no warmup → heap anomaly fires immediately."""
        device = _make_device()

        def _fake_sample(dev, fset):
            return _make_sample(dev, free=10_000)  # always below floor

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            criteria = Criteria(heap_floor=50_000)
            anomalies = poll(
                devices=[device],
                interval=0.05,
                duration=0.3,
                detectors=[make_heap_floor_detector(criteria)],
                settle_delay=0,  # no warmup
            )

        self.assertGreater(
            len(anomalies), 0,
            "expected anomaly when settle_delay=0 and heap below floor",
        )


class TestAnomalyAfterWarmup(unittest.TestCase):
    """Directly verify detector behavior (no poll machinery)."""

    def test_heap_floor_fires_without_warmup(self):
        """Heap below floor → detector fires (no poll, no warmup involved)."""
        device = _make_device()
        criteria = Criteria(heap_floor=50_000, settle_delay=0)
        det = make_heap_floor_detector(criteria)

        sample = _make_sample(device, free=30_000)
        state: Dict[str, Any] = {}
        anomaly = det(sample, state)
        self.assertIsNotNone(anomaly)
        self.assertEqual(anomaly.detector, "heap_floor")

    def test_heap_floor_suppressed_by_warmup_via_poll(self):
        """settle_delay=3600: heap below floor, but all anomalies suppressed during warmup."""
        device = _make_device()

        def _fake_sample(dev, fset):
            return _make_sample(dev, free=30_000)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            criteria = Criteria(heap_floor=50_000)
            anomalies = poll(
                devices=[device],
                interval=0.05,
                duration=0.3,
                detectors=[make_heap_floor_detector(criteria)],
                settle_delay=3600,
            )

        self.assertEqual(
            len(anomalies), 0,
            f"expected 0 anomalies (all suppressed by warmup), got {len(anomalies)}",
        )


class TestWarmupRearmsOnReboot(unittest.TestCase):
    """Uptime regression mid-run must re-arm the warmup window."""

    def test_warmup_rearms_on_uptime_regression(self):
        """High uptime → low uptime (reboot) → warmup re-armed → post-reboot anomalies suppressed."""
        device = _make_device()
        call_count = {"n": 0}

        def _fake_sample(dev, fset):
            call_count["n"] += 1
            n = call_count["n"]
            if n == 1:
                # First tick: device up 10 min, heap healthy
                return Sample(
                    device=dev, timestamp=time.time(),
                    info={"uptime_ms": 600_000, "reset_reason": "normal", "wdt_resets": 0},
                    heap={"internal": {"free": 80_000, "min_free": 75_000}},
                    telemetry=None, sensors=None, stats=None, ok=True,
                )
            # Subsequent ticks: device rebooted (uptime regressed), heap low
            return Sample(
                device=dev, timestamp=time.time(),
                info={"uptime_ms": 5_000, "reset_reason": "normal", "wdt_resets": 0},
                heap={"internal": {"free": 20_000, "min_free": 18_000}},
                telemetry=None, sensors=None, stats=None, ok=True,
            )

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            criteria = Criteria(heap_floor=50_000)
            anomalies = poll(
                devices=[device],
                interval=0.05,
                duration=0.4,
                detectors=[make_heap_floor_detector(criteria)],
                settle_delay=3600,  # warmup re-arm lasts 3600s after reboot
            )

        self.assertEqual(
            len(anomalies), 0,
            f"expected post-reboot anomaly to be suppressed by rearm; got {len(anomalies)}: {anomalies}",
        )
        self.assertGreaterEqual(call_count["n"], 2, "expected at least 2 samples taken")

    def test_warmup_not_rearm_on_stable_uptime(self):
        """Stable uptime + no warmup → anomalies fire normally."""
        device = _make_device()
        call_count = {"n": 0}

        def _fake_sample(dev, fset):
            call_count["n"] += 1
            # Monotonically increasing uptime, heap always low
            return Sample(
                device=dev, timestamp=time.time(),
                info={"uptime_ms": call_count["n"] * 60_000, "reset_reason": "normal", "wdt_resets": 0},
                heap={"internal": {"free": 10_000, "min_free": 9_000}},
                telemetry=None, sensors=None, stats=None, ok=True,
            )

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            criteria = Criteria(heap_floor=50_000)
            # settle_delay=0 → no warmup → heap anomaly fires
            anomalies = poll(
                devices=[device],
                interval=0.05,
                duration=0.3,
                detectors=[make_heap_floor_detector(criteria)],
                settle_delay=0,
            )

        self.assertGreater(
            len(anomalies), 0,
            "expected heap anomaly when settle_delay=0 and heap below floor",
        )
        self.assertGreaterEqual(call_count["n"], 1)


class TestOnSampleCallback(unittest.TestCase):
    """on_sample callback wiring: fires per tick including warmup, backward-compat."""

    def test_callback_fires_per_tick_including_warmup(self):
        """settle_delay=3600: all ticks are in warmup; callback must still fire with warmup=True."""
        device = _make_device()
        received = []

        def _fake_sample(dev, fset):
            return _make_sample(dev, free=80_000)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            poll(
                devices=[device],
                interval=0.05,
                duration=0.2,
                detectors=[],
                settle_delay=3600,
                on_sample=received.append,
            )

        self.assertGreater(len(received), 0, "on_sample must fire at least once")
        self.assertTrue(all(s.warmup for s in received), "all ticks should be warmup=True")

    def test_callback_fires_no_warmup(self):
        """settle_delay=0: all samples must have warmup=False."""
        device = _make_device()
        received = []

        def _fake_sample(dev, fset):
            return _make_sample(dev)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            poll(
                devices=[device],
                interval=0.05,
                duration=0.2,
                detectors=[],
                settle_delay=0,
                on_sample=received.append,
            )

        self.assertGreater(len(received), 0)
        self.assertTrue(all(not s.warmup for s in received), "all ticks should be warmup=False")

    def test_no_callback_backward_compat(self):
        """Calling poll() without on_sample must not crash and must return anomalies list."""
        device = _make_device()

        def _fake_sample(dev, fset):
            return _make_sample(dev, free=10_000)

        from fleetlib.criteria import Criteria
        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            result = poll(
                devices=[device],
                interval=0.05,
                duration=0.2,
                detectors=[make_heap_floor_detector(Criteria(heap_floor=50_000))],
                settle_delay=0,
            )

        self.assertIsInstance(result, list)

    def test_callback_receives_all_samples(self):
        """on_sample receives samples for both ok and failed (unreachable) devices."""
        device = _make_device()
        received = []
        call_n = {"n": 0}

        def _fake_sample(dev, fset):
            call_n["n"] += 1
            # Alternate ok/not-ok
            if call_n["n"] % 2 == 1:
                return _make_sample(dev, ok=True)
            return _make_sample(dev, ok=False)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            poll(
                devices=[device],
                interval=0.05,
                duration=0.3,
                detectors=[],
                settle_delay=0,
                on_sample=received.append,
            )

        ok_samples = [s for s in received if s.ok]
        failed_samples = [s for s in received if not s.ok]
        self.assertGreater(len(ok_samples), 0, "should receive ok samples")
        self.assertGreater(len(failed_samples), 0, "should receive failed samples")


if __name__ == "__main__":
    unittest.main()
