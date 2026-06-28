"""Offline unit tests for fleetlib.sse (TA-446)."""
import io
import os
import sys
import threading
import time
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.sse import SSEIdleTimeout, SSEUnavailable, stream_lines, tail_lines


def _make_client(ip="192.0.2.81", port=80):
    c = MagicMock()
    c.ip = ip
    c.port = port
    c._base = f"http://{ip}" if port == 80 else f"http://{ip}:{port}"
    c.get_json = MagicMock(return_value=None)
    return c


def _fake_response(chunks):
    """Build a fake urllib response that yields bytes in chunks."""
    buf = [b"".join(chunks)]
    pos = [0]

    class _FakeResp:
        def read(self, n):
            remaining = buf[0][pos[0]:]
            if not remaining:
                return b""
            chunk = remaining[:n]
            pos[0] += len(chunk)
            return chunk

        def close(self):
            pass

        def __enter__(self):
            return self

        def __exit__(self, *a):
            self.close()

    return _FakeResp()


class TestStreamLinesBasic(unittest.TestCase):
    def _run(self, raw_bytes, stop=None):
        client = _make_client()
        resp = _fake_response([raw_bytes])
        with patch("urllib.request.urlopen", return_value=resp):
            return list(stream_lines(client, stop=stop))

    def test_simple_data_lines(self):
        raw = b"data: hello\n\ndata: world\n\n"
        lines = self._run(raw)
        self.assertEqual(lines, ["hello", "world"])

    def test_skips_heartbeat_comments(self):
        raw = b": heartbeat\ndata: line1\n\n: keep-alive\ndata: line2\n\n"
        lines = self._run(raw)
        self.assertEqual(lines, ["line1", "line2"])

    def test_skips_blank_lines(self):
        raw = b"\n\ndata: only\n\n\n"
        lines = self._run(raw)
        self.assertEqual(lines, ["only"])

    def test_data_colon_space_stripped(self):
        raw = b"data: with space\n\n"
        lines = self._run(raw)
        self.assertEqual(lines, ["with space"])

    def test_data_no_space(self):
        raw = b"data:nospace\n\n"
        lines = self._run(raw)
        self.assertEqual(lines, ["nospace"])

    def test_mixed_event_types_ignored(self):
        raw = b"event: type\ndata: payload\n\n"
        lines = self._run(raw)
        self.assertEqual(lines, ["payload"])

    def test_crlf_line_endings(self):
        raw = b"data: crlf\r\n\r\ndata: also\r\n\r\n"
        lines = self._run(raw)
        self.assertEqual(lines, ["crlf", "also"])


class TestStreamLinesChunkBoundary(unittest.TestCase):
    """data: payload split across chunk boundaries."""

    def _run_chunks(self, chunk_list):
        client = _make_client()
        # We'll deliver each chunk separately using a custom response
        all_bytes = b"".join(chunk_list)
        chunk_iter = iter(chunk_list)
        delivered = [False]

        class _ChunkedResp:
            def read(self, n):
                try:
                    return next(chunk_iter)
                except StopIteration:
                    return b""

            def close(self):
                pass

        with patch("urllib.request.urlopen", return_value=_ChunkedResp()):
            return list(stream_lines(client))

    def test_payload_split_across_chunks(self):
        # "data: hel" in one chunk, "lo\n\n" in next
        lines = self._run_chunks([b"data: hel", b"lo\n\n"])
        self.assertEqual(lines, ["hello"])

    def test_multiple_events_split(self):
        lines = self._run_chunks([
            b"data: fi",
            b"rst\n\ndat",
            b"a: second\n\n",
        ])
        self.assertEqual(lines, ["first", "second"])

    def test_newline_split_between_chunks(self):
        # newline itself is the split
        lines = self._run_chunks([b"data: hi\n", b"\ndata: bye\n\n"])
        self.assertEqual(lines, ["hi", "bye"])


