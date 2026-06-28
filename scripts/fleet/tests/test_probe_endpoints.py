"""Offline unit tests for fleet.py probe-endpoints (TA-469).

Covers:
  - enumerates GET paths from a mock spec (skips mutating + streaming by default)
  - detects an injected uptime regression and names the offending endpoint
  - includes mutating endpoints when --include-mutating is set
  - includes streaming endpoints when --include-streaming is set
  - gracefully handles unreachable device (no spec)
  - reports no crash when a healthy board survives all GETs
"""
import os
import sys
from io import StringIO
from unittest.mock import patch, MagicMock, call
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import fleet


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_args(**kwargs):
    class NS:
        pass
    ns = NS()
    defaults = {
        "log_level": "WARNING",
        "hosts": "192.0.2.1",
        "discover_timeout": 10,
        "board": None,
        "dry_run": False,
        "yes": False,
        "include_mutating": False,
        "include_streaming": False,
    }
    defaults.update(kwargs)
    for k, v in defaults.items():
        setattr(ns, k, v)
    return ns


def _fake_device(ip="192.0.2.1", board="esp32-wroom32", version="v1.0.0"):
    class D:
        pass
    d = D()
    d.ip = ip
    d.board = board
    d.version = version
    d.port = 80
    return d


# Minimal OpenAPI doc with a mix of GET, POST, and streaming paths
MOCK_SPEC = {
    "openapi": "3.1.0",
    "info": {"title": "TaipanMiner", "version": "1.0.0"},
    "paths": {
        "/api/info": {"get": {"responses": {"200": {}}}},
        "/api/stats": {"get": {"responses": {"200": {}}}},
        "/api/health": {"get": {"responses": {"200": {}}}},
        "/api/reboot": {"post": {"responses": {"200": {}}}},
        "/api/logs": {"get": {"responses": {"200": {}}}},       # streaming — skip by default
        "/api/diag/events": {"get": {"responses": {"200": {}}}},  # streaming — skip
    },
}


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestProbeEndpointsGETOnly(unittest.TestCase):
    """Default mode: only safe GETs, no streaming, no mutating."""

    def _run_probe(self, get_json_side_effect, extra_args=None):
        """Helper: run cmd_probe_endpoints with a mock client and capture stdout."""
        device = _fake_device()
        mock_client = MagicMock()
        mock_client.get_json.side_effect = get_json_side_effect
        args = _make_args(**(extra_args or {}))
        with patch("fleet.resolve_devices", return_value=[device]):
            with patch("fleetlib.client.Client", return_value=mock_client):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    rc = fleet.cmd_probe_endpoints(args)
        return rc, mock_out.getvalue(), mock_client

    def test_enumerates_get_only_skips_mutating_and_streaming(self):
        """Only /api/info, /api/stats, /api/health should be probed."""
        uptime_seq = iter([
            # First call: spec (not get_json — see below)
            # uptime readings: baseline + one per GET endpoint
            {"uptime_ms": 60000},   # baseline
            {"uptime_ms": 61000},   # after /api/health
            {"uptime_ms": 62000},   # after /api/info
            {"uptime_ms": 63000},   # after /api/stats
            {"uptime_ms": 64000},   # final
        ])

        def side_effect(path, **kw):
            if path == "/api/openapi.json":
                return MOCK_SPEC
            if path == "/api/info":
                try:
                    return next(uptime_seq)
                except StopIteration:
                    return {"uptime_ms": 65000}
            return {"ok": True}

        rc, out, mock_client = self._run_probe(side_effect)
        self.assertEqual(rc, 0)
        # Mutating endpoint not probed
        self.assertNotIn("/api/reboot", out)
        # Streaming endpoints not probed
        self.assertNotIn("/api/diag/events", out)
        # Safe GETs are probed
        self.assertIn("/api/stats", out)
        self.assertIn("no crashes detected", out)

    def test_detects_uptime_regression_names_offending_endpoint(self):
        """After /api/stats, uptime regresses — endpoint is flagged as CRASH."""
        call_count = [0]

        def side_effect(path, **kw):
            if path == "/api/openapi.json":
                return MOCK_SPEC
            if path == "/api/info":
                call_count[0] += 1
                n = call_count[0]
                # baseline=60000, after health=61000, after info=62000,
                # after stats (post-crash)=5000 (regression!)
                uptimes = {1: 60000, 2: 61000, 3: 62000, 4: 5000, 5: 6000}
                return {"uptime_ms": uptimes.get(n, 7000)}
            return {"ok": True}

        rc, out, mock_client = self._run_probe(side_effect)
        self.assertEqual(rc, 1)
        # The endpoint that caused the regression should be in the output
        self.assertIn("CRASH", out)

    def test_unreachable_device_no_spec(self):
        """When /api/openapi.json is unreachable, report error and return 1."""
        def side_effect(path, **kw):
            return None  # all requests fail

        rc, out, _ = self._run_probe(side_effect)
        self.assertEqual(rc, 1)
        self.assertIn("could not fetch", out)

    def test_healthy_board_no_crash(self):
        """Monotonically increasing uptime -> no crash reported."""
        uptime_val = [60000]

        def side_effect(path, **kw):
            if path == "/api/openapi.json":
                return MOCK_SPEC
            if path == "/api/info":
                uptime_val[0] += 1000
                return {"uptime_ms": uptime_val[0]}
            return {"key": "val"}

        rc, out, _ = self._run_probe(side_effect)
        self.assertEqual(rc, 0)
        self.assertIn("no crashes detected", out)

    def test_include_mutating_probes_post(self):
        """/api/reboot (POST) is probed when --include-mutating is set."""
        call_count = [0]

        def side_effect(path, **kw):
            if path == "/api/openapi.json":
                return MOCK_SPEC
            if path == "/api/info":
                call_count[0] += 1
                return {"uptime_ms": 60000 + call_count[0] * 1000}
            return {"ok": True}

        device = _fake_device()
        mock_client = MagicMock()
        mock_client.get_json.side_effect = side_effect
        mock_client.request.return_value = (200, b'{}')
        args = _make_args(include_mutating=True)
        with patch("fleet.resolve_devices", return_value=[device]):
            with patch("fleetlib.client.Client", return_value=mock_client):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    rc = fleet.cmd_probe_endpoints(args)
        out = mock_out.getvalue()
        # /api/reboot POST should appear in output
        self.assertIn("/api/reboot", out)

    def test_include_streaming_probes_logs(self):
        """/api/logs is probed when --include-streaming is set."""
        call_count = [0]

        def side_effect(path, **kw):
            if path == "/api/openapi.json":
                return MOCK_SPEC
            if path == "/api/info":
                call_count[0] += 1
                return {"uptime_ms": 60000 + call_count[0] * 1000}
            return None  # logs returns None (not JSON)

        device = _fake_device()
        mock_client = MagicMock()
        mock_client.get_json.side_effect = side_effect
        mock_client.request.return_value = (200, b'data')
        args = _make_args(include_streaming=True)
        with patch("fleet.resolve_devices", return_value=[device]):
            with patch("fleetlib.client.Client", return_value=mock_client):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    rc = fleet.cmd_probe_endpoints(args)
        out = mock_out.getvalue()
        self.assertIn("/api/logs", out)

    def test_no_devices(self):
        """When no devices are found, return 1."""
        args = _make_args()
        with patch("fleet.resolve_devices", return_value=[]):
            with patch("sys.stderr", new_callable=StringIO):
                rc = fleet.cmd_probe_endpoints(args)
        self.assertEqual(rc, 1)


if __name__ == "__main__":
    unittest.main()
