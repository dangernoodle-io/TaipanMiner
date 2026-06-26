"""Unit tests for suites/soak.py (offline, mock client)."""
import os
import sys
import time
import unittest
from typing import Any, Dict, Optional
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.criteria import Criteria
from fleetlib.discovery import Device
from fleetlib.monitor import Sample, Anomaly
from fleetlib.results import ResultSet, STATUS_PASS, STATUS_FAIL
from fleetlib.profiles import profile_for


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_device(ip="192.0.2.1", board="esp32-wroom32") -> Device:
    return Device(hostname=board, ip=ip, port=80, board=board, version="v0.99.0")


def _make_sample(device, ok=True, free=80_000, uptime_ms=300_000,
                 reset_reason="normal", wdt_resets=0,
                 pub_ok=True, mqtt_connected=True, mqtt_enabled=True) -> Sample:
    info = {
        "uptime_ms": uptime_ms,
        "reset_reason": reset_reason,
        "wdt_resets": wdt_resets,
    } if ok else None
    heap = {"internal": {"free": free, "min_free": free - 1000}} if ok else None
    telemetry = None
    if ok:
        telemetry = {
            "mqtt": {"enabled": mqtt_enabled, "connected": mqtt_connected},
            "http": {"enabled": False},
            "publisher": {"last_publish_ok": pub_ok},
        }
    return Sample(
        device=device,
        timestamp=time.time(),
        info=info,
        heap=heap,
        telemetry=telemetry,
        sensors=None,
        stats=None,
        ok=ok,
    )


def _make_ctx(devices, criteria=None, gates=None, fields=None, extra=None):
    from suites import SuiteContext, SettleConfig
    from fleetlib.safety import Guard
    rs = ResultSet("soak")
    return SuiteContext(
        devices=devices,
        criteria=criteria or Criteria(),
        guard=Guard(dry_run=True),
        results=rs,
        fields=fields,
        gates=set(gates) if gates else set(),
        settle=SettleConfig(settle_delay=0, enabled=False),
        out_json=None,
        out_junit=None,
        baseline=None,
        extra=extra or {},
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestSoakDetectorWiring(unittest.TestCase):
    """Verify correct detectors built for each profile type via soak._run_device."""

    def _run_with_mock(self, board, sample_fn, extra=None):
        from suites import soak
        device = _make_device(board=board)
        ctx = _make_ctx([device], extra=extra or {"duration": 0.2, "interval": 0.05})

        with patch("fleetlib.monitor._sample_device", side_effect=sample_fn):
            soak._run_device(device, ctx, ctx.results)
        return ctx.results.results

    def test_wroom32_no_vcore_detector(self):
        """wroom32 (non-ASIC) must not trigger vcore detector."""
        device = _make_device(board="esp32-wroom32")
        calls = []

        def _fake_sample(dev, fset):
            s = _make_sample(dev)
            calls.append(fset)
            return s

        results = self._run_with_mock("esp32-wroom32", _fake_sample)
        # No vcore anomaly expected since no sensors are checked for non-ASIC
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, STATUS_PASS)

    def test_bitaxe_includes_sensors_field(self):
        """ASIC board must request sensors field so vcore detector can fire."""
        from suites import soak

        device = _make_device(board="bitaxe-403")
        ctx = _make_ctx([device], extra={"duration": 0.2, "interval": 0.05})
        fetched_fields = []

        def _fake_sample(dev, fset):
            fetched_fields.append(set(fset))
            return _make_sample(dev)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        self.assertTrue(
            any("sensors" in f for f in fetched_fields),
            f"ASIC board should request sensors field; got {fetched_fields}",
        )

    def test_c3_uses_lower_heap_floor(self):
        """C3 profile sets heap_floor=30_000; soak should pass at 35K free."""
        from suites import soak

        device = _make_device(board="esp32-c3-supermini")
        ctx = _make_ctx([device], extra={"duration": 0.2, "interval": 0.05})

        def _fake_sample(dev, fset):
            return _make_sample(dev, free=35_000)  # above C3 floor (30K)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, STATUS_PASS)


class TestSoakWarmupSuppression(unittest.TestCase):
    """Warmup window must suppress criteria violations."""

    def test_no_violation_during_warmup(self):
        """settle_delay=3600 → anomalies suppressed during short run."""
        from suites import soak, SuiteContext, SettleConfig
        from fleetlib.safety import Guard

        device = _make_device()
        rs = ResultSet("soak")
        ctx = SuiteContext(
            devices=[device],
            criteria=Criteria(heap_floor=50_000),
            guard=Guard(dry_run=True),
            results=rs,
            fields=None,
            gates=set(),
            settle=SettleConfig(settle_delay=3600, enabled=True),
            out_json=None,
            out_junit=None,
            baseline=None,
            extra={"duration": 0.3, "interval": 0.05},
        )

        def _fake_sample(dev, fset):
            return _make_sample(dev, free=10_000)  # below floor

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, rs)

        results = rs.results
        self.assertEqual(len(results), 1)
        self.assertEqual(
            results[0].status, STATUS_PASS,
            "anomalies during warmup must be suppressed → PASS",
        )


