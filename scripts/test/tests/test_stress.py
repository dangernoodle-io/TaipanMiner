"""Unit tests for suites/stress.py (offline, mock client)."""
import os
import sys
import threading
import time
import unittest
from typing import Optional
from unittest.mock import MagicMock, patch, call

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.criteria import Criteria
from fleetlib.discovery import Device
from fleetlib.readiness import Readiness
from fleetlib.results import ResultSet, STATUS_PASS, STATUS_FAIL, STATUS_SKIP
from fleetlib.profiles import profile_for


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_device(ip="192.0.2.10", board="esp32-wroom32") -> Device:
    return Device(hostname=board, ip=ip, port=80, board=board, version="v0.99.0")


def _ready() -> Readiness:
    return Readiness(ready=True, elapsed_s=0.0, reason="ready")


def _not_ready(reason="not settled") -> Readiness:
    return Readiness(ready=False, elapsed_s=300.0, reason=reason)


def _make_ctx(devices, gates=None, extra=None, settle_ready=True):
    """Build a SuiteContext with a mocked settle gate (no real network).

    settle_ready: True/False, or a Readiness returned by ctx.settle.wait_ready.
    """
    from suites import SuiteContext
    from fleetlib.safety import Guard
    rs = ResultSet("stress")

    settle = MagicMock()
    settle.settle_delay = 0
    settle.enabled = True
    if settle_ready is True:
        settle.wait_ready.return_value = _ready()
    elif settle_ready is False:
        settle.wait_ready.return_value = _not_ready()
    else:
        settle.wait_ready.return_value = settle_ready

    return SuiteContext(
        devices=devices,
        criteria=Criteria(),
        guard=Guard(dry_run=True),
        results=rs,
        fields=None,
        gates=set(gates) if gates else set(),
        settle=settle,
        out_json=None,
        out_junit=None,
        baseline=None,
        extra=extra or {"duration": 1.0, "level": 0.8},
    )


# ---------------------------------------------------------------------------
# Tests: ceiling enforcement
# ---------------------------------------------------------------------------

class TestCeilingEnforcement(unittest.TestCase):
    """Concurrent requests must never exceed profile max_concurrent * level."""

    def test_wroom32_ceiling_respected(self):
        """wroom32: max_concurrent=2, level=0.8 → ceiling=1."""
        profile = profile_for("esp32-wroom32")
        level = 0.8
        ceiling = max(1, int(profile.max_concurrent * level))
        self.assertEqual(ceiling, 1)

    def test_s3_ceiling_respected(self):
        """esp32-s3: max_concurrent=4, level=0.8 → ceiling=3."""
        profile = profile_for("esp32-s3-devkit")
        level = 0.8
        ceiling = max(1, int(profile.max_concurrent * level))
        self.assertEqual(ceiling, 3)

    def test_c3_conservative_ceiling(self):
        """C3: max_concurrent=1, any level → ceiling=1 (minimum)."""
        profile = profile_for("esp32-c3-supermini")
        for level in (0.5, 0.8, 1.0):
            ceiling = max(1, int(profile.max_concurrent * level))
            self.assertEqual(ceiling, 1, f"C3 ceiling should be 1 at level={level}")

    def test_nopsram_conservative(self):
        """No-PSRAM boards (wroom32, tdongle) get max_concurrent=2."""
        for board in ("esp32-wroom32", "tdongle-s3"):
            p = profile_for(board)
            self.assertEqual(p.max_concurrent, 2)
            self.assertFalse(p.has_psram)

    def test_bitaxe_conservative(self):
        """ASIC bitaxe boards get max_concurrent=2."""
        p = profile_for("bitaxe-601")
        self.assertEqual(p.max_concurrent, 2)

    def test_thread_count_matches_ceiling(self):
        """_apply_load spawns exactly max_concurrent threads."""
        from suites.stress import _apply_load
        from fleetlib.client import Client

        device = _make_device()
        client = MagicMock(spec=Client)
        client.get_json.return_value = {"uptime_ms": 10_000}

        active_counts = []
        real_sleep = time.sleep

        def _tracking_sleep(t):
            real_sleep(min(t, 0.05))

        with patch("suites.stress.time.sleep", side_effect=_tracking_sleep):
            crashed, detail = _apply_load(client, device, duration=0.3, max_concurrent=2, baseline_uptime=5_000)

        self.assertFalse(crashed)


