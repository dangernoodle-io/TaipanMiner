"""Tests for TA-457: push readiness grace + PENDING-not-FAILED classification.

Covers:
  (a) healthy mining + validated:false => PENDING success (ok=True, pending=True), NOT FAILED
  (b) abnormal reset_reason (panic/brownout/wdt) => FAILED (ok=False)
      unreachable (never reboots) => FAILED
      mining never resumes within window => FAILED
  (c) wait_until_ready is invoked with a real timeout in the push path even when
      no --settle flag is given (settle=None)
  (d) --mark-valid drives mark_valid then reports VALIDATED (ok=True, pending=False)
      default (do_mark_valid=False) does NOT call mark_valid
  (e) reset_reason='software' (expected OTA reboot) is NOT a failure
  (f) cmd_ota_push prints PENDING/VALIDATED/FAILED verdicts and correct exit codes
"""
import os
import sys
import tempfile
import unittest
from unittest.mock import patch, call, MagicMock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib import ota
from fleetlib.ota import VerifyResult, _POST_OTA_READINESS_TIMEOUT
from fleetlib.readiness import Readiness
from fleetlib.safety import Guard


# ---------------------------------------------------------------------------
# Mock client helpers
# ---------------------------------------------------------------------------

class MockClient:
    """Mock of fleetlib.Client for push-path tests."""

    def __init__(self, ip="192.0.2.1", port=80, gets=None, reqs=None):
        self.ip = ip
        self.port = port
        self.gets = gets or {}
        self.reqs = reqs or {}
        self.request_log = []

    @staticmethod
    def _pop(v):
        if isinstance(v, list):
            if len(v) > 1:
                return v.pop(0)
            return v[0] if v else None
        return v

    def get_json(self, path, timeout=5):
        return self._pop(self.gets.get(path))

    def request(self, method, path, body=None, timeout=10):
        self.request_log.append((method.upper(), path))
        v = self.reqs.get((method.upper(), path), (200, b""))
        out = self._pop(v)
        return out if out is not None else (200, b"")


def _live_guard():
    return Guard(dry_run=False, confirm=True, expect_board="esp32-s2-mini")


def _patch_identity(ok=True):
    val = ("esp32-s2-mini", "test-host") if ok else (None, None)
    return patch("fleetlib.discovery._read_identity", return_value=val)


def _ready_readiness():
    """Readiness result simulating a board that came up ready."""
    return Readiness(ready=True, elapsed_s=15.0, reason="ready")


def _not_ready_readiness(reason="hashrate 0.0 <= min 0.0"):
    return Readiness(ready=False, elapsed_s=120.0, reason=reason)


def _gets_healthy_mining(version="v0.70.0", reset="software", validated=False):
    """GETs dict for a healthy mining board post-OTA reboot.

    validated=False simulates OTA pending-validation state.
    validated=True simulates already-validated state.
    """
    health = {"ok": False if not validated else True, "validated": validated}
    return {
        "/api/health": health,
        "/api/info": {"build": {"version": version}, "reset_reason": reset, "uptime_ms": 30000},
        "/api/stats": {"hashrate_ghs": 243.0, "expected_ghs": 243.0},
        "/api/diag/heap": {"internal": {"free": 80000}},
        "/api/pool": {"connected": True},
    }


# Stub out time.sleep globally for this module so readiness loops don't stall.
def setUpModule():
    global _sleep_patcher
    _sleep_patcher = patch("fleetlib.ota.time.sleep", lambda *a, **k: None)
    _sleep_patcher.start()


def tearDownModule():
    _sleep_patcher.stop()


# ---------------------------------------------------------------------------
# (a) PENDING success: healthy mining + validated:false
# ---------------------------------------------------------------------------

