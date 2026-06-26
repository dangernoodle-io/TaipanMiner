"""Offline tests for ResultSet.push_telemetry (TA-455).

No real broker is touched — paho client is injected via _client_factory.
"""
from __future__ import annotations
import json
import os
import sys
import unittest
from io import StringIO
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device
from fleetlib.results import Result, ResultSet, STATUS_FAIL, STATUS_PASS, STATUS_SKIP


def _dev(ip: str = "192.0.2.1", board: str = "test-board") -> Device:
    return Device(hostname="test-host", ip=ip, port=80, board=board, version="v1.0.0")


def _make_published_factory():
    """Return (factory, published_list) where published_list accumulates publish() calls."""
    published = []

    class FakeClient:
        def __init__(self):
            self.on_connect = None
            self.on_publish = None

        def connect(self, host, port, keepalive=10):
            if self.on_connect:
                self.on_connect(self, None, None, 0)

        def publish(self, topic, payload, qos=1):
            published.append({"topic": topic, "payload": payload})
            info = MagicMock()
            info.mid = 1
            if self.on_publish:
                self.on_publish(self, None, 1)
            return info

        def disconnect(self):
            pass

        def loop_start(self):
            pass

        def loop_stop(self):
            pass

    return FakeClient, published


class TestPushTelemetryPublishes(unittest.TestCase):
    """push_telemetry connects and publishes one message per result with metrics."""

    def setUp(self):
        self.factory, self.published = _make_published_factory()

    def test_publishes_one_message_per_result_with_metrics(self):
        rs = ResultSet("soak")
        rs.add(Result("r1", _dev(), STATUS_PASS, "", {"heap_free_min": 60000}))
        rs.add(Result("r2", _dev("192.0.2.2", "bitaxe-601"), STATUS_PASS, "",
                      {"hashrate_avg": 485.0}))
        rs.push_telemetry("broker.example:1883", _client_factory=self.factory)
        self.assertEqual(len(self.published), 2)

    def test_topic_format(self):
        rs = ResultSet("stress")
        rs.add(Result("r", _dev(board="esp32-wroom32"), STATUS_PASS, "",
                      {"duration_s": 30.0}))
        rs.push_telemetry("broker.example:1883", topic_prefix="fleettest",
                          _client_factory=self.factory)
        self.assertEqual(len(self.published), 1)
        self.assertEqual(self.published[0]["topic"], "fleettest/stress/esp32-wroom32")

    def test_payload_shape(self):
        rs = ResultSet("soak")
        rs.add(Result("r", _dev(board="bitaxe-601"), STATUS_FAIL, "bad",
                      {"heap_free_min": 10000, "reboot_count": 2}))
        rs.push_telemetry("broker.example:1883", _client_factory=self.factory)
        self.assertEqual(len(self.published), 1)
        payload = json.loads(self.published[0]["payload"].decode())
        self.assertEqual(payload["suite"], "soak")
        self.assertEqual(payload["board"], "bitaxe-601")
        self.assertEqual(payload["host"], "192.0.2.1")
        self.assertEqual(payload["status"], "fail")
        self.assertIn("ts", payload)
        self.assertEqual(payload["metrics"]["heap_free_min"], 10000)
        self.assertEqual(payload["metrics"]["reboot_count"], 2)

    def test_results_without_metrics_are_skipped(self):
        rs = ResultSet("functional")
        rs.add(Result("r1", _dev(), STATUS_PASS, ""))           # no metrics
        rs.add(Result("r2", _dev(), STATUS_PASS, "", {}))        # empty metrics
        rs.add(Result("r3", _dev(), STATUS_PASS, "", {"x": 1}))  # has metrics
        rs.push_telemetry("broker.example:1883", _client_factory=self.factory)
        self.assertEqual(len(self.published), 1)

    def test_custom_topic_prefix(self):
        rs = ResultSet("soak")
        rs.add(Result("r", _dev(board="tdongle-s3"), STATUS_PASS, "", {"heap_free_min": 50000}))
        rs.push_telemetry("broker.example:1883", topic_prefix="myprefix",
                          _client_factory=self.factory)
        self.assertEqual(self.published[0]["topic"], "myprefix/soak/tdongle-s3")


class TestPushTelemetryNoPublish(unittest.TestCase):
    """--no-publish-metrics path: no broker call made."""

    def test_empty_broker_url_skips(self):
        factory, published = _make_published_factory()
        rs = ResultSet("soak")
        rs.add(Result("r", _dev(), STATUS_PASS, "", {"heap_free_min": 60000}))
        rs.push_telemetry("", _client_factory=factory)
        self.assertEqual(published, [])

    def test_none_broker_url_skips(self):
        factory, published = _make_published_factory()
        rs = ResultSet("soak")
        rs.add(Result("r", _dev(), STATUS_PASS, "", {"heap_free_min": 60000}))
        rs.push_telemetry(None, _client_factory=factory)
        self.assertEqual(published, [])


class TestPushTelemetryNoBrokerConfigured(unittest.TestCase):
    """When no broker URL is provided, push_telemetry is a no-op; run succeeds."""

    def test_no_publish_no_exception(self):
        rs = ResultSet("soak")
        rs.add(Result("r", _dev(), STATUS_PASS, "", {"heap_free_min": 60000}))
        # No broker: should not raise, no publish
        try:
            rs.push_telemetry(None)
        except Exception as e:
            self.fail(f"push_telemetry raised unexpectedly: {e}")