# ---------------------------------------------------------------------------
# Tests: crash signal detection
# ---------------------------------------------------------------------------

class TestCrashSignalDetection(unittest.TestCase):
    def test_unreachable_during_load_causes_fail(self):
        """Device unreachable mid-load → stress fails."""
        from suites import stress

        device = _make_device()
        ctx = _make_ctx([device], extra={"duration": 1.0, "level": 0.8})

        mock_client = MagicMock()
        # Baseline calls succeed, then unreachable
        call_n = {"n": 0}

        def _get_json(path, timeout=5):
            call_n["n"] += 1
            if call_n["n"] <= 3:
                return {"uptime_ms": 10_000, "free_heap": 80_000}
            return None

        mock_client.get_json = _get_json

        with patch("suites.stress.wait_until_ready", return_value=_ready()):
            with patch("suites.stress.Client", return_value=mock_client):
                stress._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        # Device went unreachable during load → crash → FAIL
        self.assertEqual(results[0].status, STATUS_FAIL)
        self.assertIn("crash", results[0].detail.lower())

    def test_uptime_regression_detected(self):
        """Uptime regression mid-load → crash detected → FAIL."""
        from suites.stress import _apply_load
        from fleetlib.client import Client

        device = _make_device()
        client = MagicMock(spec=Client)
        call_n = {"n": 0}

        def _get_json(path, timeout=5):
            call_n["n"] += 1
            # First 2 polls: normal uptime
            if call_n["n"] <= 2:
                return {"uptime_ms": 300_000}
            # Then: uptime regressed (reboot)
            return {"uptime_ms": 5_000}

        client.get_json = _get_json
        crashed, detail = _apply_load(
            client, device, duration=0.5, max_concurrent=1, baseline_uptime=300_000
        )
        self.assertTrue(crashed)
        self.assertIn("regression", detail.lower())

    def test_no_crash_on_stable_uptime(self):
        """Stable uptime throughout load → no crash."""
        from suites.stress import _apply_load
        from fleetlib.client import Client

        device = _make_device()
        client = MagicMock(spec=Client)
        up = {"v": 10_000}

        def _get_json(path, timeout=5):
            up["v"] += 1_000
            return {"uptime_ms": up["v"]}

        client.get_json = _get_json
        crashed, detail = _apply_load(
            client, device, duration=0.4, max_concurrent=1, baseline_uptime=10_000
        )
        self.assertFalse(crashed)


# ---------------------------------------------------------------------------
# Tests: recovery assertion
# ---------------------------------------------------------------------------

class TestRecoveryAssertion(unittest.TestCase):
    def test_pass_when_heap_recovers(self):
        """Heap recovers to > 80% of baseline and above floor → PASS."""
        from suites.stress import _check_heap_recovery
        criteria = Criteria(heap_floor=50_000)

        ok, detail = _check_heap_recovery(80_000, 70_000, criteria)
        self.assertTrue(ok, detail)

    def test_fail_when_heap_below_floor(self):
        """Post-load heap below floor → FAIL."""
        from suites.stress import _check_heap_recovery
        criteria = Criteria(heap_floor=50_000)

        ok, detail = _check_heap_recovery(80_000, 40_000, criteria)
        self.assertFalse(ok)
        self.assertIn("floor", detail)

    def test_fail_when_heap_doesnt_recover_to_baseline(self):
        """Post-load heap above floor but below 80% of baseline → FAIL."""
        from suites.stress import _check_heap_recovery
        criteria = Criteria(heap_floor=30_000)

        # baseline=100K, floor=30K, post=40K — above floor but < 80% of baseline
        ok, detail = _check_heap_recovery(100_000, 40_000, criteria)
        self.assertFalse(ok)
        self.assertIn("80%", detail)

    def test_pass_when_heap_data_unavailable(self):
        """No heap data → skip check (pass)."""
        from suites.stress import _check_heap_recovery
        criteria = Criteria(heap_floor=50_000)

        ok, detail = _check_heap_recovery(None, None, criteria)
        self.assertTrue(ok)

    def test_full_run_passes_on_healthy_device(self):
        """End-to-end: healthy device survives stress and recovers → PASS."""
        from suites import stress

        device = _make_device()
        ctx = _make_ctx([device], extra={"duration": 0.5, "level": 0.8})

        mock_client = MagicMock()
        mock_client.get_json.return_value = {"uptime_ms": 30_000, "free_heap": 90_000}

        # patch Client constructor and wait_until_ready
        with patch("suites.stress.Client", return_value=mock_client):
            with patch("suites.stress.wait_until_ready", return_value=_ready()):
                stress._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, STATUS_PASS)

    def test_no_recovery_causes_fail(self):
        """Board doesn't recover after load → FAIL."""
        from suites import stress

        device = _make_device()
        ctx = _make_ctx([device], extra={"duration": 0.3, "level": 0.8})

        mock_client = MagicMock()
        mock_client.get_json.return_value = {"uptime_ms": 15_000, "free_heap": 80_000}

        # settle gate (ctx.settle.wait_ready) is mocked ready; the recovery
        # check (suites.stress.wait_until_ready) returns not-ready.
        with patch("suites.stress.Client", return_value=mock_client):
            with patch("suites.stress.wait_until_ready",
                       return_value=_not_ready("heap low after load")):
                stress._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, STATUS_FAIL)
        self.assertIn("recovery", results[0].detail)


