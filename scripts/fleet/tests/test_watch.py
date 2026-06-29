"""Offline unit tests for fleet.py cmd_watch."""
import csv
import io
import json
import os
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device, EnrichFailure, ResolveResult


def _make_device(ip="192.0.2.10", board="esp32-wroom32") -> Device:
    return Device(hostname=board, ip=ip, port=80, board=board, version="v0.99.0")


def _base_args(**kwargs):
    """Build a minimal args namespace for cmd_watch."""
    defaults = dict(
        watch_path="/api/diag/heap",
        fields=None,
        interval=0.0,
        duration=None,
        count=None,
        on_change=False,
        until=None,
        any_device=False,
        alert=None,
        out_json=None,
        out_csv=None,
        hosts=None,
        board=None,
        discover_timeout=10,
        log_level="WARNING",
    )
    defaults.update(kwargs)
    return SimpleNamespace(**defaults)


def _run_watch(devices, args, responses_map):
    """Call cmd_watch with mocked resolve_devices and Client.

    responses_map: {ip: [resp_tick0, resp_tick1, ...]}
    Each response is a dict returned by client.get_json().
    Responses cycle — last value repeated when ticks exceed list length.
    """
    import commands.watch as watch_cmd

    call_counts = {ip: 0 for ip in responses_map}

    def _make_client(ip, port=80):
        c = MagicMock()
        resps = responses_map.get(ip, [{}])

        def _get_json(path, timeout=5):
            idx = min(call_counts[ip], len(resps) - 1)
            call_counts[ip] += 1
            return resps[idx]

        c.get_json = _get_json
        c.spec = None
        return c

    stdout_buf = io.StringIO()
    stderr_buf = io.StringIO()

    with patch("commands.watch.resolve_devices", return_value=devices):
        with patch("fleetlib.client.Client", side_effect=_make_client):
            with patch("time.sleep", return_value=None):
                with patch("sys.stdout", stdout_buf):
                    with patch("sys.stderr", stderr_buf):
                        code = watch_cmd.run(args)

    return code, stdout_buf.getvalue(), stderr_buf.getvalue()


class TestFieldExtractionRow(unittest.TestCase):
    def test_field_in_output(self):
        device = _make_device()
        args = _base_args(
            fields="internal.free",
            count=1,
        )
        code, out, _ = _run_watch(
            [device],
            args,
            {device.ip: [{"internal": {"free": 12345}}]},
        )
        self.assertEqual(code, 0)
        self.assertIn("12345", out)
        self.assertIn("internal.free", out)


class TestOnChangeSuppression(unittest.TestCase):
    def test_same_value_suppressed(self):
        device = _make_device()
        args = _base_args(fields="hashrate", count=2, on_change=True)
        code, out, _ = _run_watch(
            [device],
            args,
            {device.ip: [{"hashrate": 100}, {"hashrate": 100}]},
        )
        self.assertEqual(code, 0)
        rows = [l for l in out.strip().splitlines() if l.strip()]
        self.assertEqual(len(rows), 1, f"expected 1 row, got {len(rows)}: {out!r}")

    def test_changed_value_emitted(self):
        device = _make_device()
        args = _base_args(fields="hashrate", count=2, on_change=True)
        code, out, _ = _run_watch(
            [device],
            args,
            {device.ip: [{"hashrate": 100}, {"hashrate": 200}]},
        )
        self.assertEqual(code, 0)
        rows = [l for l in out.strip().splitlines() if l.strip()]
        self.assertEqual(len(rows), 2, f"expected 2 rows, got {len(rows)}: {out!r}")


class TestCountBound(unittest.TestCase):
    def test_exactly_n_rows(self):
        device = _make_device()
        args = _base_args(count=3)
        code, out, _ = _run_watch(
            [device],
            args,
            {device.ip: [{"x": i} for i in range(10)]},
        )
        self.assertEqual(code, 0)
        rows = [l for l in out.strip().splitlines() if l.strip()]
        self.assertEqual(len(rows), 3, f"expected 3 rows, got {len(rows)}: {out!r}")


class TestUntilSatisfiedExit0(unittest.TestCase):
    def test_exit_0_on_satisfied(self):
        device = _make_device()
        args = _base_args(until="uptime_ms > 0", count=5)
        code, out, err = _run_watch(
            [device],
            args,
            {device.ip: [{"uptime_ms": 1000}]},
        )
        self.assertEqual(code, 0)
        # Should exit on first tick — only 1 row
        rows = [l for l in out.strip().splitlines() if l.strip()]
        self.assertEqual(len(rows), 1)