class TestPendingNotFailed(unittest.TestCase):
    """Healthy mining + validated:false must produce PENDING (ok=True), not FAILED."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.tmp.write(b"\x00firmware\xff")
        self.tmp.close()

    def tearDown(self):
        os.unlink(self.tmp.name)

    def _do_push(self, gets):
        c = MockClient(
            gets=gets,
            reqs={("POST", "/api/update/push"): (200, b"")},
        )
        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value="v0.70.0"):
                with patch("fleetlib.ota.wait_until_ready", return_value=_ready_readiness()):
                    r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0")
        return r

    def test_healthy_mining_validated_false_is_pending_success(self):
        """Mining up + validated:false => ok=True, pending=True, NOT FAILED."""
        gets = _gets_healthy_mining(validated=False)
        r = self._do_push(gets)
        self.assertTrue(r.ok, f"expected ok=True, got detail={r.detail!r}")
        self.assertTrue(r.pending, "expected pending=True")
        self.assertTrue(r.healthy)
        self.assertIn("pending validation", r.detail)

    def test_healthy_mining_validated_true_is_ok_not_pending(self):
        """Mining up + validated:true => ok=True, pending=False."""
        gets = _gets_healthy_mining(validated=True)
        r = self._do_push(gets)
        self.assertTrue(r.ok, f"expected ok=True, got detail={r.detail!r}")
        self.assertFalse(r.pending)
        self.assertTrue(r.healthy)

    def test_software_reset_reason_is_not_failure(self):
        """reset_reason='software' (OTA reboot) must not be treated as an error."""
        gets = _gets_healthy_mining(reset="software", validated=False)
        r = self._do_push(gets)
        self.assertTrue(r.ok, f"software reset should not fail: {r.detail!r}")
        # reset_reason is captured in metrics but not flagged
        self.assertEqual(r.metrics.get("reset_reason"), "software")


# ---------------------------------------------------------------------------
# (b) Real failures still produce ok=False
# ---------------------------------------------------------------------------

class TestRealFailures(unittest.TestCase):
    """Abnormal reset / mining-never-resumes / unreachable must still FAIL."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.tmp.write(b"\x00firmware\xff")
        self.tmp.close()

    def tearDown(self):
        os.unlink(self.tmp.name)

    def _push_with_gets(self, gets, readiness=None):
        c = MockClient(
            gets=gets,
            reqs={("POST", "/api/update/push"): (200, b"")},
        )
        if readiness is None:
            readiness = _ready_readiness()
        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value="v0.70.0"):
                with patch("fleetlib.ota.wait_until_ready", return_value=readiness):
                    r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0")
        return r

    def test_panic_reset_reason_fails(self):
        gets = _gets_healthy_mining(reset="panic")
        r = self._push_with_gets(gets)
        self.assertFalse(r.ok)
        self.assertIn("panic", r.detail)

    def test_brownout_reset_reason_fails(self):
        gets = _gets_healthy_mining(reset="brownout")
        r = self._push_with_gets(gets)
        self.assertFalse(r.ok)
        self.assertFalse(r.healthy)

    def test_task_wdt_reset_reason_fails(self):
        gets = _gets_healthy_mining(reset="task_wdt")
        r = self._push_with_gets(gets)
        self.assertFalse(r.ok)

    def test_mining_never_resumes_fails(self):
        """Readiness times out (not ready) with hashrate=0 => FAILED."""
        gets = _gets_healthy_mining(reset="software", validated=False)
        gets["/api/stats"] = {"hashrate_ghs": 0.0, "expected_ghs": 243.0}
        r = self._push_with_gets(gets, readiness=_not_ready_readiness())
        self.assertFalse(r.ok)

    def test_unreachable_after_reboot_fails(self):
        """wait_for_boot returning None (device never came back) => FAILED."""
        c = MockClient(reqs={("POST", "/api/update/push"): (200, b"")})
        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value=None):
                r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0")
        self.assertFalse(r.ok)
        self.assertIn("come back up", r.detail)

    def test_version_mismatch_fails(self):
        """Device boots wrong version => FAILED."""
        gets = _gets_healthy_mining(version="v0.69.0", validated=True)
        r = self._push_with_gets(gets)
        self.assertFalse(r.ok)
        self.assertIn("version", r.detail)


# ---------------------------------------------------------------------------
# (c) Readiness grace always applied (even with settle=None)
# ---------------------------------------------------------------------------