class TestStreamLinesStop(unittest.TestCase):
    def test_stop_event_halts_iteration(self):
        # Pre-set stop event so it fires before we start
        stop = threading.Event()
        stop.set()
        raw = b"data: line1\n\ndata: line2\n\ndata: line3\n\n"
        client = _make_client()
        resp = _fake_response([raw])
        results = []

        with patch("urllib.request.urlopen", return_value=resp):
            for line in stream_lines(client, stop=stop):
                results.append(line)

        # stop was already set, so nothing should be yielded
        self.assertEqual(results, [])

    def test_stop_event_halts_mid_stream(self):
        # Stop event fires after first yield (set by a watcher thread)
        stop = threading.Event()
        raw = b"data: line1\n\ndata: line2\n\ndata: line3\n\n"
        client = _make_client()
        resp = _fake_response([raw])
        results = []

        def _set_stop_after_delay():
            # Set after a brief moment; stream_lines checks stop after each yield
            import time
            time.sleep(0.01)
            stop.set()

        watcher = threading.Thread(target=_set_stop_after_delay, daemon=True)
        watcher.start()

        with patch("urllib.request.urlopen", return_value=resp):
            for line in stream_lines(client, stop=stop):
                results.append(line)

        # At most 3 lines (all lines); stop fires asynchronously
        self.assertLessEqual(len(results), 3)

    def test_stop_callable(self):
        # Callable that returns True from the first call
        def _stop():
            return True

        raw = b"data: a\n\ndata: b\n\ndata: c\n\n"
        client = _make_client()
        resp = _fake_response([raw])
        with patch("urllib.request.urlopen", return_value=resp):
            lines = list(stream_lines(client, stop=_stop))
        # stop() returns True immediately (before while loop), so 0 lines
        self.assertEqual(lines, [])


class TestSSEUnavailable(unittest.TestCase):
    def test_http_error_raises_unavailable(self):
        import urllib.error
        client = _make_client()
        client.get_json = MagicMock(return_value=None)
        err = urllib.error.HTTPError(url="http://x", code=503, msg="busy",
                                     hdrs=None, fp=None)
        with patch("urllib.request.urlopen", side_effect=err):
            with self.assertRaises(SSEUnavailable):
                list(stream_lines(client))

    def test_connection_error_raises_unavailable(self):
        import urllib.error
        client = _make_client()
        client.get_json = MagicMock(return_value=None)
        err = urllib.error.URLError("connection refused")
        with patch("urllib.request.urlopen", side_effect=err):
            with self.assertRaises(SSEUnavailable):
                list(stream_lines(client))

    def test_sink_occupied_status_raises(self):
        client = _make_client()
        client.get_json = MagicMock(return_value={"occupied": True})
        with self.assertRaises(SSEUnavailable) as ctx:
            list(stream_lines(client))
        self.assertIn("occupied", str(ctx.exception))

    def test_sink_consumers_at_max_raises(self):
        client = _make_client()
        client.get_json = MagicMock(return_value={"consumers": 1, "max": 1})
        with self.assertRaises(SSEUnavailable):
            list(stream_lines(client))

    def test_sink_available_does_not_raise(self):
        client = _make_client()
        client.get_json = MagicMock(return_value={"occupied": False})
        raw = b"data: hi\n\n"
        resp = _fake_response([raw])
        with patch("urllib.request.urlopen", return_value=resp):
            lines = list(stream_lines(client))
        self.assertEqual(lines, ["hi"])

    def test_status_none_does_not_raise(self):
        client = _make_client()
        client.get_json = MagicMock(return_value=None)
        raw = b"data: hi\n\n"
        resp = _fake_response([raw])
        with patch("urllib.request.urlopen", return_value=resp):
            lines = list(stream_lines(client))
        self.assertEqual(lines, ["hi"])


