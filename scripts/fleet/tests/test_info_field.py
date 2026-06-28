"""Offline unit tests for B1-360 info_field accessor and routed sites (TA-467).

Covers:
  - info_field: build-shape returns build value; missing build returns default;
    missing key in build returns default; build not a dict returns default.
  - discovery._enrich / _enrich_with_reason: board/version from build.*
  - discovery.verify_identity: board check uses build.*
  - ota.wait_for_boot: version from build.*
  - ota.verify: version from build.*
  - ota._post_boot_verify (via push): version from build.* (false-negative fix)
  - fleet.cmd_status: BOARD/VERSION columns from build.*
  - fleet.cmd_elf_list: IN-USE resolved via build.app_sha256
"""
from __future__ import annotations
import io
import os
import sys
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch, MagicMock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.client import info_field
from fleetlib.discovery import Device


# ---------------------------------------------------------------------------
# info_field unit tests
# ---------------------------------------------------------------------------

class TestInfoField(unittest.TestCase):
    def _build_info(self, **build_kwargs):
        return {"build": build_kwargs, "uptime_ms": 1000, "mac": "aa:bb:cc"}

    def test_build_shape_returns_build_value(self):
        info = self._build_info(version="v1.2.3", board="esp32-wroom32")
        self.assertEqual(info_field(info, "version"), "v1.2.3")
        self.assertEqual(info_field(info, "board"), "esp32-wroom32")

    def test_build_shape_app_sha256(self):
        info = self._build_info(app_sha256="dd159641e")
        self.assertEqual(info_field(info, "app_sha256"), "dd159641e")

    def test_missing_build_returns_default(self):
        info = {"uptime_ms": 1000, "version": "old", "board": "old-board"}
        self.assertIsNone(info_field(info, "version"))
        self.assertIsNone(info_field(info, "board"))

    def test_missing_build_custom_default(self):
        info = {"uptime_ms": 1000}
        self.assertEqual(info_field(info, "version", "unknown"), "unknown")

    def test_missing_key_in_build_returns_default(self):
        info = self._build_info(version="v1.0.0")
        self.assertIsNone(info_field(info, "board"))
        self.assertEqual(info_field(info, "board", "unknown"), "unknown")

    def test_build_not_a_dict_returns_default(self):
        info = {"build": "not-a-dict", "version": "top"}
        self.assertIsNone(info_field(info, "version"))

    def test_build_none_returns_default(self):
        info = {"build": None}
        self.assertIsNone(info_field(info, "version"))

    def test_empty_info_returns_default(self):
        self.assertIsNone(info_field({}, "version"))

    def test_dynamic_top_level_fields_not_routed(self):
        """uptime_ms / mac stay top-level — info_field must not see them."""
        info = {"build": {}, "uptime_ms": 99000, "mac": "aa:bb:cc:dd:ee:ff"}
        # Dynamic fields are read directly from info, not through info_field
        self.assertEqual(info.get("uptime_ms"), 99000)
        self.assertEqual(info.get("mac"), "aa:bb:cc:dd:ee:ff")
        # info_field returns nothing for absent build keys
        self.assertIsNone(info_field(info, "uptime_ms"))


# ---------------------------------------------------------------------------
# discovery — _enrich and _enrich_with_reason
# ---------------------------------------------------------------------------

class TestDiscoveryEnrich(unittest.TestCase):
    def _build_info_response(self):
        return {
            "hostname": "taipan-81",
            "build": {"board": "esp32-wroom32", "version": "dev-f218d41-bb-7043c1e"},
            "uptime_ms": 5000,
        }

    def test_enrich_board_version_from_build(self):
        from fleetlib import discovery
        info = self._build_info_response()
        with patch("fleetlib.discovery.Client") as MockClient:
            MockClient.return_value.get_json.return_value = info
            d = discovery._enrich("192.0.2.81")
        self.assertIsNotNone(d)
        self.assertEqual(d.board, "esp32-wroom32")
        self.assertEqual(d.version, "dev-f218d41-bb-7043c1e")

    def test_enrich_with_reason_board_version_from_build(self):
        from fleetlib import discovery
        import urllib.request
        info = self._build_info_response()
        fake_resp = MagicMock()
        fake_resp.__enter__ = lambda s: s
        fake_resp.__exit__ = MagicMock(return_value=False)
        fake_resp.read.return_value = json.dumps(info).encode()
        with patch("urllib.request.urlopen", return_value=fake_resp):
            d, failure = discovery._enrich_with_reason("192.0.2.81")
        self.assertIsNotNone(d)
        self.assertIsNone(failure)
        self.assertEqual(d.board, "esp32-wroom32")
        self.assertEqual(d.version, "dev-f218d41-bb-7043c1e")

    def test_enrich_missing_build_board_unknown(self):
        """When build is absent, board/version fall back to 'unknown'."""
        from fleetlib import discovery
        info = {"hostname": "old-device", "uptime_ms": 1000}
        with patch("fleetlib.discovery.Client") as MockClient:
            MockClient.return_value.get_json.return_value = info
            d = discovery._enrich("192.0.2.107")
        self.assertIsNotNone(d)
        self.assertEqual(d.board, "unknown")
        self.assertEqual(d.version, "unknown")