class TestReadinessGraceAlwaysApplied(unittest.TestCase):
    """wait_until_ready must be called with a real timeout regardless of --settle."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.tmp.write(b"\x00firmware\xff")
        self.tmp.close()

    def tearDown(self):
        os.unlink(self.tmp.name)

    def test_push_calls_wait_until_ready_with_real_timeout_when_settle_none(self):
        """push(settle=None) must call wait_until_ready with _POST_OTA_READINESS_TIMEOUT."""
        gets = _gets_healthy_mining(validated=False)
        c = MockClient(
            gets=gets,
            reqs={("POST", "/api/update/push"): (200, b"")},
        )
        captured = {}

        def fake_wait_until_ready(client, profile, criteria, timeout=300):
            captured["timeout"] = timeout
            return _ready_readiness()

        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value="v0.70.0"):
                with patch("fleetlib.ota.wait_until_ready", side_effect=fake_wait_until_ready):
                    ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0",
                             settle=None)

        self.assertIn("timeout", captured,
                      "wait_until_ready was not called from the push path")
        self.assertGreater(
            captured["timeout"], 0,
            f"wait_until_ready timeout must be > 0; got {captured['timeout']}"
        )
        self.assertEqual(
            captured["timeout"], _POST_OTA_READINESS_TIMEOUT,
            f"expected _POST_OTA_READINESS_TIMEOUT={_POST_OTA_READINESS_TIMEOUT}s, "
            f"got {captured['timeout']}"
        )

    def test_push_calls_wait_until_ready_even_without_settle_flag(self):
        """Confirm wait_until_ready is invoked (not skipped) even with no --settle."""
        gets = _gets_healthy_mining(validated=True)
        c = MockClient(
            gets=gets,
            reqs={("POST", "/api/update/push"): (200, b"")},
        )
        wait_called = []

        def fake_wait_until_ready(client, profile, criteria, timeout=300):
            wait_called.append(timeout)
            return _ready_readiness()

        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value="v0.70.0"):
                with patch("fleetlib.ota.wait_until_ready", side_effect=fake_wait_until_ready):
                    r = ota.push(c, _live_guard(), self.tmp.name, settle=None)

        self.assertTrue(wait_called, "wait_until_ready was never called")
        self.assertTrue(r.ok, r.detail)


# ---------------------------------------------------------------------------
# (d) --mark-valid drives mark_valid; default does NOT
# ---------------------------------------------------------------------------

class TestMarkValidFlag(unittest.TestCase):
    """do_mark_valid=True drives POST mark-valid; default=False skips it."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.tmp.write(b"\x00firmware\xff")
        self.tmp.close()

    def tearDown(self):
        os.unlink(self.tmp.name)

    def _push_with_mark_valid(self, do_mark_valid, gets):
        c = MockClient(
            gets=gets,
            reqs={
                ("POST", "/api/update/push"): (200, b""),
                ("POST", "/api/update/mark-valid"): (200, b""),
            },
        )
        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value="v0.70.0"):
                with patch("fleetlib.ota.wait_until_ready", return_value=_ready_readiness()):
                    r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0",
                                 do_mark_valid=do_mark_valid)
        return r, c.request_log

    def test_mark_valid_false_does_not_call_mark_valid(self):
        """Default (do_mark_valid=False): no POST /api/update/mark-valid."""
        gets = _gets_healthy_mining(validated=False)
        r, log = self._push_with_mark_valid(False, gets)
        self.assertTrue(r.ok, r.detail)
        self.assertTrue(r.pending)
        self.assertNotIn(("POST", "/api/update/mark-valid"), log)

    def test_mark_valid_true_calls_mark_valid_and_reports_validated(self):
        """do_mark_valid=True: POST /api/update/mark-valid sent; result is VALIDATED."""
        # After mark-valid, validated:true
        gets = _gets_healthy_mining(validated=False)
        # Simulate /api/health returning validated:true after mark-valid is called
        # (first call returns false, second returns true)
        gets["/api/health"] = [
            {"ok": False, "validated": False},
            {"ok": True, "validated": True},
        ]
        c = MockClient(
            gets=gets,
            reqs={
                ("POST", "/api/update/push"): (200, b""),
                ("POST", "/api/update/mark-valid"): (200, b""),
            },
        )
        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value="v0.70.0"):
                with patch("fleetlib.ota.wait_until_ready", return_value=_ready_readiness()):
                    r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0",
                                 do_mark_valid=True)

        self.assertTrue(r.ok, r.detail)
        self.assertFalse(r.pending, "expected pending=False after mark-valid")
        self.assertIn(("POST", "/api/update/mark-valid"), c.request_log)
        self.assertIn("VALIDATED", r.detail)

    def test_mark_valid_true_fails_if_validated_not_confirmed(self):
        """do_mark_valid=True but validated:true never comes => ok=False."""
        gets = _gets_healthy_mining(validated=False)
        # /api/health always returns validated:false
        c = MockClient(
            gets=gets,
            reqs={
                ("POST", "/api/update/push"): (200, b""),
                ("POST", "/api/update/mark-valid"): (200, b""),
            },
        )
        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value="v0.70.0"):
                with patch("fleetlib.ota.wait_until_ready", return_value=_ready_readiness()):
                    r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0",
                                 do_mark_valid=True)

        self.assertFalse(r.ok)
        self.assertIn("validated:true not confirmed", r.detail)


