"""discover command — discover devices via mDNS and print table."""
from __future__ import annotations

from core import resolve_devices, unwrap_devices, no_devices_message

NAME = "discover"
HELP = "discover devices via mDNS and print table"


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


def _print_device_table(devices, extra_headers=None) -> None:
    from fleetlib.client import Client, TIMEOUT_INFO
    print(f"{'HOST':<20} {'BOARD':<20} {'VERSION':<16} {'UPTIME':>12}")
    print("-" * 72)
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
        uptime_str = "??"
        if info:
            uptime_str = _fmt_uptime(info.get("uptime_ms", 0))
        print(f"{d.ip:<20} {d.board:<20} {d.version:<16} {uptime_str:>12}")
    print(f"\n{len(devices)} device(s) found.")


def run(args) -> int:
    print("Discovering devices (mDNS _taipanminer._tcp.local.)…")
    result = resolve_devices(args)
    devices = unwrap_devices(result)
    if not devices:
        print(no_devices_message(result))
        return 0
    _print_device_table(devices, extra_headers=["uptime"])
    return 0