# ---------------------------------------------------------------------------
# discovery — verify_identity
# ---------------------------------------------------------------------------

class TestVerifyIdentity(unittest.TestCase):
    def test_board_match_via_build(self):
        from fleetlib import discovery
        info = {"build": {"board": "esp32-wroom32"}, "hostname": "taipan-81"}
        d = Device(hostname="taipan-81", ip="192.0.2.81", port=80,
                   board="esp32-wroom32", version="v1.0.0")
        with patch("fleetlib.discovery.Client") as MockClient:
            MockClient.return_value.get_json.return_value = info
            result = discovery.verify_identity(d, expect_board="esp32-wroom32")
        self.assertTrue(result)

    def test_board_mismatch_via_build(self):
        from fleetlib import discovery
        info = {"build": {"board": "bitaxe-601"}, "hostname": "taipan-81"}
        d = Device(hostname="taipan-81", ip="192.0.2.81", port=80,
                   board="esp32-wroom32", version="v1.0.0")
        with patch("fleetlib.discovery.Client") as MockClient:
            MockClient.return_value.get_json.return_value = info
            result = discovery.verify_identity(d, expect_board="esp32-wroom32")
        self.assertFalse(result)

    def test_no_build_board_mismatch(self):
        """Without build object, board is None which != expect_board."""
        from fleetlib import discovery
        info = {"hostname": "taipan-81"}
        d = Device(hostname="taipan-81", ip="192.0.2.81", port=80,
                   board="esp32-wroom32", version="v1.0.0")
        with patch("fleetlib.discovery.Client") as MockClient:
            MockClient.return_value.get_json.return_value = info
            result = discovery.verify_identity(d, expect_board="esp32-wroom32")
        self.assertFalse(result)


# ---------------------------------------------------------------------------
# ota — wait_for_boot resolves version from build.*
# ---------------------------------------------------------------------------

class TestOtaWaitForBootBuildShape(unittest.TestCase):
    def setUp(self):
        self._sleep = patch("fleetlib.ota.time.sleep", lambda *a: None)
        self._sleep.start()

    def tearDown(self):
        self._sleep.stop()

    def _build_info(self, version):
        return {"build": {"version": version}, "uptime_ms": 5000}

    def test_waits_for_target_version_in_build(self):
        from fleetlib import ota

        class MockClient:
            ip = "192.0.2.81"
            _calls = 0
            def get_json(self, path, timeout=5):
                if path == "/api/health":
                    return {"ok": True}
                if path == "/api/info":
                    self._calls += 1
                    # first call returns wrong version, second returns target
                    if self._calls == 1:
                        return {"build": {"version": "v0.69.0"}, "uptime_ms": 1000}
                    return {"build": {"version": "v0.70.0"}, "uptime_ms": 2000}

        c = MockClient()
        result = ota.wait_for_boot(c, target_version="v0.70.0")
        self.assertEqual(result, "v0.70.0")

    def test_returns_none_when_build_absent(self):
        from fleetlib import ota

        class MockClient:
            ip = "192.0.2.107"
            def get_json(self, path, timeout=5):
                if path == "/api/health":
                    return {"ok": True}
                # old-shape: no build object
                return {"version": "v0.69.0", "uptime_ms": 1000}

        c = MockClient()
        # target won't match because info_field returns None (no build object)
        result = ota.wait_for_boot(c, target_version="v0.70.0", timeout=0.05)
        self.assertIsNone(result)


# ---------------------------------------------------------------------------
# ota — verify resolves version from build.*
# ---------------------------------------------------------------------------

class TestOtaVerifyBuildShape(unittest.TestCase):
    def setUp(self):
        self._sleep = patch("fleetlib.ota.time.sleep", lambda *a: None)
        self._sleep.start()

    def tearDown(self):
        self._sleep.stop()

    def _criteria(self):
        from fleetlib.criteria import Criteria
        return Criteria(settle_delay=0, readiness_heap_floor=0,
                        readiness_hashrate_min=0.0, readiness_vcore_floor=0)

    def test_pass_version_from_build(self):
        from fleetlib import ota

        class MockClient:
            ip = "192.0.2.81"
            def get_json(self, path, timeout=5):
                if path == "/api/info":
                    return {"build": {"version": "v0.70.0"}, "uptime_ms": 5000,
                            "reset_reason": "software"}
                if path == "/api/health":
                    return {"ok": True}
                if path == "/api/stats":
                    return {"hashrate_ghs": 485.0}
                if path == "/api/diag/heap":
                    return {"internal": {"free": 80000}}
                return None
            def request(self, *a, **k):
                return (200, b"")

        c = MockClient()
        r = ota.verify(c, None, self._criteria(), "v0.70.0", settle=0)
        self.assertTrue(r.ok, r.detail)
        self.assertEqual(r.version, "v0.70.0")

    def test_fail_version_mismatch_build_shape(self):
        from fleetlib import ota

        class MockClient:
            ip = "192.0.2.81"
            def get_json(self, path, timeout=5):
                if path == "/api/info":
                    return {"build": {"version": "v0.69.0"}, "uptime_ms": 5000}
                if path == "/api/health":
                    return {"ok": True}
                if path == "/api/stats":
                    return {"hashrate_ghs": 485.0}
                if path == "/api/diag/heap":
                    return {"internal": {"free": 80000}}
                return None
            def request(self, *a, **k):
                return (200, b"")

        c = MockClient()
        r = ota.verify(c, None, self._criteria(), "v0.70.0", settle=0)
        self.assertFalse(r.ok)
        self.assertIn("version", r.detail)


