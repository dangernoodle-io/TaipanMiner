"""Offline unit tests for the telemetry transport suite — config + no-false-sinks logic.

No device, broker, or InfluxDB is touched: Client/guard/docker are all mocked.
"""
import os
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.criteria import Criteria
from fleetlib.discovery import Device
from fleetlib.readiness import Readiness
from fleetlib.results import ResultSet
from fleetlib.safety import Guard
from suites import SuiteContext

import suites.telemetry as matrix


# --------------------------------------------------------------------------- helpers

def _device(board="esp32-wroom32", ip="192.0.2.20"):
    return Device(hostname="host", ip=ip, port=80, board=board, version="v0.69.0")


class FakeSettle:
    settle_delay = 0
    enabled = True

    def wait_ready(self, device, criteria=None):
        return Readiness(ready=True, elapsed_s=0.0, reason="ready")


def _ctx(devices, guard=None, gates=None, extra=None):
    return SuiteContext(
        devices=devices, criteria=Criteria(settle_delay=0), guard=guard or _ok_guard(),
        results=ResultSet("telemetry"), fields=None, gates=gates or set(),
        settle=FakeSettle(), out_json=None, out_junit=None, baseline=None,
        extra=extra or {},
    )


def _ok_guard(board="esp32-wroom32"):
    return Guard(dry_run=False, confirm=True, expect_board=board)


def _mk_client(telem):
    c = MagicMock()

    def gj(path, timeout=5):
        if path == "/api/telemetry":
            return telem
        return None

    c.get_json = gj
    c.request = MagicMock(return_value=(200, b""))
    return c


_HEALTHY_MQTT = {
    "mqtt": {"enabled": True, "connected": True},
    "http": {"enabled": False},
    "publisher": {"last_publish_ok": True, "last_publish_age_ms": 1200, "sink_count": 1},
}


# --------------------------------------------------------------------------- build_config

class TestBuildConfig(unittest.TestCase):
    def test_mqtt_plain(self):
        dis, en = matrix.build_config("mqtt_plain", "rx.example", {}, 1883)
        self.assertEqual(dis, {"http": {"enabled": False}})
        self.assertEqual(en["mqtt"]["uri"], "mqtt://rx.example:1883")
        self.assertFalse(en["mqtt"]["tls"])

    def test_mqtt_mtls_carries_certs(self):
        certs = {"ca": "CA", "cert": "CRT", "key": "KEY"}
        _dis, en = matrix.build_config("mqtt_mtls", "rx.example", certs, 8884)
        m = en["mqtt"]
        self.assertTrue(m["tls"])
        self.assertEqual(m["tls_ca"], "CA")
        self.assertEqual(m["tls_cert"], "CRT")
        self.assertEqual(m["tls_key"], "KEY")
        self.assertTrue(m["uri"].startswith("mqtts://"))

    def test_http_tls(self):
        dis, en = matrix.build_config("http_tls", "rx.example", {"ca": "CA"}, 9881)
        self.assertEqual(dis, {"mqtt": {"enabled": False}})
        self.assertEqual(en["http"]["base"], "https://rx.example:9881")
        self.assertEqual(en["http"]["tls_ca"], "CA")


# --------------------------------------------------------------------------- evaluate_row (no-false-sinks)

class TestEvaluateRow(unittest.TestCase):
    def test_mqtt_connected_and_ok_passes(self):
        ok, _ = matrix.evaluate_row("mqtt_plain", _HEALTHY_MQTT)
        self.assertTrue(ok)

    def test_mqtt_publish_not_ok_fails(self):
        t = {"mqtt": {"enabled": True, "connected": True}, "http": {"enabled": False},
             "publisher": {"last_publish_ok": False}}
        ok, detail = matrix.evaluate_row("mqtt_plain", t)
        self.assertFalse(ok)
        self.assertIn("last_publish_ok", detail)

    def test_mqtt_not_connected_fails(self):
        t = {"mqtt": {"enabled": True, "connected": False}, "http": {"enabled": False},
             "publisher": {"last_publish_ok": True}}
        ok, detail = matrix.evaluate_row("mqtt_plain", t)
        self.assertFalse(ok)
        self.assertIn("not connected", detail)

    def test_mqtt_row_other_sink_enabled_is_false_sink(self):
        t = {"mqtt": {"enabled": True, "connected": True}, "http": {"enabled": True},
             "publisher": {"last_publish_ok": True}}
        ok, detail = matrix.evaluate_row("mqtt_plain", t)
        self.assertFalse(ok)
        self.assertIn("false-sink", detail)

    def test_http_fresh_passes(self):
        t = {"mqtt": {"enabled": False}, "http": {"enabled": True},
             "publisher": {"last_publish_ok": True, "last_publish_age_ms": 1000}}
        ok, _ = matrix.evaluate_row("http_plain", t)
        self.assertTrue(ok)

    def test_http_stale_fails(self):
        t = {"mqtt": {"enabled": False}, "http": {"enabled": True},
             "publisher": {"last_publish_ok": True, "last_publish_age_ms": 30_000}}
        ok, detail = matrix.evaluate_row("http_plain", t)
        self.assertFalse(ok)
        self.assertIn("stale", detail)

    def test_http_row_mqtt_enabled_is_false_sink(self):
        t = {"mqtt": {"enabled": True}, "http": {"enabled": True},
             "publisher": {"last_publish_ok": True}}
        ok, detail = matrix.evaluate_row("http_plain", t)
        self.assertFalse(ok)
        self.assertIn("false-sink", detail)

    def test_none_telemetry_fails(self):
        ok, _ = matrix.evaluate_row("mqtt_plain", None)
        self.assertFalse(ok)


