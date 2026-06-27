"""Device discovery via mDNS (_taipanminer._tcp.local.) or explicit host list."""
from __future__ import annotations
import socket
import urllib.error
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

from .client import Client, TIMEOUT_INFO, info_field


@dataclass
class Device:
    hostname: str
    ip: str
    port: int
    board: str
    version: str


@dataclass
class EnrichFailure:
    """A host that failed /api/info enrichment with a human-readable reason."""
    host: str
    reason: str      # e.g. "timeout after 5s", "connection refused", "no route to host"
    category: str    # "timeout" | "refused" | "no_route" | "http_error" | "bad_response"


@dataclass
class ResolveResult:
    """Result of resolve_devices: successfully enriched devices + per-host failures."""
    devices: List[Device] = field(default_factory=list)
    failures: List[EnrichFailure] = field(default_factory=list)
    # True when the device list came from mDNS (discover), False when from --hosts.
    from_mdns: bool = False


def _classify_enrich_exception(exc: Exception, timeout: float) -> Tuple[str, str]:
    """Classify a network exception into (category, human_reason).

    Returns a (category, reason) tuple for EnrichFailure.
    """
    msg = str(exc)
    exc_type = type(exc).__name__

    # socket.timeout and urllib's URLError wrapping a timeout
    if isinstance(exc, socket.timeout):
        return "timeout", f"timeout after {timeout:.0f}s"

    # HTTPError is a subclass of URLError — check it first
    if isinstance(exc, urllib.error.HTTPError):
        return "http_error", f"HTTP {exc.code}"

    if isinstance(exc, urllib.error.URLError):
        reason = exc.reason
        if isinstance(reason, socket.timeout):
            return "timeout", f"timeout after {timeout:.0f}s"
        if isinstance(reason, OSError):
            errno_val = getattr(reason, "errno", None)
            reason_str = str(reason).lower()
            if "timed out" in reason_str or "timeout" in reason_str:
                return "timeout", f"timeout after {timeout:.0f}s"
            if "connection refused" in reason_str or errno_val == 111:
                return "refused", "connection refused"
            if any(s in reason_str for s in ("no route", "network is unreachable",
                                              "host is unreachable", "nodename nor servname")):
                return "no_route", "no route to host"
            if "name or service not known" in reason_str or "name resolution" in reason_str:
                return "no_route", "hostname not resolved"
        reason_str = str(reason).lower() if reason else ""
        if "timed out" in reason_str or "timeout" in reason_str:
            return "timeout", f"timeout after {timeout:.0f}s"
        return "no_route", f"unreachable ({msg[:60]})"

    # OSError / ConnectionRefusedError / TimeoutError (stdlib)
    if isinstance(exc, ConnectionRefusedError):
        return "refused", "connection refused"
    if isinstance(exc, TimeoutError):
        return "timeout", f"timeout after {timeout:.0f}s"
    if isinstance(exc, OSError):
        msg_l = msg.lower()
        if "timed out" in msg_l or "timeout" in msg_l:
            return "timeout", f"timeout after {timeout:.0f}s"
        if "connection refused" in msg_l:
            return "refused", "connection refused"
        if "no route" in msg_l or "unreachable" in msg_l:
            return "no_route", "no route to host"
        return "no_route", f"unreachable ({msg[:60]})"

    return "bad_response", f"error ({exc_type}: {msg[:60]})"


def _enrich(ip: str, port: int = 80) -> Optional[Device]:
    """Fetch /api/info from ip:port and build a Device. Returns None if unreachable."""
    c = Client(ip, port)
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is None:
        return None
    hostname = info.get("hostname") or info.get("host") or ip
    board = info_field(info, "board") or "unknown"
    version = info_field(info, "version") or "unknown"
    return Device(hostname=hostname, ip=ip, port=port, board=board, version=version)


def _enrich_with_reason(ip: str, port: int = 80,
                        timeout: float = TIMEOUT_INFO) -> Tuple[Optional[Device], Optional[EnrichFailure]]:
    """Fetch /api/info and return (Device, None) on success or (None, EnrichFailure) on failure."""
    import urllib.request
    import json
    url = f"http://{ip}:{port}/api/info" if port != 80 else f"http://{ip}/api/info"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            data = json.loads(r.read())
        hostname = data.get("hostname") or data.get("host") or ip
        board = info_field(data, "board") or "unknown"
        version = info_field(data, "version") or "unknown"
        return Device(hostname=hostname, ip=ip, port=port, board=board, version=version), None
    except Exception as exc:
        category, reason = _classify_enrich_exception(exc, timeout)
        return None, EnrichFailure(host=ip, reason=reason, category=category)


def from_hosts(hosts: List[str], port: int = 80) -> List[Device]:
    """Build device list from explicit IP/hostname strings, enriched via /api/info.

    Hosts that are unreachable are silently skipped.

    .. deprecated::
        Prefer ``from_hosts_detailed`` which returns a ``ResolveResult`` with per-host
        failure reasons.  This function is retained for backward compatibility with
        callers that only need the device list.
    """
    devices: List[Device] = []
    for h in hosts:
        d = _enrich(h, port)
        if d is not None:
            devices.append(d)
    return devices


def from_hosts_detailed(hosts: List[str], port: int = 80) -> ResolveResult:
    """Build a ResolveResult from explicit IP/hostname strings.

    Each host is enriched via GET /api/info.  Failures are captured with a
    per-host reason rather than being silently dropped.
    """
    result = ResolveResult(from_mdns=False)
    for h in hosts:
        device, failure = _enrich_with_reason(h, port)
        if device is not None:
            result.devices.append(device)
        else:
            result.failures.append(failure)
    return result


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


def _read_identity(device: Device):
    """Fetch /api/info and return (board, hostname).

    Returns (None, None) when the device is unreachable (info=None).
    Returns (board, hostname) — either or both may be a non-None string — when
    the device responds.  Used by safety.Guard to distinguish unreachable from
    a genuine identity mismatch.
    """
    c = Client(device.ip, device.port)
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is None:
        return None, None
    board = info_field(info, "board")
    hostname = info.get("hostname") or info.get("host")
    return board, hostname


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
    if expect_board is not None and info_field(info, "board") != expect_board:
        return False
    actual_hostname = info.get("hostname") or info.get("host")
    if expect_hostname is not None and actual_hostname != expect_hostname:
        return False
    return True
