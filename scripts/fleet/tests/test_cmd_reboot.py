"""Tests for the fleet reboot command (TA-492).

Coverage:
  - dry-run: no HTTP issued, plan printed, identity-verify shown, exit 0
  - successful reboot: POST issued; sc=None (reboot reset) treated as success; exit 0
  - per-host isolation: [healthy, unreachable, healthy] -> both healthy rebooted,
    middle SKIPPED, exit non-zero
  - identity mismatch on a host -> SKIPPED, exit non-zero, others still processed
  - --settle: wait_until_ready invoked; ready -> exit 0; not-ready -> exit 1
  - non-success HTTP (e.g. 500) -> FAILED, exit non-zero
"""
from __future__ import annotations

import argparse
import io
import os
import sys
import unittest
from contextlib import redirect_stdout
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device
from fleetlib.safety import DeviceUnreachable, IdentityMismatch
from fleetlib.ota import VerifyResult
import commands.reboot as reboot_cmd


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _device(ip="192.0.2.10", board="esp32-s2-mini", version="v0.70.0"):
    return Device(hostname="miner-10", ip=ip, port=80, board=board, version=version)


def _args(**kwargs):
    ns = argparse.Namespace(
        hosts=[kwargs.get("ip", "192.0.2.10")],
        discover=False,
        board=None,
        dry_run=kwargs.get("dry_run", False),
        yes=kwargs.get("yes", True),
        settle=kwargs.get("settle", None),
        criteria=None,
        fields=None,
        gates=[],
        skip_gates=[],
        out_json=None,
        out_junit=None,
        baseline=None,
        metrics_mqtt_url=None,
        metrics_topic="fleettest",
        no_publish_metrics=False,
        discover_timeout=10,
    )
    return ns


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


def _patch_resolve(devices_or_device):
    if isinstance(devices_or_device, list):
        return patch("commands.reboot.resolve_devices", return_value=devices_or_device)
    return patch("commands.reboot.resolve_devices", return_value=[devices_or_device])


def _patch_identity(ok=True):
    val = ("test-board", "test-host") if ok else (None, None)
    return patch("fleetlib.discovery._read_identity", return_value=val)


# ---------------------------------------------------------------------------
# dry-run: no HTTP, plan printed, exit 0
# ---------------------------------------------------------------------------

class TestRebootDryRun(unittest.TestCase):
    def test_dry_run_prints_plan_no_http(self):
        device = _device()
        args = _args(dry_run=True, yes=False)
        mock_client = _MockClient(ip=device.ip)

        buf = io.StringIO()
        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with redirect_stdout(buf):
                        rc = reboot_cmd.run(args)

        output = buf.getvalue()
        self.assertEqual(rc, 0)
        # No HTTP issued
        self.assertEqual(mock_client.request_log, [])
        # Plan fields present
        self.assertIn("DRY-RUN", output)
        self.assertIn("identity-verify", output)
        self.assertIn(device.ip, output)
        self.assertIn("/api/reboot", output)
        self.assertIn("no HTTP sent", output)

    def test_dry_run_shows_identity_fail(self):
        device = _device()
        args = _args(dry_run=True, yes=False)
        mock_client = _MockClient(ip=device.ip)

        buf = io.StringIO()
        with _patch_resolve(device):
            with patch("fleetlib.discovery.verify_identity", return_value=False):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with redirect_stdout(buf):
                        reboot_cmd.run(args)

        self.assertIn("FAIL", buf.getvalue())

    def test_dry_run_with_settle_mentions_settle(self):
        device = _device()
        args = _args(dry_run=True, yes=False, settle=30)
        mock_client = _MockClient(ip=device.ip)

        buf = io.StringIO()
        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with redirect_stdout(buf):
                        rc = reboot_cmd.run(args)

        output = buf.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("would wait until ready", output)


# ---------------------------------------------------------------------------
# Successful reboot: sc=None treated as success
# ---------------------------------------------------------------------------

