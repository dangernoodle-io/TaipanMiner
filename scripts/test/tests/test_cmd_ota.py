"""Tests for cmd_ota_* CLI handlers (TA-456).

These tests drive the REAL cmd_ota_* handler functions with a Device
(the raw dataclass from discovery.py — no .request() method) and a
patched Client/discovery, asserting each handler constructs a Client and
reaches the ota.* functions WITHOUT AttributeError.

Coverage:
  - cmd_ota_push with Device -> no AttributeError, Client.request called
  - cmd_ota_push dry-run -> plan detail printed, NO request sent
  - cmd_ota_pull with Device -> no AttributeError, Client.request called
  - cmd_ota_mark_valid with Device -> no AttributeError, Client.request called
  - cmd_ota_recover with Device -> no AttributeError, Client.request called
  - cmd_ota_status with Device -> no AttributeError, no request (read-only)
  - cmd_ota_verify with Device -> no AttributeError, no mutating request
  - elf list IN-USE for dev/dirty running build matched via /api/info app_sha256
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device
from fleetlib.safety import Guard


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _device(ip="192.0.2.10", board="esp32-s2-mini", version="v0.70.0-dev-dirty"):
    return Device(hostname="miner-10", ip=ip, port=80, board=board, version=version)


def _args(**kwargs):
    """Build a minimal argparse.Namespace for ota handler args."""
    ns = argparse.Namespace(
        hosts=[kwargs.get("ip", "192.0.2.10")],
        discover=False,
        board=None,
        dry_run=kwargs.get("dry_run", False),
        yes=kwargs.get("yes", True),
        target_version=kwargs.get("target_version", None),
        settle=None,
        binfile=kwargs.get("binfile", "/dev/null"),
        mode=kwargs.get("mode", "auto"),
        criteria=None,
    )
    return ns


def _ok_verify_result(ok=True, version="v0.70.0", detail="ok"):
    from fleetlib.ota import VerifyResult
    return VerifyResult(ok=ok, version=version, detail=detail, healthy=ok, ready=ok)


# Patch resolve_devices to return a Device directly (bypasses live network).
def _patch_resolve(device):
    return patch("fleet.resolve_devices", return_value=[device])


# Patch verify_identity so Guard.check doesn't make real HTTP calls.
def _patch_identity(ok=True):
    return patch("fleetlib.discovery.verify_identity", return_value=ok)


# ---------------------------------------------------------------------------
# Shared mock client factory — records .request() calls
# ---------------------------------------------------------------------------

class _MockClient:
    def __init__(self, ip="192.0.2.10", port=80):
        self.ip = ip
        self.port = port
        self.board = ""
        self.request_log = []

    def get_json(self, path, timeout=5):
        return {"state": "done"}

    def request(self, method, path, body=None, timeout=10):
        self.request_log.append((method.upper(), path))
        return (200, b"")


# ---------------------------------------------------------------------------
# cmd_ota_push
# ---------------------------------------------------------------------------

class TestCmdOtaPushDeviceWrap(unittest.TestCase):
    """cmd_ota_push must wrap Device in Client; no AttributeError."""

    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.tmp.write(b"\x00firmware\xff")
        self.tmp.close()
        self.device = _device()

    def tearDown(self):
        os.unlink(self.tmp.name)

    def test_push_reaches_ota_push_no_attribute_error(self):
        """Handler must not crash with AttributeError; ota.push must receive a Client."""
        import fleet
        mock_client = _MockClient(ip=self.device.ip)
        args = _args(binfile=self.tmp.name, yes=True)

        captured_client = {}

        def fake_push(client, guard, binfile, target_version=None, settle=None,
                      elf_path=None, do_mark_valid=False):
            captured_client["client"] = client
            # Verify it has .request — no AttributeError
            client.request("POST", "/api/update/push", body=b"x")
            return _ok_verify_result()

        with _patch_resolve(self.device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client) as MockClientCls:
                    with patch("fleetlib.ota.push", side_effect=fake_push):
                        rc = fleet.cmd_ota_push(args)

        self.assertEqual(rc, 0)
        # Client was constructed with device's ip and port
        MockClientCls.assert_called_once_with(self.device.ip, 80)
        # The client passed to ota.push is the mock (not the raw Device)
        self.assertIs(captured_client["client"], mock_client)
        # .request was successfully called (no AttributeError)
        self.assertIn(("POST", "/api/update/push"), mock_client.request_log)

    def test_push_board_attr_set_on_client(self):
        """cmd_ota_push must set c.board = d.board for ELF archival."""
        import fleet
        mock_client = _MockClient(ip=self.device.ip)
        args = _args(binfile=self.tmp.name, yes=True)

        def fake_push(client, guard, binfile, **kw):
            return _ok_verify_result()

        with _patch_resolve(self.device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.ota.push", side_effect=fake_push):
                        fleet.cmd_ota_push(args)

        self.assertEqual(mock_client.board, self.device.board)

    def test_push_failure_propagates_nonzero_rc(self):
        import fleet
        mock_client = _MockClient(ip=self.device.ip)
        args = _args(binfile=self.tmp.name, yes=True)

        def fake_push(client, guard, binfile, **kw):
            return _ok_verify_result(ok=False, detail="push rejected HTTP 500")

        with _patch_resolve(self.device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.ota.push", side_effect=fake_push):
                        rc = fleet.cmd_ota_push(args)

        self.assertEqual(rc, 1)


# ---------------------------------------------------------------------------
# cmd_ota_push --dry-run plan detail
# ---------------------------------------------------------------------------

class TestCmdOtaPushDryRun(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
        self.tmp.write(b"\x00firmware\xff" * 100)
        self.tmp.close()
        self.device = _device()

    def tearDown(self):
        os.unlink(self.tmp.name)

    def test_dry_run_prints_plan_no_request_sent(self):
        """dry-run must print identity-verify, bin file, image size, host — no HTTP."""
        import fleet
        mock_client = _MockClient(ip=self.device.ip)
        args = _args(binfile=self.tmp.name, dry_run=True, yes=False)

        push_called = []

        def fake_push(client, guard, binfile, **kw):
            push_called.append(True)
            return _ok_verify_result()

        with _patch_resolve(self.device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.ota.push", side_effect=fake_push):
                        import io
                        from contextlib import redirect_stdout
                        buf = io.StringIO()
                        with redirect_stdout(buf):
                            rc = fleet.cmd_ota_push(args)

        output = buf.getvalue()
        # dry-run: ota.push must NOT be called (no HTTP)
        self.assertEqual(push_called, [], "ota.push must not be called in dry-run")
        self.assertEqual(mock_client.request_log, [])
        # Plan fields must be present
        self.assertIn("DRY-RUN", output)
        self.assertIn("identity-verify", output)
        self.assertIn(self.tmp.name, output)
        self.assertIn("bytes", output)
        self.assertIn(self.device.ip, output)
        self.assertIn("reboot", output)

    def test_dry_run_shows_identity_fail(self):
        """dry-run plan must show FAIL when identity check fails."""
        import fleet
        mock_client = _MockClient(ip=self.device.ip)
        args = _args(binfile=self.tmp.name, dry_run=True, yes=False)

        with _patch_resolve(self.device):
            with patch("fleetlib.discovery.verify_identity", return_value=False):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    import io
                    from contextlib import redirect_stdout
                    buf = io.StringIO()
                    with redirect_stdout(buf):
                        fleet.cmd_ota_push(args)

        self.assertIn("FAIL", buf.getvalue())


# ---------------------------------------------------------------------------
# cmd_ota_pull
# ---------------------------------------------------------------------------

class TestCmdOtaPullDeviceWrap(unittest.TestCase):
    def test_pull_wraps_device_in_client(self):
        import fleet
        device = _device()
        mock_client = _MockClient(ip=device.ip)
        args = _args(yes=True)

        captured = {}

        def fake_pull(client, guard, mode="auto", target_version=None, settle=None):
            captured["client"] = client
            return _ok_verify_result(version="v0.70.0")

        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client) as MockClientCls:
                    with patch("fleetlib.ota.pull", side_effect=fake_pull):
                        rc = fleet.cmd_ota_pull(args)

        self.assertEqual(rc, 0)
        MockClientCls.assert_called_once_with(device.ip, 80)
        self.assertIs(captured["client"], mock_client)

    def test_pull_failure_returns_nonzero(self):
        import fleet
        device = _device()
        mock_client = _MockClient(ip=device.ip)
        args = _args(yes=True)

        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.ota.pull",
                               return_value=_ok_verify_result(ok=False, detail="apply busy (HTTP 409)")):
                        rc = fleet.cmd_ota_pull(args)

        self.assertEqual(rc, 1)


# ---------------------------------------------------------------------------
# cmd_ota_mark_valid
# ---------------------------------------------------------------------------

class TestCmdOtaMarkValidDeviceWrap(unittest.TestCase):
    def test_mark_valid_wraps_device_in_client(self):
        import fleet
        device = _device()
        mock_client = _MockClient(ip=device.ip)
        args = _args(yes=True)

        captured = {}

        def fake_mark(client, guard):
            captured["client"] = client
            client.request("POST", "/api/update/mark-valid")
            return True

        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client) as MockClientCls:
                    with patch("fleetlib.ota.mark_valid", side_effect=fake_mark):
                        rc = fleet.cmd_ota_mark_valid(args)

        self.assertEqual(rc, 0)
        MockClientCls.assert_called_once_with(device.ip, 80)
        self.assertIs(captured["client"], mock_client)
        self.assertIn(("POST", "/api/update/mark-valid"), mock_client.request_log)


# ---------------------------------------------------------------------------
# cmd_ota_recover
# ---------------------------------------------------------------------------

class TestCmdOtaRecoverDeviceWrap(unittest.TestCase):
    def test_recover_wraps_device_in_client(self):
        import fleet
        device = _device()
        mock_client = _MockClient(ip=device.ip)
        args = _args(yes=True)

        captured = {}

        def fake_recover(client, guard):
            captured["client"] = client
            client.request("POST", "/api/update/recover")
            return True

        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client) as MockClientCls:
                    with patch("fleetlib.ota.recover", side_effect=fake_recover):
                        rc = fleet.cmd_ota_recover(args)

        self.assertEqual(rc, 0)
        MockClientCls.assert_called_once_with(device.ip, 80)
        self.assertIs(captured["client"], mock_client)
        self.assertIn(("POST", "/api/update/recover"), mock_client.request_log)


# ---------------------------------------------------------------------------
# cmd_ota_status (read-only — no request, just get_json)
# ---------------------------------------------------------------------------

class TestCmdOtaStatusDeviceWrap(unittest.TestCase):
    def test_status_wraps_device_in_client(self):
        import fleet
        device = _device()
        mock_client = _MockClient(ip=device.ip)
        args = _args(yes=False)

        captured = {}

        def fake_status(client):
            captured["client"] = client
            return {"state": "done"}

        with _patch_resolve(device):
            with patch("fleetlib.client.Client", return_value=mock_client) as MockClientCls:
                with patch("fleetlib.ota.status", side_effect=fake_status):
                    rc = fleet.cmd_ota_status(args)

        self.assertEqual(rc, 0)
        MockClientCls.assert_called_once_with(device.ip, 80)
        self.assertIs(captured["client"], mock_client)
        # No mutating requests
        self.assertEqual(mock_client.request_log, [])


# ---------------------------------------------------------------------------
# cmd_ota_verify
# ---------------------------------------------------------------------------

class TestCmdOtaVerifyDeviceWrap(unittest.TestCase):
    def test_verify_wraps_device_in_client(self):
        import fleet
        device = _device()
        mock_client = _MockClient(ip=device.ip)
        args = _args(yes=False, target_version="v0.70.0")

        captured = {}

        def fake_verify(client, profile, criteria, target_version, settle):
            captured["client"] = client
            return _ok_verify_result(version="v0.70.0")

        with _patch_resolve(device):
            with patch("fleetlib.client.Client", return_value=mock_client) as MockClientCls:
                with patch("fleetlib.ota.verify", side_effect=fake_verify):
                    rc = fleet.cmd_ota_verify(args)

        self.assertEqual(rc, 0)
        MockClientCls.assert_called_once_with(device.ip, 80)
        self.assertIs(captured["client"], mock_client)
        self.assertEqual(mock_client.request_log, [])

    def test_verify_failure_returns_nonzero(self):
        import fleet
        device = _device()
        mock_client = _MockClient(ip=device.ip)
        args = _args(yes=False, target_version="v0.70.0")

        with _patch_resolve(device):
            with patch("fleetlib.client.Client", return_value=mock_client):
                with patch("fleetlib.ota.verify",
                           return_value=_ok_verify_result(ok=False, detail="version mismatch")):
                    rc = fleet.cmd_ota_verify(args)

        self.assertEqual(rc, 1)


# ---------------------------------------------------------------------------
# elf list IN-USE: dev/dirty build matched via /api/info app_sha256
# ---------------------------------------------------------------------------

class TestElfListInUseDirty(unittest.TestCase):
    """cmd_elf_list must mark IN-USE when /api/info returns the running ELF sha prefix."""

    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.store = Path(self.td.name)

    def tearDown(self):
        self.td.cleanup()

    def _put_archive(self, sha64: str, version: str, dirty: bool = False):
        """Write a fake archive entry with the given full sha256 key."""
        (self.store / f"{sha64}.elf").write_bytes(b"x" * 64)
        meta = {
            "sha256": sha64,
            "board": "esp32-s2-mini",
            "version": version,
            "build_time": "",
            "git_sha": "",
            "dirty": dirty,
            "archived_at": "2025-01-01T00:00:00Z",
        }
        (self.store / f"{sha64}.json").write_text(json.dumps(meta))

    def test_in_use_matched_via_info_app_sha256_dirty(self):
        """A dev/dirty build with a short app_sha256 in /api/info must show IN-USE=yes."""
        import fleet
        from fleetlib.client import Client
        from fleetlib.elfstore import list_entries

        # Archived ELF: sha starts with "71cf103b"
        full_sha = "71cf103b" + "0" * 56  # 64-char
        self._put_archive(full_sha, version="v0.71.0-dev-dirty", dirty=True)

        device = _device(version="v0.71.0-dev-dirty")

        # /api/info returns a truncated app_sha256 (9 chars, as ESP-IDF default)
        short_sha = "71cf103b0"

        def fake_get_json(path, timeout=5):
            if path == "/api/info":
                return {"version": "v0.71.0-dev-dirty", "app_sha256": short_sha,
                        "board": "esp32-s2-mini"}
            return None

        mock_client = _MockClient(ip=device.ip)
        mock_client.get_json = fake_get_json

        args = _args(ip=device.ip)

        import io
        from contextlib import redirect_stdout

        with _patch_resolve(device):
            with patch("fleetlib.client.Client", return_value=mock_client):
                with patch("fleetlib.elfstore.list_entries", return_value=list_entries(self.store)):
                    buf = io.StringIO()
                    with redirect_stdout(buf):
                        rc = fleet.cmd_elf_list(args)

        output = buf.getvalue()
        self.assertEqual(rc, 0)
        # The row for this ELF must show "yes" in the IN-USE column
        self.assertIn("yes", output, f"Expected IN-USE=yes for sha {full_sha[:8]}…\n{output}")

    def test_in_use_no_match_different_sha(self):
        """An ELF whose sha does not match the running sha must show IN-USE=no."""
        import fleet
        from fleetlib.elfstore import list_entries

        full_sha = "aabbccdd" + "0" * 56
        self._put_archive(full_sha, version="v0.70.0", dirty=False)

        device = _device()
        short_sha = "deadbeef9"  # different sha

        def fake_get_json(path, timeout=5):
            if path == "/api/info":
                return {"version": "v0.70.0", "app_sha256": short_sha,
                        "board": "esp32-s2-mini"}
            return None

        mock_client = _MockClient(ip=device.ip)
        mock_client.get_json = fake_get_json

        args = _args(ip=device.ip)

        import io
        from contextlib import redirect_stdout

        with _patch_resolve(device):
            with patch("fleetlib.client.Client", return_value=mock_client):
                with patch("fleetlib.elfstore.list_entries", return_value=list_entries(self.store)):
                    buf = io.StringIO()
                    with redirect_stdout(buf):
                        rc = fleet.cmd_elf_list(args)

        output = buf.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("no", output)

    def test_in_use_fallback_to_panic_sha(self):
        """When /api/info has no app_sha256, fallback to /api/diag/panic."""
        import fleet
        from fleetlib.elfstore import list_entries

        full_sha = "cafebabe" + "0" * 56
        self._put_archive(full_sha, version="v0.69.0", dirty=False)

        device = _device()
        short_sha = "cafebabe0"

        def fake_get_json(path, timeout=5):
            if path == "/api/info":
                return {"version": "v0.69.0", "board": "esp32-s2-mini"}  # no app_sha256
            if path == "/api/diag/panic":
                return {"app_sha256": short_sha}
            return None

        mock_client = _MockClient(ip=device.ip)
        mock_client.get_json = fake_get_json

        args = _args(ip=device.ip)

        import io
        from contextlib import redirect_stdout

        with _patch_resolve(device):
            with patch("fleetlib.client.Client", return_value=mock_client):
                with patch("fleetlib.elfstore.list_entries", return_value=list_entries(self.store)):
                    buf = io.StringIO()
                    with redirect_stdout(buf):
                        rc = fleet.cmd_elf_list(args)

        output = buf.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("yes", output, f"Expected IN-USE=yes via panic fallback\n{output}")


if __name__ == "__main__":
    unittest.main()