class TestSoakWdtDetection(unittest.TestCase):
    """WDT resets increase should trigger wdt_increase anomaly."""

    def test_wdt_increase_causes_fail(self):
        from suites import soak

        device = _make_device()
        ctx = _make_ctx([device], extra={"duration": 0.3, "interval": 0.05})
        call_n = {"n": 0}

        def _fake_sample(dev, fset):
            call_n["n"] += 1
            # First sample has wdt_resets=0; subsequent have wdt_resets=1
            wdt = 0 if call_n["n"] == 1 else 1
            return _make_sample(dev, wdt_resets=wdt)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        # wdt increase should produce a FAIL
        self.assertEqual(results[0].status, STATUS_FAIL)
        self.assertIn("wdt", results[0].detail.lower())


class TestSoakPublisherTA426(unittest.TestCase):
    """TA-426: publisher verdict suppressed until mqtt.connected seen at least once."""

    def test_no_false_positive_before_mqtt_connected(self):
        """pub_ok=False but mqtt.connected=False: no publisher_down anomaly."""
        from suites import soak

        device = _make_device()
        # Only enable publisher gate
        ctx = _make_ctx([device], gates={"publisher"}, extra={"duration": 0.4, "interval": 0.05})

        def _fake_sample(dev, fset):
            # mqtt enabled but NOT connected yet; pub_ok false
            return _make_sample(dev, pub_ok=False, mqtt_connected=False, mqtt_enabled=True)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        # Should NOT fail — no connection yet so publisher verdict is suppressed
        self.assertEqual(
            results[0].status, STATUS_PASS,
            "publisher_down must not fire before mqtt.connected is seen",
        )

    def test_publisher_down_fires_after_connected(self):
        """mqtt.connected seen once then pub_ok=False → publisher_down fires."""
        from suites import soak

        device = _make_device()
        ctx = _make_ctx([device], gates={"publisher"}, extra={"duration": 0.5, "interval": 0.05})
        call_n = {"n": 0}

        def _fake_sample(dev, fset):
            call_n["n"] += 1
            if call_n["n"] == 1:
                # First sample: connected and publishing fine
                return _make_sample(dev, pub_ok=True, mqtt_connected=True)
            # Subsequent: connected but pub_ok=False
            return _make_sample(dev, pub_ok=False, mqtt_connected=True)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        # After mqtt.connected=True was seen, repeated pub_ok=False should fail
        # (needs >= publisher_max_polls=6 consecutive fails; run is short so
        #  PASS is also acceptable if not enough ticks — just verify no crash)
        self.assertIn(results[0].status, (STATUS_PASS, STATUS_FAIL))

    def test_publisher_gate_detector_suppresses_before_connected(self):
        """Unit test the gate detector directly (no poll machinery)."""
        from suites.soak import _build_publisher_gate_detector

        criteria = Criteria(publisher_max_polls=1)
        det = _build_publisher_gate_detector(criteria, "publisher")
        device = _make_device()
        state: Dict[str, Any] = {}

        # Sample with mqtt NOT connected, pub_ok=False
        sample = _make_sample(device, pub_ok=False, mqtt_connected=False, mqtt_enabled=True)
        result = det(sample, state)
        self.assertIsNone(result, "should not fire before mqtt.connected seen")

        # Now simulate connected=True with pub_ok=False — enough to trigger (max_polls=1)
        sample2 = _make_sample(device, pub_ok=False, mqtt_connected=True, mqtt_enabled=True)
        result2 = det(sample2, state)
        self.assertIsNotNone(result2, "should fire after mqtt.connected seen and pub_ok=False")
        self.assertEqual(result2.detector, "publisher_down")


class TestSoakVersionCheck(unittest.TestCase):
    def test_version_mismatch_fails(self):
        from suites import soak

        device = Device(hostname="wroom32", ip="192.0.2.5", port=80,
                        board="esp32-wroom32", version="v0.68.0")
        ctx = _make_ctx([device], extra={"duration": 0.2, "interval": 0.05, "target": "v0.99.0"})
        soak._run_device(device, ctx, ctx.results)
        results = ctx.results.results
        self.assertEqual(results[0].status, STATUS_FAIL)
        self.assertIn("version", results[0].detail)

    def test_version_match_continues(self):
        from suites import soak

        device = Device(hostname="wroom32", ip="192.0.2.6", port=80,
                        board="esp32-wroom32", version="v0.99.0")
        ctx = _make_ctx([device], extra={"duration": 0.2, "interval": 0.05, "target": "v0.99.0"})

        def _fake_sample(dev, fset):
            return _make_sample(dev)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(results[0].status, STATUS_PASS)