class TestRebootSuccess(unittest.TestCase):
    def test_reboot_issued_exit_0(self):
        device = _device()
        args = _args(yes=True)
        mock_client = _MockClient(ip=device.ip)

        ok_result = VerifyResult(ok=True, detail="reboot issued")

        buf = io.StringIO()
        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.ota.reboot", return_value=ok_result):
                        with redirect_stdout(buf):
                            rc = reboot_cmd.run(args)

        self.assertEqual(rc, 0)
        output = buf.getvalue()
        self.assertIn("reboot issued", output)

    def test_reboot_sc_none_is_success(self):
        """The fleetlib.reboot primitive must accept sc=None as success."""
        from fleetlib.safety import Guard
        from fleetlib.client import Client

        mock_client = _MockClient(ip="192.0.2.10")
        # sc=None: connection reset because device rebooted
        mock_client.request = lambda method, path, body=None, timeout=10: (None, None)

        guard = Guard(dry_run=False, confirm=True)

        with patch("fleetlib.discovery._read_identity", return_value=("test-board", "test-host")):
            import fleetlib.ota as _ota
            result = _ota.reboot(mock_client, guard, settle=None)

        self.assertTrue(result.ok, f"sc=None should be success, got: {result.detail}")
        self.assertEqual(result.detail, "reboot issued")

    def test_reboot_sc_200_is_success(self):
        """sc=200 is also a valid reboot acceptance."""
        from fleetlib.safety import Guard

        mock_client = _MockClient(ip="192.0.2.10")
        mock_client.request = lambda method, path, body=None, timeout=10: (200, b"ok")

        guard = Guard(dry_run=False, confirm=True)

        with patch("fleetlib.discovery._read_identity", return_value=("test-board", "test-host")):
            import fleetlib.ota as _ota
            result = _ota.reboot(mock_client, guard, settle=None)

        self.assertTrue(result.ok)


# ---------------------------------------------------------------------------
# Per-host isolation: [healthy, unreachable, healthy]
# ---------------------------------------------------------------------------

class TestRebootHostIsolation(unittest.TestCase):
    def test_unreachable_middle_host_skipped_others_continue(self):
        dev_a = _device(ip="192.0.2.10")
        dev_b = _device(ip="192.0.2.28")   # unreachable
        dev_c = _device(ip="192.0.2.77")

        devices = [dev_a, dev_b, dev_c]
        args = _args(yes=True)

        rebooted_ips = []

        def fake_reboot(client, guard, settle=None, **kw):
            if client.ip == "192.0.2.28":
                raise DeviceUnreachable(f"unreachable: {client.ip}")
            rebooted_ips.append(client.ip)
            return VerifyResult(ok=True, detail="reboot issued")

        buf = io.StringIO()
        with _patch_resolve(devices):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", side_effect=lambda ip, port=80: _MockClient(ip=ip, port=port)):
                    with patch("fleetlib.ota.reboot", side_effect=fake_reboot):
                        with redirect_stdout(buf):
                            rc = reboot_cmd.run(args)

        output = buf.getvalue()

        # Both healthy hosts rebooted
        self.assertIn("192.0.2.10", rebooted_ips)
        self.assertIn("192.0.2.77", rebooted_ips)
        # Unreachable host not rebooted
        self.assertNotIn("192.0.2.28", rebooted_ips)
        # Non-zero exit (one host failed)
        self.assertEqual(rc, 1)
        # Output mentions SKIPPED
        self.assertIn("192.0.2.28", output)
        self.assertIn("SKIPPED", output)


# ---------------------------------------------------------------------------
# Identity mismatch: SKIPPED, exit non-zero, others processed
# ---------------------------------------------------------------------------

class TestRebootIdentityMismatch(unittest.TestCase):
    def test_identity_mismatch_skipped_others_continue(self):
        dev_a = _device(ip="192.0.2.10")
        dev_b = _device(ip="192.0.2.28")   # identity mismatch
        dev_c = _device(ip="192.0.2.77")

        devices = [dev_a, dev_b, dev_c]
        args = _args(yes=True)

        rebooted_ips = []

        def fake_reboot(client, guard, settle=None, **kw):
            if client.ip == "192.0.2.28":
                raise IdentityMismatch("board mismatch for test")
            rebooted_ips.append(client.ip)
            return VerifyResult(ok=True, detail="reboot issued")

        buf = io.StringIO()
        with _patch_resolve(devices):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", side_effect=lambda ip, port=80: _MockClient(ip=ip, port=port)):
                    with patch("fleetlib.ota.reboot", side_effect=fake_reboot):
                        with redirect_stdout(buf):
                            rc = reboot_cmd.run(args)

        output = buf.getvalue()
        self.assertIn("192.0.2.10", rebooted_ips)
        self.assertIn("192.0.2.77", rebooted_ips)
        self.assertNotIn("192.0.2.28", rebooted_ips)
        self.assertEqual(rc, 1)
        self.assertIn("SKIPPED", output)
        self.assertIn("identity mismatch", output)


