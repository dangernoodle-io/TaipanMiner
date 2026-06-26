"""Server-Sent Events reader for TaipanMiner fleet harness (TA-446).

Public API:
  SSEUnavailable      -- raised when the log sink is occupied or unreachable
  stream_lines(...)   -- iterator over decoded data: payload lines
  tail_lines(...)     -- bounded collect (max_lines + max_seconds); TA-447 hook
"""
from __future__ import annotations

import threading
import time
import urllib.error
import urllib.request
from typing import Callable, Iterator, List, Optional, Union

from .client import Client

_READ_CHUNK = 4096
_STATUS_PATH = "/api/logs/status"


class SSEUnavailable(Exception):
    """Raised when the SSE stream cannot be opened or the log sink is occupied."""


def _base_url(client: Client) -> str:
    return client._base


def _check_sink_status(client: Client, timeout: float = 5.0) -> None:
    """Raise SSEUnavailable if /api/logs/status indicates the sink is occupied."""
    try:
        status = client.get_json(_STATUS_PATH, timeout=timeout)
    except Exception:
        # If status endpoint errors, proceed — may not be implemented on all firmware
        return
    if status is None:
        return
    # occupied field: {"occupied": true} or {"consumers": N, "max": 1}
    if status.get("occupied") is True:
        raise SSEUnavailable(f"log sink occupied on {client.ip}")
    consumers = status.get("consumers")
    max_consumers = status.get("max")
    if consumers is not None and max_consumers is not None:
        if int(consumers) >= int(max_consumers):
            raise SSEUnavailable(
                f"log sink occupied on {client.ip} ({consumers}/{max_consumers} consumers)"
            )


def stream_lines(
    client: Client,
    path: str = "/api/logs",
    timeout: Optional[float] = None,
    stop: Optional[Union[threading.Event, Callable[[], bool]]] = None,
) -> Iterator[str]:
    """Open the SSE GET stream and yield decoded data: payload lines.

    Args:
        client:  Client instance; provides base URL and host info.
        path:    Endpoint path (default: /api/logs).
        timeout: Per-read socket timeout in seconds (None = no timeout).
        stop:    threading.Event or zero-arg callable; when set/truthy, stops iteration.

    Raises:
        SSEUnavailable: if the sink status is occupied or the stream cannot be opened.
    """
    _check_sink_status(client)

    url = f"{_base_url(client)}{path}"
    req = urllib.request.Request(url, headers={"Accept": "text/event-stream"})

    def _stopped() -> bool:
        if stop is None:
            return False
        if isinstance(stop, threading.Event):
            return stop.is_set()
        return bool(stop())

    try:
        resp = urllib.request.urlopen(req, timeout=timeout)
    except urllib.error.HTTPError as e:
        raise SSEUnavailable(f"HTTP {e.code} opening log stream on {client.ip}: {e.reason}") from e
    except Exception as e:
        raise SSEUnavailable(f"cannot open log stream on {client.ip}: {e}") from e

    try:
        buf = b""
        while not _stopped():
            try:
                chunk = resp.read(_READ_CHUNK)
            except Exception:
                break
            if not chunk:
                break
            buf += chunk
            # Split on newlines, keep any incomplete trailing line in buf
            while b"\n" in buf:
                if _stopped():
                    return
                line_bytes, buf = buf.split(b"\n", 1)
                line = line_bytes.rstrip(b"\r").decode("utf-8", errors="replace")
                if line.startswith("data:"):
                    payload = line[5:].lstrip(" ")
                    yield payload
                    if _stopped():
                        return
                # skip empty lines, ": " heartbeats/comments silently
    finally:
        try:
            resp.close()
        except Exception:
            pass


def tail_lines(
    client: Client,
    path: str = "/api/logs",
    max_lines: int = 50,
    max_seconds: float = 10.0,
) -> List[str]:
    """Collect up to max_lines lines within max_seconds, then return.

    Designed for TA-447 anomaly log-tail: call with both bounds to get a
    bounded snapshot even if the stream is very active or very quiet.

    Returns:
        List of decoded log line strings (data: payloads).

    Raises:
        SSEUnavailable: propagated from stream_lines if the sink is occupied.
    """
    stop_event = threading.Event()
    deadline = time.monotonic() + max_seconds
    lines: List[str] = []

    def _deadline_watcher() -> None:
        remaining = deadline - time.monotonic()
        if remaining > 0:
            stop_event.wait(remaining)
        stop_event.set()

    watcher = threading.Thread(target=_deadline_watcher, daemon=True)
    watcher.start()

    try:
        for line in stream_lines(client, path=path, timeout=max_seconds, stop=stop_event):
            lines.append(line)
            if len(lines) >= max_lines:
                break
    finally:
        stop_event.set()

    return lines
