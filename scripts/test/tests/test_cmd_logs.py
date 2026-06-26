"""Offline unit tests for fleet.py cmd_logs (TA-446)."""
import io
import os
import sys
import tempfile
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device
from fleetlib.sse import SSEUnavailable


def _make_device(ip="192.0.2.81", board="esp32-wroom32") -> Device:
    return Device(hostname=board, ip=ip, port=80, board=board, version="v0.99.0")


def _base_args(**kwargs):
    defaults = dict(
        follow=False,
        duration=None,
        lines=None,
        out_path=None,
        hosts=None,
        board=None,
        discover_timeout=10,
        log_level="WARNING",
    )
    defaults.update(kwargs)
    return SimpleNamespace(**defaults)


def _run_logs(devices, args, stream_lines_side_effect=None):
    """Call cmd_logs with mocked resolve_devices and stream_lines."""
    import fleet

    stdout_buf = io.StringIO()
    stderr_buf = io.StringIO()

    def _default_stream(client, path="/api/logs", timeout=None, stop=None):
        for line in ["log line 1", "log line 2", "log line 3"]:
            if stop is not None:
                stopped = stop.is_set() if isinstance(stop, threading.Event) else stop()
                if stopped:
                    return
            yield line

    side_effect = stream_lines_side_effect or _default_stream

    with patch("fleet.resolve_devices", return_value=devices):
        with patch("fleetlib.sse.stream_lines", side_effect=side_effect):
            with patch("sys.stdout", stdout_buf):
                with patch("sys.stderr", stderr_buf):
                    code = fleet.cmd_logs(args)

    return code, stdout_buf.getvalue(), stderr_buf.getvalue()


class TestCmdLogsLinesFlag(unittest.TestCase):
    def test_lines_bound_respected(self):
        devices = [_make_device()]
        args = _base_args(lines=2)
        code, stdout, _ = _run_logs(devices, args)
        self.assertEqual(code, 0)
        lines = [l for l in stdout.splitlines() if l]
        self.assertEqual(len(lines), 2)

    def test_lines_single_line(self):
        devices = [_make_device()]
        args = _base_args(lines=1)
        code, stdout, _ = _run_logs(devices, args)
        self.assertEqual(code, 0)
        self.assertEqual(stdout.strip(), "log line 1")


class TestCmdLogsDurationFlag(unittest.TestCase):
    def test_duration_stops_stream(self):
        devices = [_make_device()]
        args = _base_args(duration="0.15s")

        import time as _time
        call_count = [0]

        def _slow_stream(client, path="/api/logs", timeout=None, stop=None):
            for i in range(100):
                if stop is not None:
                    s = stop.is_set() if isinstance(stop, threading.Event) else stop()
                    if s:
                        return
                # sleep so the deadline fires before all 100 lines
                _time.sleep(0.05)
                call_count[0] += 1
                yield f"line{i}"

        code, stdout, _ = _run_logs(devices, args, stream_lines_side_effect=_slow_stream)
        self.assertEqual(code, 0)
        # 0.15s / 0.05s = ~3 lines; well under 100
        self.assertLess(call_count[0], 10)


class TestCmdLogsDefaultBounds(unittest.TestCase):
    def test_default_50_lines(self):
        """No --follow, no --duration, no --lines: should collect up to 50 lines."""
        devices = [_make_device()]
        args = _base_args()  # no follow, no duration, no lines

        produced = [f"line{i}" for i in range(80)]

        def _big_stream(client, path="/api/logs", timeout=None, stop=None):
            for line in produced:
                if stop is not None:
                    s = stop.is_set() if isinstance(stop, threading.Event) else stop()
                    if s:
                        return
                yield line

        code, stdout, _ = _run_logs(devices, args, stream_lines_side_effect=_big_stream)
        self.assertEqual(code, 0)
        out_lines = [l for l in stdout.splitlines() if l]
        self.assertEqual(len(out_lines), 50)