# ---------------------------------------------------------------------------
# --settle: wait_until_ready invoked
# ---------------------------------------------------------------------------

class TestRebootSettle(unittest.TestCase):
    def test_settle_ready_exit_0(self):
        device = _device()
        from core import SETTLE_BARE
        args = _args(yes=True, settle=30)

        ready_result = VerifyResult(ok=True, ready=True, detail="ready")

        buf = io.StringIO()
        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=_MockClient(ip=device.ip)):
                    with patch("fleetlib.ota.reboot", return_value=ready_result) as mock_reboot:
                        with redirect_stdout(buf):
                            rc = reboot_cmd.run(args)

        self.assertEqual(rc, 0)
        output = buf.getvalue()
        self.assertIn("ready", output)
        # reboot was called with settle=30
        call_kwargs = mock_reboot.call_args
        self.assertEqual(call_kwargs[1].get("settle") or call_kwargs[0][2], 30)

    def test_settle_not_ready_exit_1(self):
        device = _device()
        args = _args(yes=True, settle=30)

        not_ready_result = VerifyResult(ok=False, ready=False, detail="not ready: heap_free unavailable")

        buf = io.StringIO()
        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=_MockClient(ip=device.ip)):
                    with patch("fleetlib.ota.reboot", return_value=not_ready_result):
                        with redirect_stdout(buf):
                            rc = reboot_cmd.run(args)

        self.assertEqual(rc, 1)
        output = buf.getvalue()
        self.assertIn("FAILED", output)

    def test_settle_via_wait_until_ready(self):
        """fleetlib.ota.reboot with settle invokes wait_until_ready after wait_for_boot."""
        from fleetlib.safety import Guard
        from fleetlib.readiness import Readiness

        mock_client = _MockClient(ip="192.0.2.10")
        mock_client.request = lambda method, path, body=None, timeout=10: (None, None)

        guard = Guard(dry_run=False, confirm=True)

        ready = Readiness(ready=True, elapsed_s=5.0, reason="ready")

        with patch("fleetlib.discovery._read_identity", return_value=("test-board", "test-host")):
            with patch("fleetlib.ota.wait_for_boot", return_value="v0.70.0") as mock_boot:
                with patch("fleetlib.ota.wait_until_ready", return_value=ready) as mock_ready:
                    import fleetlib.ota as _ota
                    result = _ota.reboot(mock_client, guard, settle=10)

        mock_boot.assert_called_once()
        mock_ready.assert_called_once()
        self.assertTrue(result.ok)
        self.assertTrue(result.ready)


# ---------------------------------------------------------------------------
# Non-success HTTP -> FAILED
# ---------------------------------------------------------------------------

class TestRebootHttpError(unittest.TestCase):
    def test_http_500_failed_exit_1(self):
        device = _device()
        args = _args(yes=True)

        failed_result = VerifyResult(ok=False, detail="reboot rejected HTTP 500")

        buf = io.StringIO()
        with _patch_resolve(device):
            with _patch_identity(True):
                with patch("fleetlib.client.Client", return_value=_MockClient(ip=device.ip)):
                    with patch("fleetlib.ota.reboot", return_value=failed_result):
                        with redirect_stdout(buf):
                            rc = reboot_cmd.run(args)

        self.assertEqual(rc, 1)
        output = buf.getvalue()
        self.assertIn("FAILED", output)

    def test_http_500_in_primitive(self):
        """fleetlib.ota.reboot must return ok=False for unexpected HTTP status."""
        from fleetlib.safety import Guard

        mock_client = _MockClient(ip="192.0.2.10")
        mock_client.request = lambda method, path, body=None, timeout=10: (500, b"error")

        guard = Guard(dry_run=False, confirm=True)

        with patch("fleetlib.discovery._read_identity", return_value=("test-board", "test-host")):
            import fleetlib.ota as _ota
            result = _ota.reboot(mock_client, guard, settle=None)

        self.assertFalse(result.ok)
        self.assertIn("500", result.detail)


if __name__ == "__main__":
    unittest.main()