# --------------------------------------------------------------------------- selected_rows

class TestSelectedRows(unittest.TestCase):
    def test_default_all(self):
        ctx = _ctx([_device()], extra={})
        self.assertEqual(matrix.selected_rows(ctx), matrix.ROWS)

    def test_subset_string(self):
        ctx = _ctx([_device()], extra={"rows": "mqtt_plain, http_tls, bogus"})
        self.assertEqual(matrix.selected_rows(ctx), ["mqtt_plain", "http_tls"])


# --------------------------------------------------------------------------- _run_device

class TestRunDevice(unittest.TestCase):
    def test_no_receiver_skips(self):
        dev = _device()
        ctx = _ctx([dev], extra={"rows": "mqtt_plain"})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=_mk_client(None)):
            matrix.run(ctx)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "skip")
        self.assertIn("receiver", r.detail)

    def test_certs_missing_skips(self):
        dev = _device()
        ctx = _ctx([dev], extra={"rows": "mqtt_mtls", "receiver": "rx.example"})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=_mk_client(None)):
            matrix.run(ctx)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "skip")
        self.assertIn("cert", r.detail.lower())

    def test_dry_run_skips_patch(self):
        dev = _device()
        client = _mk_client(_HEALTHY_MQTT)
        guard = Guard(dry_run=True, confirm=True, expect_board=dev.board)
        ctx = _ctx([dev], guard=guard, gates={"mqtt_plain"},
                   extra={"rows": "mqtt_plain", "receiver": "rx.example"})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=client), \
             patch("fleetlib.discovery.verify_identity", return_value=True):
            matrix.run(ctx)
        self.assertEqual(ctx.results.results[0].status, "skip")
        client.request.assert_not_called()

    def test_row_pass(self):
        dev = _device()
        client = _mk_client(_HEALTHY_MQTT)
        ctx = _ctx([dev], guard=_ok_guard(dev.board), gates={"mqtt_plain"},
                   extra={"rows": "mqtt_plain", "receiver": "rx.example"})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=client), \
             patch("fleetlib.discovery.verify_identity", return_value=True):
            matrix.run(ctx)
        self.assertEqual(ctx.results.results[0].status, "pass",
                         ctx.results.results[0].detail)
        self.assertEqual(client.request.call_count, 2)  # disable + enable PATCH

    def test_row_gated_out_skips(self):
        dev = _device()
        client = _mk_client(_HEALTHY_MQTT)
        ctx = _ctx([dev], gates={"http_plain"},
                   extra={"rows": "mqtt_plain", "receiver": "rx.example"})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=client):
            matrix.run(ctx)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "skip")
        self.assertIn("gated", r.detail)

    def test_influx_unavailable_note_does_not_fail(self):
        dev = _device()
        client = _mk_client(_HEALTHY_MQTT)
        docker = MagicMock(return_value=(1, "no influx"))
        # gates empty -> influx gate enabled; docker unavailable -> note only
        ctx = _ctx([dev], guard=_ok_guard(dev.board), gates=set(),
                   extra={"rows": "mqtt_plain", "receiver": "rx.example",
                          "docker_runner": docker})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=client), \
             patch("fleetlib.discovery.verify_identity", return_value=True):
            matrix.run(ctx)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "pass")
        self.assertIn("influx check skipped", r.detail)


if __name__ == "__main__":
    unittest.main()