# ---------------------------------------------------------------------------
# fleet.cmd_status — BOARD/VERSION from build.*
# ---------------------------------------------------------------------------

class TestCmdStatusBuildShape(unittest.TestCase):
    def _make_device(self, ip="192.0.2.81"):
        return Device(hostname="taipan-81", ip=ip, port=80,
                      board="esp32-wroom32", version="v0.99.0")

    def _run_status(self, devices, client_mock):
        import fleet
        args = MagicMock()
        args.hosts = ",".join(d.ip for d in devices)
        args.board = None
        args.discover_timeout = 10
        buf = io.StringIO()
        with patch("fleet.resolve_devices", return_value=devices):
            with patch("fleetlib.client.Client", return_value=client_mock):
                with patch("sys.stdout", buf):
                    code = fleet.cmd_status(args)
        return code, buf.getvalue()

    def test_board_version_from_build(self):
        """BOARD/VERSION columns resolved from info.build.* on B1-360 firmware."""
        info = {
            "build": {"board": "esp32-wroom32", "version": "dev-f218d41-bb-7043c1e"},
            "uptime_ms": 5000,
        }
        c = MagicMock()
        c.get_json.side_effect = lambda path, timeout=5: (
            info if path == "/api/info" else
            {"ok": True} if path == "/api/health" else
            {"internal": {"free": 80000}} if "heap" in path else None
        )
        d = self._make_device()
        code, out = self._run_status([d], c)
        self.assertEqual(code, 0)
        self.assertIn("esp32-wroom32", out)
        self.assertIn("dev-f218d41", out)

    def test_board_version_unknown_when_no_build(self):
        """Without build object, board/version fall back to Device attrs (from discovery)."""
        info = {"uptime_ms": 5000}
        c = MagicMock()
        c.get_json.side_effect = lambda path, timeout=5: (
            info if path == "/api/info" else
            {"ok": True} if path == "/api/health" else
            {"internal": {"free": 80000}} if "heap" in path else None
        )
        d = self._make_device()
        code, out = self._run_status([d], c)
        # Falls back to d.board / d.version from Device (set at discovery time)
        self.assertEqual(code, 0)
        self.assertIn("esp32-wroom32", out)


# ---------------------------------------------------------------------------
# fleet.cmd_elf_list — IN-USE via build.app_sha256
# ---------------------------------------------------------------------------

class TestElfListInUseBuildShape(unittest.TestCase):
    def test_in_use_via_build_app_sha256(self):
        """cmd_elf_list marks IN-USE when build.app_sha256 matches archive prefix."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            data = b"elf_inuse_build_shape_test"
            sha = hashlib.sha256(data).hexdigest()
            (store / f"{sha}.elf").write_bytes(data)
            (store / f"{sha}.json").write_text(json.dumps({
                "sha256": sha, "board": "esp32-wroom32", "version": "dev-f218d41",
                "build_time": "", "git_sha": "", "dirty": False,
                "archived_at": "2026-06-26T00:00:00Z",
            }))

            short_sha = sha[:9]
            # Device returns build-shape /api/info with build.app_sha256
            mock_client = MagicMock()
            mock_client.get_json.return_value = {
                "build": {"app_sha256": short_sha, "board": "esp32-wroom32",
                          "version": "dev-f218d41"},
                "uptime_ms": 5000,
            }
            device = Device(hostname="taipan-81", ip="192.0.2.81", port=80,
                            board="esp32-wroom32", version="dev-f218d41")
            import fleet
            args = MagicMock()
            args.hosts = "192.0.2.81"
            args.board = None
            args.discover_timeout = 10
            with patch("fleet.resolve_devices", return_value=[device]):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                        with patch("sys.stdout", new_callable=io.StringIO) as mock_out:
                            rc = fleet.cmd_elf_list(args)

            self.assertEqual(rc, 0)
            out = mock_out.getvalue()
            self.assertIn(sha[:16], out)
            self.assertIn("yes", out)  # IN-USE column


if __name__ == "__main__":
    unittest.main()
