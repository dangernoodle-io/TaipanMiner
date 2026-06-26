"""Offline unit tests for the telemetry transport suite — config + no-false-sinks logic.

No device or broker is touched: Client/guard/paho are all mocked.
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


def _make_paho_factory(deliver_message=True, connect_raises=None):
    """Return a factory that builds a mock paho client.

    If deliver_message=True, calling loop_start() triggers on_message with a fake
    message (simulating broker receipt). If connect_raises is set, connect() raises it.
    """
    import time

    class FakeMsg:
        topic = "metrics/host/health"
        payload = b'{"hostname":"host"}'

    class FakeClient:
        def __init__(self):
            self.on_connect = None
            self.on_message = None
            self._stopped = False

        def connect(self, host, port, keepalive=30):
            if connect_raises:
                raise connect_raises
            # simulate successful connect + subscribe
            if self.on_connect:
                self.on_connect(self, None, None, 0)

        def subscribe(self, topic, qos=0):
            pass

        def disconnect(self):
            pass

        def loop_start(self):
            if deliver_message and self.on_message:
                # deliver immediately in test context
                self.on_message(self, None, FakeMsg())

        def loop_stop(self):
            self._stopped = True

    return FakeClient


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
    def test_default_is_mqtt_plain(self):
        ctx = _ctx([_device()], extra={})
        self.assertEqual(matrix.selected_rows(ctx), ["mqtt_plain"])

    def test_subset_string(self):
        ctx = _ctx([_device()], extra={"rows": "mqtt_plain, http_tls, bogus"})
        self.assertEqual(matrix.selected_rows(ctx), ["mqtt_plain", "http_tls"])

    def test_all_rows_via_explicit_rows(self):
        ctx = _ctx([_device()], extra={"rows": ",".join(matrix.ROWS)})
        self.assertEqual(matrix.selected_rows(ctx), matrix.ROWS)


# --------------------------------------------------------------------------- _mqtt_broker_verify

class TestMqttBrokerVerify(unittest.TestCase):
    def test_message_arrives_passes(self):
        factory = _make_paho_factory(deliver_message=True)
        ok, detail = matrix._mqtt_broker_verify(
            host="broker.example",
            port=1883,
            hostname="host",
            certs={},
            row="mqtt_plain",
            timeout=5,
            topic_prefix="metrics",
            _paho_client_factory=factory,
        )
        self.assertTrue(ok)
        self.assertIn("broker receipt confirmed", detail)

    def test_timeout_fails(self):
        factory = _make_paho_factory(deliver_message=False)
        ok, detail = matrix._mqtt_broker_verify(
            host="broker.example",
            port=1883,
            hostname="host",
            certs={},
            row="mqtt_plain",
            timeout=0,  # immediate timeout
            topic_prefix="metrics",
            _paho_client_factory=factory,
        )
        self.assertFalse(ok)
        self.assertIn("timeout", detail)

    def test_connect_failure_fails(self):
        factory = _make_paho_factory(connect_raises=ConnectionRefusedError("refused"))
        ok, detail = matrix._mqtt_broker_verify(
            host="broker.example",
            port=1883,
            hostname="host",
            certs={},
            row="mqtt_plain",
            timeout=5,
            topic_prefix="metrics",
            _paho_client_factory=factory,
        )
        self.assertFalse(ok)
        self.assertIn("connect failed", detail)

    def test_paho_missing_returns_skip_note(self):
        import builtins
        real_import = builtins.__import__

        def mock_import(name, *args, **kwargs):
            if name == "paho.mqtt.client":
                raise ImportError("no module named paho")
            return real_import(name, *args, **kwargs)

        with patch("builtins.__import__", side_effect=mock_import):
            ok, detail = matrix._mqtt_broker_verify(
                host="broker.example",
                port=1883,
                hostname="host",
                certs={},
                row="mqtt_plain",
                timeout=5,
                topic_prefix="metrics",
            )
        self.assertFalse(ok)
        self.assertIn("paho-mqtt not installed", detail)

    def test_mtls_row_uses_tls(self):
        """mtls row: verify TLS context is built with ca + cert/key wired correctly."""
        certs = {"ca": "FAKE_CA", "cert": "FAKE_CERT", "key": "FAKE_KEY"}

        # Build a factory whose FakeClient also has tls_set_context
        class FakeClientTLS(_make_paho_factory(deliver_message=True)):
            def __init__(self):
                super().__init__()
                self.tls_ctx_set = None

            def tls_set_context(self, ctx):
                self.tls_ctx_set = ctx

        # patch ssl to avoid real cert parsing; capture the SSLContext calls
        with patch("suites.telemetry.ssl") as mock_ssl:
            mock_ctx = MagicMock()
            mock_ssl.SSLContext.return_value = mock_ctx
            mock_ssl.PROTOCOL_TLS_CLIENT = 0
            mock_ssl.CERT_NONE = 0
            mock_ssl.CERT_REQUIRED = 2
            ok, detail = matrix._mqtt_broker_verify(
                host="broker.example",
                port=8884,
                hostname="host",
                certs=certs,
                row="mqtt_mtls",
                timeout=5,
                topic_prefix="metrics",
                _paho_client_factory=FakeClientTLS,
            )
        # ssl.SSLContext was created, ca and cert/key were loaded
        mock_ctx.load_verify_locations.assert_called_once()
        mock_ctx.load_cert_chain.assert_called_once()


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

    def test_row_pass_with_broker_receipt(self):
        dev = _device()
        client = _mk_client(_HEALTHY_MQTT)
        factory = _make_paho_factory(deliver_message=True)
        ctx = _ctx([dev], guard=_ok_guard(dev.board), gates={"mqtt_plain"},
                   extra={"rows": "mqtt_plain", "receiver": "rx.example",
                          "_paho_client_factory": factory})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=client), \
             patch("fleetlib.discovery.verify_identity", return_value=True):
            matrix.run(ctx)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "pass", r.detail)
        self.assertIn("broker receipt confirmed", r.detail)
        self.assertEqual(client.request.call_count, 2)  # disable + enable PATCH

    def test_row_pass_paho_missing_note(self):
        """Row passes even when paho missing; broker check note appended."""
        dev = _device()
        client = _mk_client(_HEALTHY_MQTT)
        ctx = _ctx([dev], guard=_ok_guard(dev.board), gates={"mqtt_plain"},
                   extra={"rows": "mqtt_plain", "receiver": "rx.example"})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=client), \
             patch("fleetlib.discovery.verify_identity", return_value=True), \
             patch("suites.telemetry._mqtt_broker_verify",
                   return_value=(False, "paho-mqtt not installed; broker-subscribe check skipped")):
            matrix.run(ctx)
        r = ctx.results.results[0]
        self.assertEqual(r.status, "pass", r.detail)
        self.assertIn("paho-mqtt not installed", r.detail)

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

    def test_http_row_no_broker_verify(self):
        """HTTP rows pass on device-side signal only; broker verify not called."""
        dev = _device()
        telem_http = {
            "mqtt": {"enabled": False},
            "http": {"enabled": True},
            "publisher": {"last_publish_ok": True, "last_publish_age_ms": 500, "sink_count": 1},
        }
        client = _mk_client(telem_http)
        ctx = _ctx([dev], guard=_ok_guard(dev.board), gates={"http_plain"},
                   extra={"rows": "http_plain", "receiver": "rx.example"})
        with patch.dict(os.environ, {}, clear=True), \
             patch("suites.telemetry.Client", return_value=client), \
             patch("fleetlib.discovery.verify_identity", return_value=True), \
             patch("suites.telemetry._mqtt_broker_verify") as mock_broker:
            matrix.run(ctx)
        mock_broker.assert_not_called()
        r = ctx.results.results[0]
        self.assertEqual(r.status, "pass", r.detail)

    def test_no_docker_exec_influx(self):
        """Confirm no reference to docker or influx in the run path."""
        import inspect
        source = inspect.getsource(matrix._run_device)
        self.assertNotIn("docker", source.lower())
        self.assertNotIn("influx", source.lower())


if __name__ == "__main__":
    unittest.main()