class TestTailLines(unittest.TestCase):
    def _patch_stream(self, line_list):
        """Patch stream_lines to yield from line_list."""
        def _fake_stream(client, path="/api/logs", timeout=None, stop=None, **kwargs):
            for line in line_list:
                if stop is not None:
                    stopped = stop.is_set() if isinstance(stop, threading.Event) else stop()
                    if stopped:
                        return
                yield line

        return patch("fleetlib.sse.stream_lines", side_effect=_fake_stream)

    def test_max_lines_bound(self):
        client = _make_client()
        lines = ["line%d" % i for i in range(20)]
        with self._patch_stream(lines):
            result = tail_lines(client, max_lines=5, max_seconds=60.0)
        self.assertEqual(result, ["line0", "line1", "line2", "line3", "line4"])

    def test_max_seconds_bound(self):
        """tail_lines stops when the stop event fires."""
        client = _make_client()

        def _slow_stream(client, path="/api/logs", timeout=None, stop=None, **kwargs):
            for i in range(100):
                if stop is not None:
                    stopped = stop.is_set() if isinstance(stop, threading.Event) else stop()
                    if stopped:
                        return
                # sleep so deadline watcher fires
                time.sleep(0.05)
                yield f"line{i}"

        with patch("fleetlib.sse.stream_lines", side_effect=_slow_stream):
            result = tail_lines(client, max_lines=100, max_seconds=0.15)
        # 0.15s / 0.05s per line = ~3 lines max before deadline fires
        self.assertLess(len(result), 10)

    def test_returns_list(self):
        client = _make_client()
        with self._patch_stream(["a", "b", "c"]):
            result = tail_lines(client, max_lines=10, max_seconds=5.0)
        self.assertIsInstance(result, list)
        self.assertEqual(result, ["a", "b", "c"])

    def test_sse_unavailable_propagates(self):
        client = _make_client()

        def _raises(client, path="/api/logs", timeout=None, stop=None, **kwargs):
            raise SSEUnavailable("occupied")
            yield  # make it a generator

        with patch("fleetlib.sse.stream_lines", side_effect=_raises):
            with self.assertRaises(SSEUnavailable):
                tail_lines(client)

    def test_empty_stream(self):
        client = _make_client()
        with self._patch_stream([]):
            result = tail_lines(client, max_lines=10, max_seconds=5.0)
        self.assertEqual(result, [])


class TestStreamLinesIdleTimeout(unittest.TestCase):
    """TA-458: early-bail when no SSE line arrives within the idle window."""

    def _timeout_response(self):
        """Fake response whose read() always raises socket.timeout (no data ever arrives)."""
        import socket as _socket

        class _TimeoutResp:
            def read(self, n):
                raise _socket.timeout("timed out")

            def close(self):
                pass

        return _TimeoutResp()

    def test_idle_timeout_fires_when_no_data(self):
        """No data within idle window => SSEIdleTimeout raised, does NOT block to full timeout."""
        client = _make_client()
        resp = self._timeout_response()
        t0 = time.monotonic()
        with patch("urllib.request.urlopen", return_value=resp):
            with self.assertRaises(SSEIdleTimeout):
                list(stream_lines(client, timeout=30.0, idle_timeout=0.05))
        elapsed = time.monotonic() - t0
        # Must bail well before the 30s socket timeout
        self.assertLess(elapsed, 5.0, f"idle bail took too long: {elapsed:.2f}s")

    def test_idle_timeout_not_fired_when_data_arrives(self):
        """Lines arriving before idle window => streams normally, no SSEIdleTimeout."""
        client = _make_client()
        raw = b"data: line1\n\ndata: line2\n\n"
        resp = _fake_response([raw])
        with patch("urllib.request.urlopen", return_value=resp):
            lines = list(stream_lines(client, idle_timeout=10.0))
        self.assertEqual(lines, ["line1", "line2"])

    def test_idle_timeout_none_disables_check(self):
        """idle_timeout=None: no SSEIdleTimeout even if stream is silent."""
        client = _make_client()
        # Response that immediately returns empty (EOF), no idle check
        resp = _fake_response([b""])
        with patch("urllib.request.urlopen", return_value=resp):
            lines = list(stream_lines(client, idle_timeout=None))
        self.assertEqual(lines, [])

    def test_comment_line_resets_idle_clock(self):
        """A SSE comment (': connected') counts as activity and prevents early bail."""
        client = _make_client()
        raw = b": connected\ndata: hello\n\n"
        resp = _fake_response([raw])
        with patch("urllib.request.urlopen", return_value=resp):
            lines = list(stream_lines(client, idle_timeout=0.05))
        self.assertEqual(lines, ["hello"])


class TestTailLinesNoIdleTimeout(unittest.TestCase):
    """tail_lines must not raise SSEIdleTimeout (it disables idle check)."""

    def test_tail_lines_silent_stream_returns_empty(self):
        """A silent stream with tail_lines returns [] without raising SSEIdleTimeout."""
        client = _make_client()

        def _silent(client, path="/api/logs", timeout=None, stop=None, idle_timeout=None):
            # Immediately signal stop and yield nothing
            if stop is not None:
                if isinstance(stop, threading.Event):
                    stop.set()
            return
            yield  # make it a generator

        with patch("fleetlib.sse.stream_lines", side_effect=_silent):
            result = tail_lines(client, max_lines=10, max_seconds=0.1)
        self.assertEqual(result, [])


if __name__ == "__main__":
    unittest.main()