# ---------------------------------------------------------------------------
# (f) cmd_ota_push verdict strings + exit codes
# ---------------------------------------------------------------------------

class TestCmdOtaPushVerdicts(unittest.TestCase):
    """cmd_ota_push must print PENDING/VALIDATED/FAILED and return correct exit codes."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.tmp.write(b"\x00firmware\xff")
        self.tmp.close()

    def tearDown(self):
        os.unlink(self.tmp.name)

    def _run_cmd(self, result, mark_valid=False):
        import argparse
        import io
        from contextlib import redirect_stdout
        import fleet
        from fleetlib.discovery import Device
        from fleetlib.client import Client

        device = Device(hostname="miner-107", ip="192.0.2.107", port=80,
                        board="esp32-s2-mini", version="v0.70.0")
        args = argparse.Namespace(
            hosts=["192.0.2.107"],
            discover=False,
            board=None,
            dry_run=False,
            yes=True,
            target_version="v0.70.0",
            settle=None,
            binfile=self.tmp.name,
            mode="auto",
            criteria=None,
            mark_valid=mark_valid,
        )

        mock_client = MagicMock()
        mock_client.ip = device.ip
        mock_client.port = device.port
        mock_client.board = device.board

        buf = io.StringIO()
        with patch("fleet.resolve_devices", return_value=[device]):
            with patch("fleetlib.discovery._read_identity", return_value=("test-board", "test-host")):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.ota.push", return_value=result):
                        with redirect_stdout(buf):
                            rc = fleet.cmd_ota_push(args)

        return rc, buf.getvalue()

    def test_pending_result_prints_PENDING_and_exits_zero(self):
        r = VerifyResult(ok=True, pending=True, healthy=True, ready=True,
                         detail="PUSH OK — pending validation (firmware self-validates on first share / timer)",
                         version="v0.70.0", target_version="v0.70.0")
        rc, out = self._run_cmd(r)
        self.assertEqual(rc, 0, f"PENDING push should exit 0, got {rc}\n{out}")
        self.assertIn("PENDING", out)

    def test_failed_result_prints_FAILED_and_exits_one(self):
        r = VerifyResult(ok=False, pending=False, healthy=False, ready=False,
                         detail="abnormal reset_reason='panic' after OTA reboot",
                         version="v0.70.0", target_version="v0.70.0")
        rc, out = self._run_cmd(r)
        self.assertEqual(rc, 1)
        self.assertIn("FAILED", out)

    def test_validated_result_prints_OK_and_exits_zero(self):
        r = VerifyResult(ok=True, pending=False, healthy=True, ready=True,
                         detail="VALIDATED (mark-valid confirmed)",
                         version="v0.70.0", target_version="v0.70.0")
        rc, out = self._run_cmd(r, mark_valid=True)
        self.assertEqual(rc, 0)
        # OK line or VALIDATED detail should be present
        self.assertTrue("OK" in out or "VALIDATED" in out,
                        f"expected OK or VALIDATED in output:\n{out}")

    def test_dry_run_mark_valid_shows_in_plan(self):
        """dry-run with --mark-valid must mention mark-valid in the plan output."""
        import argparse
        import io
        from contextlib import redirect_stdout
        import fleet
        from fleetlib.discovery import Device
        from fleetlib.client import Client

        device = Device(hostname="miner-107", ip="192.0.2.107", port=80,
                        board="esp32-s2-mini", version="v0.70.0")
        args = argparse.Namespace(
            hosts=["192.0.2.107"],
            discover=False,
            board=None,
            dry_run=True,
            yes=False,
            target_version="v0.70.0",
            settle=None,
            binfile=self.tmp.name,
            mode="auto",
            criteria=None,
            mark_valid=True,
        )

        mock_client = MagicMock()
        mock_client.ip = device.ip
        mock_client.port = device.port
        mock_client.board = device.board

        buf = io.StringIO()
        with patch("fleet.resolve_devices", return_value=[device]):
            with patch("fleetlib.discovery._read_identity", return_value=("test-board", "test-host")):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with redirect_stdout(buf):
                        rc = fleet.cmd_ota_push(args)

        out = buf.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("DRY-RUN", out)
        self.assertIn("mark-valid", out)


if __name__ == "__main__":
    unittest.main()
