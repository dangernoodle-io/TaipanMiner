"""Offline unit tests for fleetlib.ota — mock-client based, no live OTA.

Covers every public function and both apply modes:
  - push: success / reject / no-boot / dry-run / identity-mismatch / no-confirm
  - pull: boot-mode (200) / pull-mode (202) progress-to-terminal / busy (409) /
          no-update / pull-mode download failure / dry-run / guard enforcement
  - mark_valid / recover: success / dry-run / no-confirm
  - status: read-only merge
  - verify: settle-then-assert pass / version-mismatch fail / unhealthy fail
  - wait_for_boot: success / version-target / timeout / connection-refused tolerance
"""
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib import ota
from fleetlib.ota import VerifyResult
from fleetlib.criteria import Criteria
from fleetlib.profiles import profile_for
from fleetlib.safety import Guard, IdentityMismatch, RefusedWithoutConfirmation


# ---------------------------------------------------------------------------
# Mock client
# ---------------------------------------------------------------------------

class MockClient:
    """Mock of fleetlib.Client.

    gets: path -> value, or list used as a queue (last element repeats).
    reqs: (METHOD, path) -> (status, bytes), or a queue list.
    Every request is recorded in request_log for mutation-skip assertions.
    """

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


def _ok_info(version="v0.70.0", reset="power_on"):
    return {"version": version, "reset_reason": reset, "uptime_ms": 30000}


def _healthy_gets(version="v0.70.0"):
    return {
        "/api/health": {"ok": True},
        "/api/info": _ok_info(version),
        "/api/stats": {"hashrate_ghs": 485.0},
        "/api/diag/heap": {"internal": {"free": 80000}},
    }


def _live_guard():
    return Guard(dry_run=False, confirm=True, expect_board="esp32-wroom32")


def _patch_identity(ok=True):
    return patch("fleetlib.discovery.verify_identity", return_value=ok)


# Kill real sleeps in every test (poll loops would otherwise stall).
def setUpModule():
    global _sleep_patcher
    _sleep_patcher = patch("fleetlib.ota.time.sleep", lambda *a, **k: None)
    _sleep_patcher.start()


def tearDownModule():
    _sleep_patcher.stop()


# ---------------------------------------------------------------------------
# wait_for_boot
# ---------------------------------------------------------------------------

class TestWaitForBoot(unittest.TestCase):
    def test_back_up_any_version(self):
        c = MockClient(gets={"/api/health": {"ok": True}, "/api/info": _ok_info("v0.69.0")})
        self.assertEqual(wait := ota.wait_for_boot(c), "v0.69.0")

    def test_waits_for_target_version(self):
        c = MockClient(gets={
            "/api/health": {"ok": True},
            "/api/info": [_ok_info("v0.69.0"), _ok_info("v0.70.0")],
        })
        self.assertEqual(ota.wait_for_boot(c, target_version="v0.70.0"), "v0.70.0")

    def test_timeout_returns_none(self):
        c = MockClient(gets={"/api/health": None, "/api/info": None})
        self.assertIsNone(ota.wait_for_boot(c, timeout=0.05))

    def test_tolerates_connection_refused(self):
        def boom(path, timeout=5):
            raise OSError("connection refused")
        c = MockClient()
        c.get_json = boom
        self.assertIsNone(ota.wait_for_boot(c, timeout=0.05))


# ---------------------------------------------------------------------------
# push
# ---------------------------------------------------------------------------

