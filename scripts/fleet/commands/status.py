"""status command — GET /api/info + /api/health per device."""
from __future__ import annotations

import sys

from core import resolve_devices, unwrap_devices, no_devices_message

NAME = "status"
HELP = "GET /api/info + /api/health per device"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)


def _fmt_uptime(uptime_ms: int) -> str:
    s = uptime_ms // 1000
    h, rem = divmod(s, 3600)
    m, sec = divmod(rem, 60)
    if h > 0:
        return f"{h}h{m:02d}m{sec:02d}s"
    if m > 0:
        return f"{m}m{sec:02d}s"
    return f"{sec}s"


def run(args) -> int:
    """Fetch /api/info + /api/health for each device and print summary."""
    from fleetlib.client import Client, TIMEOUT_INFO, TIMEOUT_HEALTH, info_field

    result = resolve_devices(args)
    devices = unwrap_devices(result)
    if not devices:
        print(no_devices_message(result), file=sys.stderr)
        return 1

    print(f"{'HOST':<20} {'BOARD':<20} {'VERSION':<16} {'UPTIME':>12}  {'HEAP FREE':>12}  HEALTH")
    print("-" * 90)
    all_ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
        health = c.get_json("/api/health", timeout=TIMEOUT_HEALTH)

        if info is None:
            print(f"{d.ip:<20} {'??':<20} {'??':<16} {'UNREACHABLE':>12}")
            all_ok = False
            continue

        uptime_ms = info.get("uptime_ms", 0)
        uptime_str = _fmt_uptime(uptime_ms)

        heap = c.get_json("/api/diag/heap", timeout=TIMEOUT_INFO)
        heap_free: object = None
        if heap is not None:
            heap_free = (heap.get("internal") or {}).get("free")
        if heap_free is None:
            heap_free = info.get("free_heap")
        heap_str = f"{heap_free:,}" if isinstance(heap_free, int) else "??"

        board = info_field(info, "board") or d.board
        version = info_field(info, "version") or d.version

        if health is None:
            health_str = "??"
        elif health.get("ok") is True:
            health_str = "ok"
        elif health.get("ok") is False:
            health_str = "unhealthy"
            all_ok = False
        else:
            health_str = "??"

        print(f"{d.ip:<20} {board:<20} {version:<16} {uptime_str:>12}  {heap_str:>12}  {health_str}")

    return 0 if all_ok else 1