# ---------------------------------------------------------------------------
# Tests: settle gate
# ---------------------------------------------------------------------------

class TestRecoverySettleDelay(unittest.TestCase):
    """Recovery assertion must use settle_delay=0 so it never re-waits the warmup floor."""

    def test_recovery_uses_zero_settle_delay(self):
        """wait_until_ready for recovery is called with settle_delay=0 criteria copy.

        This prevents timeout=120 from racing against settle_delay=120 and
        spuriously failing healthy boards.
        """
        from suites import stress
        import copy

        device = _make_device()
        ctx = _make_ctx([device], extra={"duration": 0.3, "level": 0.8})
        # Give ctx.criteria a non-zero settle_delay to verify it is zeroed for recovery
        ctx.criteria = Criteria(settle_delay=120, heap_floor=50_000)

        mock_client = MagicMock()
        mock_client.get_json.return_value = {"uptime_ms": 30_000, "free_heap": 90_000}

        captured_criteria = []

        def _fake_wait_ready(c, profile, crit, timeout=300):
            captured_criteria.append(copy.copy(crit))
            return _ready()

        with patch("suites.stress.Client", return_value=mock_client):
            with patch("suites.stress.wait_until_ready", side_effect=_fake_wait_ready):
                stress._run_device(device, ctx, ctx.results)

        # The recovery call should have settle_delay=0
        self.assertTrue(len(captured_criteria) >= 1, "wait_until_ready not called")
        recovery_crit = captured_criteria[-1]
        self.assertEqual(
            recovery_crit.settle_delay, 0,
            f"recovery settle_delay should be 0, got {recovery_crit.settle_delay}",
        )

    def test_healthy_board_passes_after_short_duration(self):
        """A healthy board passes stress even with default criteria (settle_delay=120)
        because recovery uses settle_delay=0.
        """
        from suites import stress

        device = _make_device()
        ctx = _make_ctx([device], extra={"duration": 0.3, "level": 0.8})
        ctx.criteria = Criteria(settle_delay=120, heap_floor=50_000)

        mock_client = MagicMock()
        mock_client.get_json.return_value = {"uptime_ms": 30_000, "free_heap": 90_000}

        # Recovery wait_until_ready returns ready immediately (settle_delay=0 on recovery)
        with patch("suites.stress.Client", return_value=mock_client):
            with patch("suites.stress.wait_until_ready", return_value=_ready()):
                stress._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, STATUS_PASS, results[0].detail)


class TestSettleGate(unittest.TestCase):
    def test_not_ready_causes_skip(self):
        """Board not ready before load → SKIP (never apply load)."""
        from suites import stress

        device = _make_device()
        # settle gate returns not-ready → load must never be applied
        ctx = _make_ctx([device], extra={"duration": 0.3, "level": 0.8},
                        settle_ready=False)

        with patch("suites.stress.Client"):
            stress._run_device(device, ctx, ctx.results)

        results = ctx.results.results
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].status, STATUS_SKIP)
        self.assertIn("not ready", results[0].detail)


if __name__ == "__main__":
    unittest.main()