class TestUntilTimedOutExit1(unittest.TestCase):
    def test_exit_1_on_timeout(self):
        device = _make_device()
        args = _base_args(until="free < 1", count=3)
        code, out, err = _run_watch(
            [device],
            args,
            {device.ip: [{"free": 99999}]},
        )
        self.assertEqual(code, 1)


class TestAnyFlagUntil(unittest.TestCase):
    def test_any_exits_0_when_one_satisfied(self):
        dev_a = _make_device(ip="192.0.2.1", board="esp32")
        dev_b = _make_device(ip="192.0.2.2", board="esp32")
        args = _base_args(until="x > 0", any_device=True, count=3)
        code, out, err = _run_watch(
            [dev_a, dev_b],
            args,
            {
                dev_a.ip: [{"x": 5}],
                dev_b.ip: [{"x": 0}],
            },
        )
        self.assertEqual(code, 0)


class TestAlertNonTerminating(unittest.TestCase):
    def test_alert_runs_to_count_exit_0(self):
        device = _make_device()
        args = _base_args(alert="internal.free < 999999999", count=3)
        code, out, _ = _run_watch(
            [device],
            args,
            {device.ip: [{"internal": {"free": 1000}}]},
        )
        self.assertEqual(code, 0)
        self.assertIn("ALERT", out)

    def test_alert_appears_on_rows(self):
        device = _make_device()
        args = _base_args(alert="x > 0", count=2)
        code, out, _ = _run_watch(
            [device],
            args,
            {device.ip: [{"x": 5}]},
        )
        rows = [l for l in out.strip().splitlines() if l.strip()]
        self.assertEqual(len(rows), 2)
        for row in rows:
            self.assertIn("ALERT", row)


class TestCsvOutput(unittest.TestCase):
    def test_csv_file_written(self):
        import tempfile
        tf = tempfile.mktemp(suffix=".csv")
        device = _make_device()
        args = _base_args(fields="internal.free", count=2, out_csv=tf)
        try:
            code, out, _ = _run_watch(
                [device],
                args,
                {device.ip: [{"internal": {"free": 42}}]},
            )
            self.assertEqual(code, 0)
            self.assertTrue(os.path.exists(tf))
            with open(tf) as fh:
                reader = list(csv.reader(fh))
            # header + 2 data rows
            self.assertEqual(reader[0], ["ts", "host", "internal.free"])
            self.assertEqual(len(reader), 3)
        finally:
            if os.path.exists(tf):
                os.unlink(tf)


class TestJsonOutput(unittest.TestCase):
    def test_json_file_written(self):
        import tempfile
        tf = tempfile.mktemp(suffix=".json")
        device = _make_device()
        args = _base_args(fields="internal.free", count=2, out_json=tf)
        try:
            code, out, _ = _run_watch(
                [device],
                args,
                {device.ip: [{"internal": {"free": 99}}]},
            )
            self.assertEqual(code, 0)
            self.assertTrue(os.path.exists(tf))
            with open(tf) as fh:
                records = json.load(fh)
            self.assertEqual(len(records), 2)
            for rec in records:
                self.assertIn("ts", rec)
                self.assertIn("host", rec)
                self.assertIn("fields", rec)
        finally:
            if os.path.exists(tf):
                os.unlink(tf)


def _run_watch_result(resolve_result, args, responses_map):
    """Like _run_watch but accepts a ResolveResult (for --hosts / flapping tests).

    responses_map: {ip: [resp_tick0, ...] | None}
    A None response list means get_json raises ConnectionRefusedError every call.
    """
    import commands.watch as watch_cmd

    call_counts = {ip: 0 for ip in responses_map}

    def _make_client(ip, port=80):
        c = MagicMock()
        resps = responses_map.get(ip, [{}])

        def _get_json(path, timeout=5):
            if resps is None:
                raise ConnectionRefusedError("connection refused")
            idx = min(call_counts[ip], len(resps) - 1)
            call_counts[ip] += 1
            return resps[idx]

        c.get_json = _get_json
        c.spec = None
        return c

    stdout_buf = io.StringIO()
    stderr_buf = io.StringIO()

    with patch("commands.watch.resolve_devices", return_value=resolve_result):
        with patch("fleetlib.client.Client", side_effect=_make_client):
            with patch("time.sleep", return_value=None):
                with patch("sys.stdout", stdout_buf):
                    with patch("sys.stderr", stderr_buf):
                        code = watch_cmd.run(args)

    return code, stdout_buf.getvalue(), stderr_buf.getvalue()