class TestPushTelemetryExceptionHandling(unittest.TestCase):
    """Publish exception must not propagate — run still succeeds."""

    def test_publish_exception_is_warned_not_raised(self):
        class BombFactory:
            def __init__(self):
                self.on_connect = None
                self.on_publish = None

            def connect(self, *a, **kw):
                raise OSError("network unreachable")

            def loop_start(self): pass
            def loop_stop(self): pass
            def disconnect(self): pass

        rs = ResultSet("soak")
        rs.add(Result("r", _dev(), STATUS_PASS, "", {"heap_free_min": 60000}))
        import logging
        # connect failure → connect_and_publish returns (False, "broker connect failed: …")
        # → results.py logs a WARNING via fleetlib.results logger
        with self.assertLogs("fleetlib.results", level="WARNING") as cm:
            try:
                rs.push_telemetry("broker.example:1883", _client_factory=BombFactory)
            except Exception as e:
                self.fail(f"exception must not propagate: {e}")
        self.assertTrue(any("connect" in m.lower() or "publish" in m.lower()
                            for m in cm.output))

    def test_paho_missing_logs_warning(self):
        """When paho-mqtt is not importable, push_telemetry warns and does not raise."""
        rs = ResultSet("soak")
        rs.add(Result("r", _dev(), STATUS_PASS, "", {"heap_free_min": 60000}))
        import fleetlib.mqtt as mqtt_mod
        orig = mqtt_mod._import_paho
        try:
            mqtt_mod._import_paho = lambda: (None, "paho-mqtt not installed (test stub)")
            import logging
            with self.assertLogs("fleetlib.results", level="WARNING"):
                rs.push_telemetry("broker.example:1883")
        finally:
            mqtt_mod._import_paho = orig


class TestPushTelemetryRename(unittest.TestCase):
    """Verify push_influxdb is gone and push_telemetry exists."""

    def test_push_telemetry_exists(self):
        rs = ResultSet("t")
        self.assertTrue(hasattr(rs, "push_telemetry"))
        self.assertTrue(callable(rs.push_telemetry))

    def test_push_influxdb_removed(self):
        rs = ResultSet("t")
        self.assertFalse(hasattr(rs, "push_influxdb"),
                         "push_influxdb must be removed — use push_telemetry")


class TestFleetCmdSuiteMetricsFlag(unittest.TestCase):
    """fleet.py cmd_suite: --no-publish-metrics suppresses publish; no broker → skip note."""

    def _run_cmd_suite(self, extra_args=None, env=None):
        """Build a minimal args namespace and call cmd_suite, capturing stderr."""
        import argparse
        import io
        sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
        import fleet as fleet_mod
        from fleetlib.criteria import Criteria
        from fleetlib.profiles import Profiles
        from fleetlib.results import ResultSet
        from fleetlib.safety import Guard
        from suites import SuiteContext, SettleConfig

        # Build a fake args namespace
        args = argparse.Namespace(
            log_level="WARNING",
            hosts="192.0.2.99",
            discover_timeout=1,
            board=None,
            fields=None,
            gates=[],
            skip_gates=[],
            out_json=None,
            out_junit=None,
            baseline=None,
            criteria=None,
            settle=None,
            dry_run=False,
            yes=True,
            subcommand="soak",
            func=None,
            metrics_mqtt_url=None,
            metrics_topic="fleettest",
            no_publish_metrics=(extra_args or {}).get("no_publish_metrics", False),
        )
        for k, v in (extra_args or {}).items():
            setattr(args, k, v)

        published = []

        # Patch resolve_devices, load_suite, and push_telemetry
        mock_device = _dev(ip="192.0.2.99", board="esp32-wroom32")

        class FakeModule:
            NAME = "soak"
            HELP = "fake soak"

            @staticmethod
            def add_arguments(p):
                pass

            @staticmethod
            def run(ctx):
                ctx.results.add(Result("r", mock_device, STATUS_PASS, "",
                                       {"heap_free_min": 60000}))
                return ctx.results

        with patch("fleet.resolve_devices", return_value=[mock_device]), \
             patch("fleet.load_suite", return_value=FakeModule) if False else \
             patch("suites.load_suite", return_value=FakeModule), \
             patch.dict(os.environ, env or {}, clear=False):
            captured_stderr = io.StringIO()
            old_stderr = sys.stderr
            sys.stderr = captured_stderr
            try:
                from suites import SUITES
                orig_suites = dict(SUITES)
                SUITES["soak"] = "suites.soak"
                try:
                    rc = fleet_mod.cmd_suite(args, "soak")
                finally:
                    SUITES.clear()
                    SUITES.update(orig_suites)
            finally:
                sys.stderr = old_stderr

        return rc, captured_stderr.getvalue(), published

    def test_no_broker_prints_skip_note(self):
        # No broker configured → skip note on stderr
        rc, stderr, _ = self._run_cmd_suite(
            extra_args={"no_publish_metrics": False, "metrics_mqtt_url": None},
            env={"BB_TEST_METRICS_BROKER": "", "BB_TEST_RECEIVER": ""},
        )
        self.assertEqual(rc, 0)
        self.assertIn("not published", stderr)

    def test_no_publish_metrics_flag_suppresses_note(self):
        # --no-publish-metrics: no note, no publish
        rc, stderr, _ = self._run_cmd_suite(
            extra_args={"no_publish_metrics": True, "metrics_mqtt_url": None},
            env={"BB_TEST_METRICS_BROKER": "", "BB_TEST_RECEIVER": ""},
        )
        self.assertEqual(rc, 0)
        self.assertNotIn("not published", stderr)


if __name__ == "__main__":
    unittest.main()
