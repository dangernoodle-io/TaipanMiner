"""Device discovery via mDNS (_taipanminer._tcp.local.) or explicit host list."""
from __future__ import annotations
from dataclasses import dataclass
from typing import List, Optional

from .client import Client, TIMEOUT_INFO


@dataclass
class Device:
    hostname: str
    ip: str
    port: int
    board: str
    version: str


def _enrich(ip: str, port: int = 80) -> Optional[Device]:
    """Fetch /api/info from ip:port and build a Device. Returns None if unreachable."""
    c = Client(ip, port)
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is None:
        return None
    hostname = info.get("hostname") or info.get("host") or ip
    board = info.get("board") or "unknown"
    version = info.get("version") or "unknown"
    return Device(hostname=hostname, ip=ip, port=port, board=board, version=version)


def from_hosts(hosts: List[str], port: int = 80) -> List[Device]:
    """Build device list from explicit IP/hostname strings, enriched via /api/info.

    Hosts that are unreachable are silently skipped.
    """
    devices: List[Device] = []
    for h in hosts:
        d = _enrich(h, port)
        if d is not None:
            devices.append(d)
    return devices


def discover(timeout: float = 5) -> List[Device]:
    """Discover devices via mDNS browsing _taipanminer._tcp.local.

    Each mDNS hit is enriched via GET /api/info (overrides stale TXT records).
    Falls back to TXT data if /api/info is unreachable.

    Raises ImportError if zeroconf is not installed.
    """
    try:
        from zeroconf import Zeroconf, ServiceBrowser
    except ImportError:
        raise ImportError(
            "zeroconf is required for mDNS discovery; "
            "install it with: pip install zeroconf"
        )

    import time

    found: dict = {}

    class _Listener:
        def add_service(self, zc: "Zeroconf", stype: str, name: str) -> None:
            info = zc.get_service_info(stype, name)
            if info is None:
                return
            addrs = info.parsed_addresses()
            if not addrs:
                return
            ip = addrs[0]
            port = info.port or 80
            props = {
                (k.decode() if isinstance(k, bytes) else k): (
                    v.decode() if isinstance(v, bytes) else v
                )
                for k, v in (info.properties or {}).items()
            }
            found[ip] = {
                "ip": ip,
                "port": port,
                "props": props,
                "hostname": info.server or ip,
            }

        def remove_service(self, zc: "Zeroconf", stype: str, name: str) -> None:
            pass

        def update_service(self, zc: "Zeroconf", stype: str, name: str) -> None:
            self.add_service(zc, stype, name)

    zc = Zeroconf()
    _browser = ServiceBrowser(zc, "_taipanminer._tcp.local.", _Listener())
    time.sleep(timeout)
    zc.close()

    devices: List[Device] = []
    for meta in found.values():
        d = _enrich(meta["ip"], meta["port"])
        if d is None:
            # fallback: use TXT record data when /api/info is unreachable
            props = meta.get("props", {})
            d = Device(
                hostname=meta["hostname"],
                ip=meta["ip"],
                port=meta["port"],
                board=props.get("board", "unknown"),
                version=props.get("version", "unknown"),
            )
        devices.append(d)
    return devices


def verify_identity(
    device: Device,
    expect_board: Optional[str] = None,
    expect_hostname: Optional[str] = None,
) -> bool:
    """Re-fetch /api/info and verify that board/hostname match expectations.

    Used by safety.Guard before any destructive operation.
    Returns True if identity is confirmed (or no expectations set).
    Returns False if unreachable or any expectation mismatches.
    """
    c = Client(device.ip, device.port)
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is None:
        return False
    if expect_board is not None and info.get("board") != expect_board:
        return False
    actual_hostname = info.get("hostname") or info.get("host")
    if expect_hostname is not None and actual_hostname != expect_hostname:
        return False
    return True