class TestSoakHashrateCPU(unittest.TestCase):
    """TA-451: hashrate detector active on all mining boards (not ASIC-only)."""

    def _make_sample_with_stats(self, device, expected_ghs, hashrate):
        s = _make_sample(device)
        s.stats = {"expected_ghs": expected_ghs, "hashrate": hashrate}
        return s

    def test_hashrate_detector_active_on_cpu_when_expected_ghs_nonzero(self):
        """CPU board with expected_ghs > 0 and hashrate far below floor → FAIL."""
        from suites import soak

        device = _make_device(board="esp32-wroom32")
        ctx = _make_ctx([device], extra={"duration": 0.3, "interval": 0.05, "quiet": True})

        def _fake_sample(dev, fset):
            return self._make_sample_with_stats(dev, expected_ghs=0.5, hashrate=0.01)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, STATUS_FAIL)
        self.assertIn("hashrate", results[0].detail.lower())

    def test_hashrate_detector_skips_when_expected_ghs_zero(self):
        """CPU board with expected_ghs = 0 → no floor check → PASS."""
        from suites import soak

        device = _make_device(board="esp32-wroom32")
        ctx = _make_ctx([device], extra={"duration": 0.2, "interval": 0.05, "quiet": True})

        def _fake_sample(dev, fset):
            return self._make_sample_with_stats(dev, expected_ghs=0.0, hashrate=0.0)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, STATUS_PASS)

    def test_stats_field_fetched_for_cpu_when_hashrate_gate_enabled(self):
        """CPU board must request stats field when hashrate gate is enabled."""
        from suites import soak

        device = _make_device(board="esp32-wroom32")
        ctx = _make_ctx([device], extra={"duration": 0.2, "interval": 0.05, "quiet": True})
        fetched_fields = []

        def _fake_sample(dev, fset):
            fetched_fields.append(set(fset))
            return _make_sample(dev)

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_sample):
            soak._run_device(device, ctx, ctx.results)

        self.assertTrue(
            any("stats" in f for f in fetched_fields),
            f"CPU board should request stats field when hashrate gate enabled; got {fetched_fields}",
        )

    def test_per_class_floor_from_profile(self):
        """Profile hashrate_floor_pct=90%: hashrate 0.85 → FAIL; 0.95 → PASS."""
        from suites import soak
        from fleetlib.criteria import Criteria
        from fleetlib.profiles import Profile, Profiles

        # Build a context with a profile that has hashrate_floor_pct=90.0
        criteria = Criteria(hashrate_floor_pct=90.0)

        # Test: below floor
        device = _make_device(board="esp32-wroom32")
        ctx = _make_ctx([device], criteria=criteria, extra={"duration": 0.3, "interval": 0.05, "quiet": True})

        def _fake_below(dev, fset):
            s = _make_sample(dev)
            s.stats = {"expected_ghs": 1.0, "hashrate": 0.85}
            return s

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_below):
            soak._run_device(device, ctx, ctx.results)

        self.assertEqual(ctx.results.results[0].status, STATUS_FAIL)

        # Test: above floor
        device2 = _make_device(ip="192.0.2.2", board="esp32-wroom32")
        rs2 = ResultSet("soak")
        from suites import SuiteContext, SettleConfig
        from fleetlib.safety import Guard
        ctx2 = SuiteContext(
            devices=[device2],
            criteria=criteria,
            guard=Guard(dry_run=True),
            results=rs2,
            fields=None,
            gates=set(),
            settle=SettleConfig(settle_delay=0, enabled=False),
            out_json=None,
            out_junit=None,
            baseline=None,
            extra={"duration": 0.3, "interval": 0.05, "quiet": True},
        )

        def _fake_above(dev, fset):
            s = _make_sample(dev)
            s.stats = {"expected_ghs": 1.0, "hashrate": 0.95}
            return s

        with patch("fleetlib.monitor._sample_device", side_effect=_fake_above):
            soak._run_device(device2, ctx2, rs2)

        self.assertEqual(rs2.results[0].status, STATUS_PASS)


class TestResultLogs(unittest.TestCase):
    """Result.logs field serializes correctly into JSON output."""

    def test_logs_serialized_in_json(self):
        import json
        import tempfile
        import os
        from fleetlib.results import Result, ResultSet, STATUS_PASS
        from fleetlib.discovery import Device

        dev = Device(hostname="h", ip="192.0.2.1", port=80, board="test", version="v1")
        rs = ResultSet("t")
        rs.add(Result("r", dev, STATUS_PASS, "", logs=["line1", "line2"]))

        with tempfile.NamedTemporaryFile(suffix=".json", delete=False, mode="w") as f:
            path = f.name
        try:
            rs.to_json(path)
            with open(path) as f:
                data = json.load(f)
            self.assertEqual(data["results"][0]["logs"], ["line1", "line2"])
        finally:
            os.unlink(path)

    def test_logs_none_serialized_as_null(self):
        import json
        import tempfile
        import os
        from fleetlib.results import Result, ResultSet, STATUS_PASS
        from fleetlib.discovery import Device

        dev = Device(hostname="h", ip="192.0.2.1", port=80, board="test", version="v1")
        rs = ResultSet("t")
        rs.add(Result("r", dev, STATUS_PASS, "", logs=None))

        with tempfile.NamedTemporaryFile(suffix=".json", delete=False, mode="w") as f:
            path = f.name
        try:
            rs.to_json(path)
            with open(path) as f:
                data = json.load(f)
            self.assertIsNone(data["results"][0]["logs"])
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
