"""logs command — retrieve device kernel log via GET /api/logs (SSE)."""
from __future__ import annotations

import sys

from core import resolve_devices, unwrap_devices, no_devices_message

NAME = "logs"
HELP = "retrieve device kernel log via GET /api/logs (SSE, read-only)"


def add_arguments(parser) -> None:
    from core import add_logs_flags
    add_logs_flags(parser)
    parser.add_argument("--follow", "-f", action="store_true",
                        help="stream until Ctrl-C (clean exit)")
    parser.add_argument("--duration", metavar="DUR",
                        help="stop after this duration: 30s / 5m / 1h / bare seconds")
    parser.add_argument("--lines", type=int, default=None, metavar="N",
                        help="stop after N log lines (default: 50 when neither --follow nor --duration given)")
    parser.add_argument("--out", metavar="PATH", dest="out_path",
                        help="also write captured lines to a file (in addition to stdout)")


def _parse_duration(s: str) -> float:
    """Parse '30s', '5m', '1h', or bare seconds (int/float)."""
    s = s.strip()
    if s.endswith("s"):
        return float(s[:-1])
    if s.endswith("m"):
        return float(s[:-1]) * 60
    if s.endswith("h"):
        return float(s[:-1]) * 3600
    return float(s)


def _stream_one_device(
    client,
    ip: str,
    follow: bool,
    max_duration,
    max_lines,
    prefix: str,
    color: str,
    reset: str,
    out_fh,
    collected: list,
    stop_event=None,
) -> int:
    """Stream log lines from one device. Returns exit code (0 or 1)."""
    import time
    import threading

    from fleetlib.sse import SSEIdleTimeout, SSEUnavailable, SSE_IDLE_TIMEOUT, stream_lines

    # Build a stop callable that checks both the caller's event and our deadline
    t0 = time.monotonic()
    local_stop = threading.Event()

    def _should_stop() -> bool:
        if local_stop.is_set():
            return True
        if stop_event is not None and stop_event.is_set():
            return True
        if max_duration is not None and (time.monotonic() - t0) >= max_duration:
            local_stop.set()
            return True
        return False

    try:
        count = 0
        for line in stream_lines(client, path="/api/logs", timeout=30.0, stop=_should_stop):
            if _should_stop():
                break
            out_line = f"{color}{prefix}{reset}{line}" if (prefix or color) else line
            print(out_line)
            if out_fh:
                out_fh.write(line + "\n")
                out_fh.flush()
            collected.append(line)
            count += 1
            if max_lines is not None and count >= max_lines:
                break
    except SSEIdleTimeout:
        print(
            f"WARNING: no log data from {ip} within {SSE_IDLE_TIMEOUT:.0f}s "
            f"(board may not support streaming or its worker is saturated); disconnecting",
            file=sys.stderr,
        )
        return 0
    except SSEUnavailable as e:
        print(f"ERROR: log sink unavailable on {ip}: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        local_stop.set()

    return 0


def run(args) -> int:
    """Retrieve device kernel log via GET /api/logs (SSE)."""
    import threading

    from fleetlib.client import Client
    from fleetlib.profiles import Profiles, profile_for
    from fleetlib.sse import SSEUnavailable, stream_lines

    follow = getattr(args, "follow", False)
    duration_str = getattr(args, "duration", None)
    max_lines = getattr(args, "lines", None)
    out_path = getattr(args, "out_path", None)

    max_duration = _parse_duration(duration_str) if duration_str else None

    # Defaults when not following and no bounds given
    if not follow and max_duration is None and max_lines is None:
        max_lines = 50
        max_duration = 10.0

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    # Single-worker --follow warning: warn before holding a connection on heap-tight boards
    if follow:
        profiles = Profiles.load()
        for d in devices:
            p = profile_for(getattr(d, "board", "") or "", profiles)
            if p.single_worker:
                print(
                    f"WARNING: {d.ip} ({getattr(d, 'board', 'unknown')}) has limited httpd workers; "
                    f"--follow can saturate it and block other endpoints — "
                    f"consider --lines/--duration instead",
                    file=sys.stderr,
                )

    # Color support: assign short per-host labels when multiple devices
    use_color = len(devices) > 1 and sys.stdout.isatty()
    _COLORS = ["\033[36m", "\033[33m", "\033[35m", "\033[32m", "\033[34m", "\033[31m"]
    _RESET = "\033[0m"

    def _host_tag(ip: str) -> str:
        """Short label derived from last octet(s) of IP."""
        parts = ip.rsplit(".", 1)
        return f"[.{parts[-1]}]"

    host_tags: dict = {}
    host_colors: dict = {}
    for i, d in enumerate(devices):
        host_tags[d.ip] = _host_tag(d.ip)
        host_colors[d.ip] = _COLORS[i % len(_COLORS)] if use_color else ""

    out_fh = None
    if out_path:
        try:
            out_fh = open(out_path, "w")
        except OSError as e:
            print(f"ERROR: cannot open --out {out_path}: {e}", file=sys.stderr)
            return 1

    collected_lines: list = []

    try:
        if len(devices) == 1:
            # Single host — simple single-threaded stream
            d = devices[0]
            c = Client(d.ip, getattr(d, "port", 80))
            exit_code = _stream_one_device(
                c, d.ip, follow, max_duration, max_lines,
                prefix="", color="", reset="",
                out_fh=out_fh, collected=collected_lines,
            )
        else:
            # Multiple hosts — concurrent threads
            stop_event = threading.Event()
            results: dict = {}
            threads = []

            def _thread_target(d, idx):
                c = Client(d.ip, getattr(d, "port", 80))
                prefix = host_tags[d.ip] + " "
                color = host_colors[d.ip]
                results[d.ip] = _stream_one_device(
                    c, d.ip, follow, max_duration, max_lines,
                    prefix=prefix, color=color, reset=_RESET if color else "",
                    out_fh=out_fh, collected=collected_lines,
                    stop_event=stop_event,
                )

            for i, d in enumerate(devices):
                t = threading.Thread(target=_thread_target, args=(d, i), daemon=True)
                threads.append(t)
                t.start()

            try:
                for t in threads:
                    t.join()
            except KeyboardInterrupt:
                stop_event.set()
                for t in threads:
                    t.join(timeout=2)
                print("", file=sys.stderr)
                print("interrupted", file=sys.stderr)
                if out_fh:
                    out_fh.close()
                return 0

            exit_code = 0
            for ip, code in results.items():
                if code != 0:
                    exit_code = code
    except KeyboardInterrupt:
        print("", file=sys.stderr)
        print("interrupted", file=sys.stderr)
        if out_fh:
            out_fh.close()
        return 0
    finally:
        if out_fh:
            try:
                out_fh.close()
            except Exception:
                pass

    return exit_code
