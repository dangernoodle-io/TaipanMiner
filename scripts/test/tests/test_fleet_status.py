"""Offline unit tests for fleet.py cmd_status health parsing."""
import io
import os
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device


def _make_device(ip="192.0.2.10", board="esp32-wroom32") -> Device:
    return Device(hostname=board, ip=ip, port=80, board=board, version="v0.99.0")


def _make_client(info=None, health=None, heap=None):
    """Build a mock Client returning canned responses."""
    c = MagicMock()

    def _get_json(path, timeout=5):
        mapping = {
            "/api/info": info,
            "/api/health": health,
            "/api/diag/heap": heap,
        }
        return mapping.get(path)

    c.get_json = _get_json
    return c


def _run_status(devices):
    """Call cmd_status with mocked args; return (exit_code, stdout_text)."""
    import fleet

    args = MagicMock()
    args.hosts = ",".join(d.ip for d in devices)
    args.board = None
    args.discover_timeout = 10

    buf = io.StringIO()
    with patch("fleet.resolve_devices", return_value=devices):
        with patch("sys.stdout", buf):
            code = fleet.cmd_status(args)

    return code, buf.getvalue()


# ---------------------------------------------------------------------------
# Health field: {"ok": true/false} — not {"status": "ok"}
# ---------------------------------------------------------------------------

class TestHealthParsing(unittest.TestCase):
    def test_ok_true_shows_ok_exits_0(self):
        """/api/health {"ok": true} → column shows 'ok', exits 0."""
        device = _make_device()
        info = {"uptime_ms": 60_000, "board": "esp32-wroom32", "version": "v0.99.0",
                "free_heap": 75_000}
        health = {"ok": True, "validated": True}
        client = _make_client(info=info, health=health)

        with patch("fleetlib.client.Client", return_value=client):
            code, out = _run_status([device])

        self.assertEqual(code, 0, f"expected exit 0, got {code}\n{out}")
        self.assertIn("ok", out)
        self.assertNotIn("??", out)
        self.assertNotIn("unhealthy", out)

    def test_ok_false_shows_unhealthy_exits_1(self):
        """/api/health {"ok": false} → column shows 'unhealthy', exits 1."""
        device = _make_device()
        info = {"uptime_ms": 60_000, "board": "esp32-wroom32", "version": "v0.99.0",
                "free_heap": 75_000}
        health = {"ok": False}
        client = _make_client(info=info, health=health)

        with patch("fleetlib.client.Client", return_value=client):
            code, out = _run_status([device])

        self.assertEqual(code, 1, f"expected exit 1, got {code}\n{out}")
        self.assertIn("unhealthy", out)

    def test_health_none_shows_question_exits_0(self):
        """/api/health unreachable (None) → column shows '??', exits 0 (host IS reachable)."""
        device = _make_device()
        info = {"uptime_ms": 60_000, "board": "esp32-wroom32", "version": "v0.99.0",
                "free_heap": 75_000}
        client = _make_client(info=info, health=None)

        with patch("fleetlib.client.Client", return_value=client):
            code, out = _run_status([device])

        self.assertEqual(code, 0, f"expected exit 0, got {code}\n{out}")
        self.assertIn("??", out)

    def test_info_none_shows_unreachable_exits_1(self):
        """/api/info unreachable → shows UNREACHABLE, exits 1."""
        device = _make_device()
        client = _make_client(info=None, health=None)

        with patch("fleetlib.client.Client", return_value=client):
            code, out = _run_status([device])

        self.assertEqual(code, 1)
        self.assertIn("UNREACHABLE", out)

    def test_old_status_field_does_not_cause_ok(self):
        """Old firmware returning {'status': 'ok'} (wrong field) shows '??' not 'ok'."""
        device = _make_device()
        info = {"uptime_ms": 60_000, "board": "esp32-wroom32", "version": "v0.99.0",
                "free_heap": 75_000}
        # Wrong field name — must NOT be treated as ok
        health = {"status": "ok"}
        client = _make_client(info=info, health=health)

        with patch("fleetlib.client.Client", return_value=client):
            code, out = _run_status([device])

        # Should show ?? (unknown), NOT 'ok', and NOT exit 1
        self.assertEqual(code, 0, f"unexpected exit {code}\n{out}")
        self.assertNotIn(" ok", out.split("HEALTH")[-1] if "HEALTH" in out else out)


if __name__ == "__main__":
    unittest.main()
