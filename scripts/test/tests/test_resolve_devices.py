"""Offline tests for TA-459: resolve_devices / from_hosts_detailed failure messaging.

Cases:
  (a) mDNS-empty   -> 'No devices found via mDNS' message, NOT 'No devices found.'
  (b) --hosts all-fail -> 'N host(s) specified; none reachable' + per-host reasons,
                          NOT 'No devices found.'
  (c) --hosts partial  -> proceeds with resolved device + warns about failure
  (d) reason classification: timeout vs refused vs no_route
"""
from __future__ import annotations
import io
import os
import socket
import sys
import unittest
import urllib.error
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import (
    Device,
    EnrichFailure,
    ResolveResult,
    _classify_enrich_exception,
    from_hosts_detailed,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_device(ip="192.0.2.10", board="esp32-wroom32") -> Device:
    return Device(hostname=board, ip=ip, port=80, board=board, version="v0.99.0")


def _good_info(ip="192.0.2.10"):
    return {"hostname": "miner-a", "board": "esp32-wroom32", "version": "v0.99.0"}


def _timeout_exc():
    return socket.timeout("timed out")


def _refused_exc():
    inner = OSError(111, "Connection refused")
    inner.errno = 111
    err = urllib.error.URLError(inner)
    return err


def _no_route_exc():
    inner = OSError(113, "No route to host")
    err = urllib.error.URLError(inner)
    return err


def _http_error_exc(code=503):
    return urllib.error.HTTPError(
        url="http://x", code=code, msg="Service Unavailable", hdrs=None, fp=None
    )


# ---------------------------------------------------------------------------
# (a) mDNS-empty -> correct message
# ---------------------------------------------------------------------------

class TestMdnsEmpty(unittest.TestCase):
    """fleet.py cmd_discover prints the mDNS-specific message when mDNS returns nothing."""

    def _run_discover(self, mdns_devices):
        import fleet
        args = MagicMock()
        args.hosts = None
        args.board = None
        args.discover_timeout = 10
        result = ResolveResult(devices=mdns_devices, failures=[], from_mdns=True)
        stdout_buf = io.StringIO()
        stderr_buf = io.StringIO()
        with patch("fleet.resolve_devices", return_value=result):
            with patch("sys.stdout", stdout_buf):
                with patch("sys.stderr", stderr_buf):
                    code = fleet.cmd_discover(args)
        return code, stdout_buf.getvalue(), stderr_buf.getvalue()

    def test_mdns_empty_prints_mdns_message(self):
        code, out, err = self._run_discover([])
        self.assertEqual(code, 0)
        self.assertIn("mDNS", out)
        self.assertNotIn("No devices found.", out)
        self.assertNotIn("No devices found.", err)

    def test_mdns_empty_message_mentions_service_type(self):
        _, out, _ = self._run_discover([])
        self.assertIn("_taipanminer._tcp.local.", out)

    def test_mdns_with_devices_prints_table(self):
        dev = _make_device()
        with patch("fleetlib.client.Client") as MockClient:
            mc = MagicMock()
            mc.get_json.return_value = {"uptime_ms": 5000}
            MockClient.return_value = mc
            code, out, _ = self._run_discover([dev])
        self.assertEqual(code, 0)
        self.assertIn(dev.ip, out)


# ---------------------------------------------------------------------------
# (b) --hosts all-fail -> per-host message, NOT 'No devices found.'
# ---------------------------------------------------------------------------

class TestHostsAllFail(unittest.TestCase):
    """When every --hosts entry fails enrichment, print per-host reasons."""

    def _run_status(self, result):
        import fleet
        args = MagicMock()
        args.hosts = ",".join(f.host for f in result.failures)
        args.board = None
        args.discover_timeout = 10
        stderr_buf = io.StringIO()
        stdout_buf = io.StringIO()
        with patch("fleet.resolve_devices", return_value=result):
            with patch("sys.stdout", stdout_buf):
                with patch("sys.stderr", stderr_buf):
                    code = fleet.cmd_status(args)
        return code, stdout_buf.getvalue(), stderr_buf.getvalue()

    def test_all_fail_not_no_devices_found(self):
        result = ResolveResult(
            devices=[],
            failures=[
                EnrichFailure("192.0.2.250", "timeout after 5s", "timeout"),
                EnrichFailure("192.0.2.251", "connection refused", "refused"),
            ],
            from_mdns=False,
        )
        code, out, err = self._run_status(result)
        self.assertEqual(code, 1)
        combined = out + err
        self.assertNotIn("No devices found.", combined)

    def test_all_fail_shows_count_and_hosts(self):
        result = ResolveResult(
            devices=[],
            failures=[
                EnrichFailure("192.0.2.250", "timeout after 5s", "timeout"),
                EnrichFailure("192.0.2.251", "connection refused", "refused"),
            ],
            from_mdns=False,
        )
        code, out, err = self._run_status(result)
        combined = out + err
        self.assertIn("2 host(s) specified", combined)
        self.assertIn("192.0.2.250", combined)
        self.assertIn("192.0.2.251", combined)

    def test_all_fail_shows_per_host_reasons(self):
        result = ResolveResult(
            devices=[],
            failures=[
                EnrichFailure("192.0.2.250", "timeout after 5s", "timeout"),
                EnrichFailure("192.0.2.251", "connection refused", "refused"),
            ],
            from_mdns=False,
        )
        _, out, err = self._run_status(result)
        combined = out + err
        self.assertIn("timeout after 5s", combined)
        self.assertIn("connection refused", combined)

    def test_single_host_fail(self):
        result = ResolveResult(
            devices=[],
            failures=[EnrichFailure("192.0.2.250", "timeout after 5s", "timeout")],
            from_mdns=False,
        )
        _, out, err = self._run_status(result)
        combined = out + err
        self.assertNotIn("No devices found.", combined)
        self.assertIn("192.0.2.250", combined)


# ---------------------------------------------------------------------------
# (c) --hosts partial: one resolves, one fails -> proceeds + warns
# ---------------------------------------------------------------------------

class TestHostsPartial(unittest.TestCase):
    """Partial success: resolved devices are used; failures appear as warnings."""

    def _run_status(self, result):
        import fleet
        args = MagicMock()
        args.hosts = "192.0.2.10,192.0.2.250"
        args.board = None
        args.discover_timeout = 10
        stderr_buf = io.StringIO()
        stdout_buf = io.StringIO()

        def _fake_client(ip, port=80):
            mc = MagicMock()
            mc.get_json.return_value = {
                "uptime_ms": 5000, "board": "esp32-wroom32",
                "version": "v0.99.0", "free_heap": 75000,
            }
            return mc

        with patch("fleet.resolve_devices", return_value=result):
            with patch("fleetlib.client.Client", side_effect=_fake_client):
                with patch("sys.stdout", stdout_buf):
                    with patch("sys.stderr", stderr_buf):
                        code = fleet.cmd_status(args)
        return code, stdout_buf.getvalue(), stderr_buf.getvalue()

    def test_partial_proceeds_with_good_device(self):
        result = ResolveResult(
            devices=[_make_device("192.0.2.10")],
            failures=[EnrichFailure("192.0.2.250", "timeout after 5s", "timeout")],
            from_mdns=False,
        )
        code, out, err = self._run_status(result)
        # Good device in table
        self.assertIn("192.0.2.10", out)

    def test_partial_warns_about_failed_host_on_stderr(self):
        result = ResolveResult(
            devices=[_make_device("192.0.2.10")],
            failures=[EnrichFailure("192.0.2.250", "timeout after 5s", "timeout")],
            from_mdns=False,
        )
        _, out, err = self._run_status(result)
        # Failed host reported on stderr
        self.assertIn("192.0.2.250", err)
        self.assertIn("timeout after 5s", err)

    def test_partial_does_not_say_no_devices_found(self):
        result = ResolveResult(
            devices=[_make_device("192.0.2.10")],
            failures=[EnrichFailure("192.0.2.250", "timeout after 5s", "timeout")],
            from_mdns=False,
        )
        _, out, err = self._run_status(result)
        self.assertNotIn("No devices found.", out)
        self.assertNotIn("No devices found.", err)


# ---------------------------------------------------------------------------
# (d) Reason classification via _classify_enrich_exception
# ---------------------------------------------------------------------------

class TestReasonClassification(unittest.TestCase):
    """_classify_enrich_exception maps exception types to (category, reason)."""

    def test_socket_timeout_is_timeout(self):
        cat, reason = _classify_enrich_exception(socket.timeout("timed out"), 5)
        self.assertEqual(cat, "timeout")
        self.assertIn("5s", reason)

    def test_urllib_urlerror_wrapping_socket_timeout(self):
        exc = urllib.error.URLError(socket.timeout("timed out"))
        cat, reason = _classify_enrich_exception(exc, 8)
        self.assertEqual(cat, "timeout")
        self.assertIn("8s", reason)

    def test_urllib_urlerror_connection_refused(self):
        inner = OSError(111, "Connection refused")
        inner.errno = 111
        exc = urllib.error.URLError(inner)
        cat, reason = _classify_enrich_exception(exc, 5)
        self.assertEqual(cat, "refused")
        self.assertIn("refused", reason)

    def test_urllib_urlerror_no_route(self):
        inner = OSError(113, "No route to host")
        exc = urllib.error.URLError(inner)
        cat, reason = _classify_enrich_exception(exc, 5)
        self.assertEqual(cat, "no_route")

    def test_http_error_is_http_error(self):
        exc = urllib.error.HTTPError(
            url="http://x", code=503, msg="Service Unavailable", hdrs=None, fp=None
        )
        cat, reason = _classify_enrich_exception(exc, 5)
        self.assertEqual(cat, "http_error")
        self.assertIn("503", reason)

    def test_connection_refused_error_is_refused(self):
        exc = ConnectionRefusedError(111, "Connection refused")
        cat, reason = _classify_enrich_exception(exc, 5)
        self.assertEqual(cat, "refused")

    def test_timeout_error_is_timeout(self):
        exc = TimeoutError("timed out")
        cat, reason = _classify_enrich_exception(exc, 10)
        self.assertEqual(cat, "timeout")
        self.assertIn("10s", reason)

    def test_urlerror_timeout_string_in_reason(self):
        inner = OSError("timed out")
        exc = urllib.error.URLError(inner)
        cat, reason = _classify_enrich_exception(exc, 5)
        self.assertEqual(cat, "timeout")


# ---------------------------------------------------------------------------
# from_hosts_detailed integration (mock urllib.request.urlopen)
# ---------------------------------------------------------------------------

class TestFromHostsDetailed(unittest.TestCase):
    """from_hosts_detailed returns structured ResolveResult with per-host failures."""

    def _mock_urlopen_side_effect(self, responses: dict):
        """Build a side_effect that maps URL prefixes to responses or exceptions."""
        import json

        def _side_effect(url, timeout=5):
            for prefix, val in responses.items():
                if prefix in url:
                    if isinstance(val, Exception):
                        raise val
                    cm = MagicMock()
                    cm.__enter__ = lambda s: s
                    cm.__exit__ = MagicMock(return_value=False)
                    cm.read.return_value = json.dumps(val).encode()
                    return cm
            raise socket.timeout("timed out")

        return _side_effect

    def test_all_succeed(self):
        info = {"hostname": "miner-a", "board": "esp32-wroom32", "version": "v0.1.0"}
        se = self._mock_urlopen_side_effect({"192.0.2.10": info, "192.0.2.11": info})
        with patch("urllib.request.urlopen", side_effect=se):
            r = from_hosts_detailed(["192.0.2.10", "192.0.2.11"])
        self.assertEqual(len(r.devices), 2)
        self.assertEqual(len(r.failures), 0)
        self.assertFalse(r.from_mdns)

    def test_all_fail_timeout(self):
        se = self._mock_urlopen_side_effect({
            "192.0.2.250": socket.timeout("timed out"),
            "192.0.2.251": socket.timeout("timed out"),
        })
        with patch("urllib.request.urlopen", side_effect=se):
            r = from_hosts_detailed(["192.0.2.250", "192.0.2.251"])
        self.assertEqual(len(r.devices), 0)
        self.assertEqual(len(r.failures), 2)
        for f in r.failures:
            self.assertEqual(f.category, "timeout")
            self.assertIn("timeout", f.reason)

    def test_partial_one_succeeds_one_fails(self):
        info = {"hostname": "miner-a", "board": "esp32-wroom32", "version": "v0.1.0"}
        se = self._mock_urlopen_side_effect({
            "192.0.2.10": info,
            "192.0.2.250": socket.timeout("timed out"),
        })
        with patch("urllib.request.urlopen", side_effect=se):
            r = from_hosts_detailed(["192.0.2.10", "192.0.2.250"])
        self.assertEqual(len(r.devices), 1)
        self.assertEqual(r.devices[0].ip, "192.0.2.10")
        self.assertEqual(len(r.failures), 1)
        self.assertEqual(r.failures[0].host, "192.0.2.250")
        self.assertEqual(r.failures[0].category, "timeout")

    def test_refused_failure_category(self):
        inner = OSError(111, "Connection refused")
        inner.errno = 111
        exc = urllib.error.URLError(inner)
        se = self._mock_urlopen_side_effect({"192.0.2.250": exc})
        with patch("urllib.request.urlopen", side_effect=se):
            r = from_hosts_detailed(["192.0.2.250"])
        self.assertEqual(r.failures[0].category, "refused")

    def test_resolve_result_not_from_mdns(self):
        se = self._mock_urlopen_side_effect({"192.0.2.250": socket.timeout("x")})
        with patch("urllib.request.urlopen", side_effect=se):
            r = from_hosts_detailed(["192.0.2.250"])
        self.assertFalse(r.from_mdns)


if __name__ == "__main__":
    unittest.main()