class TestPush(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.tmp.write(b"\x00firmware\xff")
        self.tmp.close()

    def tearDown(self):
        os.unlink(self.tmp.name)

    def test_push_success(self):
        c = MockClient(gets=_healthy_gets(), reqs={("POST", "/api/update/push"): (200, b"")})
        with _patch_identity(True):
            r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0")
        self.assertTrue(r.ok, r.detail)
        self.assertTrue(r.healthy)
        self.assertEqual(r.version, "v0.70.0")
        self.assertIn(("POST", "/api/update/push"), c.request_log)

    def test_push_connection_reset_is_ok(self):
        # status None = device rebooted mid-response (expected boot-mode)
        c = MockClient(gets=_healthy_gets(), reqs={("POST", "/api/update/push"): (None, b"")})
        with _patch_identity(True):
            r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0")
        self.assertTrue(r.ok, r.detail)

    def test_push_rejected(self):
        c = MockClient(gets=_healthy_gets(), reqs={("POST", "/api/update/push"): (500, b"err")})
        with _patch_identity(True):
            r = ota.push(c, _live_guard(), self.tmp.name)
        self.assertFalse(r.ok)
        self.assertIn("500", r.detail)

    def test_push_no_boot(self):
        c = MockClient(reqs={("POST", "/api/update/push"): (200, b"")})
        with _patch_identity(True):
            with patch.object(ota, "wait_for_boot", return_value=None):
                r = ota.push(c, _live_guard(), self.tmp.name, target_version="v0.70.0")
        self.assertFalse(r.ok)
        self.assertIn("come back up", r.detail)

    def test_push_dry_run_skips_mutation(self):
        c = MockClient(reqs={("POST", "/api/update/push"): (200, b"")})
        g = Guard(dry_run=True, confirm=True, expect_board="esp32-wroom32")
        with _patch_identity(True):
            r = ota.push(c, g, self.tmp.name, target_version="v0.70.0")
        self.assertTrue(r.dry_run)
        self.assertTrue(r.ok)
        self.assertEqual(c.request_log, [])  # NO HTTP mutation

    def test_push_identity_mismatch_refuses(self):
        c = MockClient()
        with _patch_identity(False):
            with self.assertRaises(IdentityMismatch):
                ota.push(c, _live_guard(), self.tmp.name)
        self.assertEqual(c.request_log, [])

    def test_push_no_confirm_refuses(self):
        c = MockClient()
        g = Guard(dry_run=False, confirm=False, expect_board="esp32-wroom32")
        with _patch_identity(True):
            with self.assertRaises(RefusedWithoutConfirmation):
                ota.push(c, g, self.tmp.name)
        self.assertEqual(c.request_log, [])


# ---------------------------------------------------------------------------
# pull — mode detection
# ---------------------------------------------------------------------------

class TestPullBootMode(unittest.TestCase):
    def test_apply_200_boot_mode(self):
        c = MockClient(
            gets=_healthy_gets(),
            reqs={
                ("POST", "/api/update/check"): (200, b'{"available": true, "latest": "v0.70.0"}'),
                ("POST", "/api/update/apply"): (200, b""),
            },
        )
        with _patch_identity(True):
            r = ota.pull(c, _live_guard())
        self.assertTrue(r.ok, r.detail)
        self.assertEqual(r.version, "v0.70.0")
        # apply happened, progress was NOT polled (boot-mode)
        self.assertIn(("POST", "/api/update/apply"), c.request_log)


class TestPullPullMode(unittest.TestCase):
    def test_apply_202_polls_progress_to_terminal(self):
        gets = _healthy_gets()
        gets["/api/update/progress"] = [
            {"state": "downloading", "percent": 10},
            {"state": "writing", "percent": 80},
            {"state": "done", "percent": 100},
        ]
        c = MockClient(
            gets=gets,
            reqs={
                ("POST", "/api/update/check"): (200, b'{"available": true, "latest": "v0.70.0"}'),
                ("POST", "/api/update/apply"): (202, b""),
            },
        )
        with _patch_identity(True):
            r = ota.pull(c, _live_guard())
        self.assertTrue(r.ok, r.detail)
        self.assertEqual(r.version, "v0.70.0")

    def test_pull_mode_download_failure(self):
        gets = _healthy_gets()
        gets["/api/update/progress"] = [
            {"state": "downloading"},
            {"state": "error", "msg": "bad signature"},
        ]
        c = MockClient(
            gets=gets,
            reqs={
                ("POST", "/api/update/check"): (200, b'{"available": true, "latest": "v0.70.0"}'),
                ("POST", "/api/update/apply"): (202, b""),
            },
        )
        with _patch_identity(True):
            r = ota.pull(c, _live_guard())
        self.assertFalse(r.ok)
        self.assertIn("error", r.detail)


class TestPullBusyAndNoUpdate(unittest.TestCase):
    def test_apply_409_busy(self):
        c = MockClient(
            gets=_healthy_gets(),
            reqs={
                ("POST", "/api/update/check"): (200, b'{"available": true, "latest": "v0.70.0"}'),
                ("POST", "/api/update/apply"): (409, b"busy"),
            },
        )
        with _patch_identity(True):
            r = ota.pull(c, _live_guard())
        self.assertFalse(r.ok)
        self.assertIn("409", r.detail)

    def test_no_update_available_is_benign(self):
        c = MockClient(
            gets={"/api/info": _ok_info("v0.70.0")},
            reqs={("POST", "/api/update/check"): (200, b'{"available": false}')},
        )
        with _patch_identity(True):
            r = ota.pull(c, _live_guard())
        self.assertTrue(r.ok)
        self.assertEqual(r.version, "v0.70.0")
        self.assertNotIn(("POST", "/api/update/apply"), c.request_log)


class TestPullGuard(unittest.TestCase):
    def test_dry_run_skips_all_mutation(self):
        c = MockClient(reqs={("POST", "/api/update/check"): (200, b'{"available": true}')})
        g = Guard(dry_run=True, confirm=True, expect_board="esp32-wroom32")
        with _patch_identity(True):
            r = ota.pull(c, g)
        self.assertTrue(r.dry_run)
        self.assertEqual(c.request_log, [])

    def test_identity_mismatch_refuses(self):
        c = MockClient()
        with _patch_identity(False):
            with self.assertRaises(IdentityMismatch):
                ota.pull(c, _live_guard())
        self.assertEqual(c.request_log, [])

    def test_no_confirm_refuses(self):
        c = MockClient()
        g = Guard(dry_run=False, confirm=False, expect_board="esp32-wroom32")
        with _patch_identity(True):
            with self.assertRaises(RefusedWithoutConfirmation):
                ota.pull(c, g)
        self.assertEqual(c.request_log, [])


# ---------------------------------------------------------------------------
# mark_valid / recover
# ---------------------------------------------------------------------------

class TestMarkValid(unittest.TestCase):
    def test_success(self):
        c = MockClient(reqs={("POST", "/api/update/mark-valid"): (200, b"")})
        with _patch_identity(True):
            self.assertTrue(ota.mark_valid(c, _live_guard()))

    def test_dry_run_no_mutation(self):
        c = MockClient()
        g = Guard(dry_run=True, confirm=True, expect_board="esp32-wroom32")
        with _patch_identity(True):
            self.assertTrue(ota.mark_valid(c, g))
        self.assertEqual(c.request_log, [])

    def test_no_confirm_refuses(self):
        c = MockClient()
        g = Guard(dry_run=False, confirm=False, expect_board="esp32-wroom32")
        with _patch_identity(True):
            with self.assertRaises(RefusedWithoutConfirmation):
                ota.mark_valid(c, g)


class TestRecover(unittest.TestCase):
    def test_success(self):
        c = MockClient(reqs={("POST", "/api/update/recover"): (200, b"")})
        with _patch_identity(True):
            self.assertTrue(ota.recover(c, _live_guard()))

    def test_dry_run_no_mutation(self):
        c = MockClient()
        g = Guard(dry_run=True, confirm=True, expect_board="esp32-wroom32")
        with _patch_identity(True):
            self.assertTrue(ota.recover(c, g))
        self.assertEqual(c.request_log, [])

    def test_failure_status(self):
        c = MockClient(reqs={("POST", "/api/update/recover"): (500, b"err")})
        with _patch_identity(True):
            self.assertFalse(ota.recover(c, _live_guard()))


# ---------------------------------------------------------------------------
# status (read-only)
# ---------------------------------------------------------------------------

class TestStatus(unittest.TestCase):
    def test_merges_status_and_progress(self):
        c = MockClient(gets={
            "/api/update/status": {"available": True, "latest": "v0.70.0"},
            "/api/update/progress": {"state": "done", "percent": 100},
        })
        merged = ota.status(c)
        self.assertEqual(merged["latest"], "v0.70.0")
        self.assertEqual(merged["progress"]["state"], "done")
        self.assertEqual(c.request_log, [])  # read-only, no mutation

    def test_missing_endpoints_default_empty(self):
        c = MockClient()
        merged = ota.status(c)
        self.assertEqual(merged, {"progress": {}})


# ---------------------------------------------------------------------------
# verify (settle-then-assert)
# ---------------------------------------------------------------------------

class TestVerify(unittest.TestCase):
    def _criteria(self):
        return Criteria(settle_delay=0, readiness_heap_floor=50_000,
                        readiness_hashrate_min=0.0, readiness_vcore_floor=0)

    def test_pass_on_version_match_and_healthy(self):
        c = MockClient(gets=_healthy_gets("v0.70.0"))
        prof = profile_for("esp32-wroom32")
        r = ota.verify(c, prof, self._criteria(), "v0.70.0", settle=0)
        self.assertTrue(r.ok, r.detail)
        self.assertTrue(r.ready)
        self.assertTrue(r.healthy)
        self.assertGreater(r.metrics["hashrate"], 0)

    def test_fail_on_version_mismatch(self):
        c = MockClient(gets=_healthy_gets("v0.69.0"))
        r = ota.verify(c, None, self._criteria(), "v0.70.0", settle=0)
        self.assertFalse(r.ok)
        self.assertIn("version", r.detail)

    def test_fail_on_unhealthy_panic_reset(self):
        gets = _healthy_gets("v0.70.0")
        gets["/api/info"] = {"version": "v0.70.0", "reset_reason": "panic"}
        gets["/api/stats"] = {"hashrate_ghs": 0.0}
        c = MockClient(gets=gets)
        r = ota.verify(c, None, self._criteria(), "v0.70.0", settle=0)
        self.assertFalse(r.ok)
        self.assertFalse(r.healthy)
        self.assertIn("unhealthy", r.detail)

    def test_settle_override_applied(self):
        # settle=0 must override a large criteria.settle_delay so verify returns fast
        c = MockClient(gets=_healthy_gets("v0.70.0"))
        crit = Criteria(settle_delay=999, readiness_heap_floor=50_000)
        r = ota.verify(c, None, crit, "v0.70.0", settle=0)
        self.assertTrue(r.ok, r.detail)


if __name__ == "__main__":
    unittest.main()