class TestDownAtT0ThenRecovered(unittest.TestCase):
    """Host down at t=0 should be retained; data captured once it comes back."""

    def test_host_down_t0_then_up(self):
        # t=0: host fails enrichment → in failures; t=1: host responds.
        ip = "192.0.2.50"
        resolve_result = ResolveResult(
            devices=[],
            failures=[EnrichFailure(host=ip, reason="timeout after 5s", category="timeout")],
            from_mdns=False,
        )
        args = _base_args(hosts=ip, count=2)
        # tick 0: None (still down), tick 1: real data
        responses_map = {ip: [None, {"free": 9999}]}
        code, out, err = _run_watch_result(resolve_result, args, responses_map)
        self.assertEqual(code, 0)
        self.assertIn("9999", out, "recovered data should appear in output")
        self.assertIn("unreachable", err, "unreachable tick should be noted on stderr")


class TestAllHostsDownAtT0(unittest.TestCase):
    """All named --hosts down at t=0 → loop keeps polling, does NOT return 1."""

    def test_all_down_keeps_polling(self):
        ip1 = "192.0.2.60"
        ip2 = "192.0.2.61"
        resolve_result = ResolveResult(
            devices=[],
            failures=[
                EnrichFailure(host=ip1, reason="timeout after 5s", category="timeout"),
                EnrichFailure(host=ip2, reason="connection refused", category="refused"),
            ],
            from_mdns=False,
        )
        args = _base_args(hosts=f"{ip1},{ip2}", count=3)
        # Both hosts stay down every tick.
        responses_map = {ip1: [None], ip2: [None]}
        code, out, err = _run_watch_result(resolve_result, args, responses_map)
        # Should complete the full count (return 0), not abort immediately (return 1).
        self.assertEqual(code, 0)
        self.assertEqual(out.strip(), "", "no data rows expected when all hosts down")
        self.assertIn("unreachable", err)


class TestMidWatchHostDrop(unittest.TestCase):
    """Host reachable at tick 0, drops at tick 1 → tick 1 recorded unreachable, loop continues."""

    def test_mid_watch_drop_no_crash(self):
        device = _make_device()
        resolve_result = ResolveResult(
            devices=[device],
            failures=[],
            from_mdns=False,
        )
        args = _base_args(hosts=device.ip, count=3)
        # tick 0: ok, tick 1: None (drops), tick 2: ok again
        responses_map = {device.ip: [{"x": 1}, None, {"x": 3}]}
        code, out, err = _run_watch_result(resolve_result, args, responses_map)
        self.assertEqual(code, 0)
        rows = [l for l in out.strip().splitlines() if l.strip()]
        self.assertEqual(len(rows), 2, f"expected 2 data rows, got {len(rows)}: {out!r}")
        self.assertIn("unreachable", err)


class TestDiscoveryModeZeroDevicesStillReturns1(unittest.TestCase):
    """mDNS discovery with zero devices → return 1 (unchanged behaviour)."""

    def test_discovery_no_devices_returns_1(self):
        resolve_result = ResolveResult(devices=[], failures=[], from_mdns=True)
        # No --hosts → discovery mode.
        args = _base_args(hosts=None, count=1)
        code, out, err = _run_watch_result(resolve_result, args, {})
        self.assertEqual(code, 1)


class TestUntilWithUnreachableTick(unittest.TestCase):
    """--until with an unreachable tick present: no crash; predicate skipped."""

    def test_until_skips_unreachable(self):
        ip = "192.0.2.70"
        resolve_result = ResolveResult(
            devices=[],
            failures=[EnrichFailure(host=ip, reason="timeout after 5s", category="timeout")],
            from_mdns=False,
        )
        # count=3: tick 0 down, tick 1 satisfies until, tick 2 never reached.
        args = _base_args(hosts=ip, until="value > 0", count=3)
        responses_map = {ip: [None, {"value": 42}]}
        code, out, err = _run_watch_result(resolve_result, args, responses_map)
        # --until satisfied at tick 1 → exit 0.
        self.assertEqual(code, 0)
        self.assertIn("42", out)

    def test_alert_skips_unreachable(self):
        ip = "192.0.2.71"
        resolve_result = ResolveResult(
            devices=[],
            failures=[EnrichFailure(host=ip, reason="timeout after 5s", category="timeout")],
            from_mdns=False,
        )
        # All ticks down; --alert should not crash.
        args = _base_args(hosts=ip, alert="value > 0", count=2)
        responses_map = {ip: [None]}
        code, out, err = _run_watch_result(resolve_result, args, responses_map)
        self.assertEqual(code, 0)
        self.assertEqual(out.strip(), "")  # no data rows


if __name__ == "__main__":
    unittest.main()