class TestCmdLogsMultiHost(unittest.TestCase):
    def test_multihost_prefix(self):
        devices = [
            _make_device(ip="192.0.2.81"),
            _make_device(ip="192.0.2.68"),
        ]
        args = _base_args(lines=1)
        code, stdout, _ = _run_logs(devices, args)
        self.assertEqual(code, 0)
        # Each line should be prefixed with a host tag
        lines = [l for l in stdout.splitlines() if l]
        self.assertTrue(any(".81" in l or ".68" in l for l in lines),
                        f"No host prefix found in: {lines}")

    def test_multihost_both_prefixes_appear(self):
        devices = [
            _make_device(ip="192.0.2.81"),
            _make_device(ip="192.0.2.68"),
        ]
        args = _base_args(lines=3)
        code, stdout, _ = _run_logs(devices, args)
        self.assertEqual(code, 0)
        lines = stdout.splitlines()
        has_81 = any(".81" in l for l in lines)
        has_68 = any(".68" in l for l in lines)
        self.assertTrue(has_81, f"No .81 tag in output: {lines}")
        self.assertTrue(has_68, f"No .68 tag in output: {lines}")


class TestCmdLogsOutFile(unittest.TestCase):
    def test_out_path_written(self):
        devices = [_make_device()]
        with tempfile.NamedTemporaryFile(mode="r", suffix=".txt", delete=False) as f:
            tmp_path = f.name

        try:
            args = _base_args(lines=2, out_path=tmp_path)
            code, stdout, _ = _run_logs(devices, args)
            self.assertEqual(code, 0)
            with open(tmp_path) as fh:
                file_lines = [l.rstrip("\n") for l in fh.readlines() if l.strip()]
            self.assertEqual(file_lines, ["log line 1", "log line 2"])
        finally:
            os.unlink(tmp_path)

    def test_out_and_stdout_both_written(self):
        devices = [_make_device()]
        with tempfile.NamedTemporaryFile(mode="r", suffix=".txt", delete=False) as f:
            tmp_path = f.name

        try:
            args = _base_args(lines=1, out_path=tmp_path)
            code, stdout, _ = _run_logs(devices, args)
            self.assertEqual(code, 0)
            self.assertIn("log line 1", stdout)
            with open(tmp_path) as fh:
                file_content = fh.read()
            self.assertIn("log line 1", file_content)
        finally:
            os.unlink(tmp_path)


class TestCmdLogsOccupiedSink(unittest.TestCase):
    def test_occupied_sink_nonzero_exit(self):
        devices = [_make_device()]
        args = _base_args(lines=5)

        def _occupied(client, path="/api/logs", timeout=None, stop=None):
            raise SSEUnavailable(f"log sink occupied on {client.ip}")
            yield  # make it a generator

        code, stdout, stderr = _run_logs(devices, args,
                                         stream_lines_side_effect=_occupied)
        self.assertNotEqual(code, 0)
        self.assertIn("unavailable", stderr.lower())

    def test_occupied_sink_message_names_host(self):
        devices = [_make_device(ip="192.0.2.81")]
        args = _base_args(lines=5)

        def _occupied(client, path="/api/logs", timeout=None, stop=None):
            raise SSEUnavailable(f"log sink occupied on {client.ip}")
            yield

        code, stdout, stderr = _run_logs(devices, args,
                                         stream_lines_side_effect=_occupied)
        self.assertIn("192.0.2.81", stderr)

    def test_multihost_one_unavailable_continues_others(self):
        """When one host is unavailable, the others still stream."""
        devices = [
            _make_device(ip="192.0.2.81"),
            _make_device(ip="192.0.2.68"),
        ]
        args = _base_args(lines=2)

        def _mixed(client, path="/api/logs", timeout=None, stop=None):
            if client.ip == "192.0.2.81":
                raise SSEUnavailable(f"log sink occupied on {client.ip}")
                yield
            else:
                yield "log line 1"
                yield "log line 2"

        code, stdout, stderr = _run_logs(devices, args,
                                         stream_lines_side_effect=_mixed)
        # .81 is unavailable → nonzero; but .68 still streams
        self.assertNotEqual(code, 0)
        # .68 lines should appear in stdout
        self.assertIn("log line", stdout)
        # error about .81 in stderr
        self.assertIn("192.0.2.81", stderr)


class TestCmdLogsNoDevices(unittest.TestCase):
    def test_no_devices_returns_nonzero(self):
        import fleet

        args = _base_args()
        stdout_buf = io.StringIO()
        stderr_buf = io.StringIO()
        with patch("fleet.resolve_devices", return_value=[]):
            with patch("sys.stdout", stdout_buf):
                with patch("sys.stderr", stderr_buf):
                    code = fleet.cmd_logs(args)
        self.assertNotEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
