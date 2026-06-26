"""OTA operations: push, pull, mark-valid, wait-for-boot, verify.

All mutating steps are gated through safety.Guard when a Guard instance is supplied.
Timeout constants come from client.py — no magic numbers here.
"""
from __future__ import annotations
import logging
import time
from typing import Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from .discovery import Device
    from .safety import Guard

from .client import (
    Client,
    TIMEOUT_INFO,
    TIMEOUT_WRITE,
    TIMEOUT_OTA_PUSH,
    TIMEOUT_UPDATE_CHECK,
)

logger = logging.getLogger(__name__)


def push(
    device: "Device",
    binfile: str,
    guard: Optional["Guard"] = None,
) -> bool:
    """OTA-push a local firmware binary to the device via POST /api/update/push.

    Returns True when the device rebooted and came back up; False on failure.
    """
    c = Client(device.ip, device.port)

    if guard is not None:
        from .safety import Guard as _Guard
        result = guard.check(device, "POST", "/api/update/push")
        if _Guard.is_dry_run_skip(result):
            return True  # dry-run: report success without doing anything

    data = open(binfile, "rb").read()
    pre_up = _get_uptime(c)
    logger.info("push %s -> %s (%d bytes)", binfile, device.ip, len(data))

    status, _ = c.request("POST", "/api/update/push", body=data, timeout=TIMEOUT_OTA_PUSH)
    # status None = connection reset mid-response (device rebooted) — expected
    if status not in (200, 202, None):
        logger.error("push rejected HTTP %s on %s", status, device.ip)
        return False

    time.sleep(8)  # wait for reboot to start
    for _ in range(24):
        cur_up = _get_uptime(c)
        if cur_up is not None and (pre_up is None or cur_up < pre_up):
            logger.info(
                "push complete: %s rebooted (uptime %s -> %s ms)",
                device.ip, pre_up, cur_up,
            )
            return True
        time.sleep(5)

    logger.error("push: no reboot detected on %s", device.ip)
    return False


def pull(
    device: "Device",
    guard: Optional["Guard"] = None,
    expected_version: Optional[str] = None,
) -> Optional[str]:
    """Trigger an OTA pull (update-check + apply) on the device.

    Returns the version string on success, None on failure.
    If the device is already on a release version and no update is available,
    returns the current version (not a failure).
    """
    c = Client(device.ip, device.port)

    if guard is not None:
        from .safety import Guard as _Guard
        result = guard.check(device, "POST", "/api/update/check")
        if _Guard.is_dry_run_skip(result):
            return expected_version or "dry-run"

    v0 = _get_version(c)
    c.request("POST", "/api/update/check", timeout=TIMEOUT_UPDATE_CHECK)

    target: Optional[str] = None
    for _ in range(20):
        s = c.get_json("/api/update/status") or {}
        if s.get("last_check_ok") and s.get("available"):
            target = s.get("latest")
            break
        time.sleep(3)

    if not target:
        s = c.get_json("/api/update/status") or {}
        if v0 and not v0.startswith("dev"):
            logger.info("%s: already on release %s, no update needed", device.ip, v0)
            return v0
        logger.error("%s: no update available (status=%s)", device.ip, s)
        return None

    logger.info("%s: pulling %s (from %s)", device.ip, target, v0)

    if guard is not None:
        from .safety import Guard as _Guard
        result = guard.check(device, "POST", "/api/update/apply")
        if _Guard.is_dry_run_skip(result):
            return target

    status, _ = c.request("POST", "/api/update/apply", timeout=TIMEOUT_WRITE)
    if status not in (200, 202):
        logger.error("%s: apply rejected (HTTP %s)", device.ip, status)
        return None

    return wait_for_boot(device, target_version=target)


def mark_valid(
    device: "Device",
    guard: Optional["Guard"] = None,
) -> bool:
    """Mark the currently-running OTA image as valid."""
    c = Client(device.ip, device.port)

    if guard is not None:
        from .safety import Guard as _Guard
        result = guard.check(device, "POST", "/api/update/mark-valid")
        if _Guard.is_dry_run_skip(result):
            return True

    status, _ = c.request("POST", "/api/update/mark-valid", timeout=TIMEOUT_WRITE)
    return status in (200, 204)


def wait_for_boot(
    device: "Device",
    target_version: str,
    timeout: float = 240,
) -> Optional[str]:
    """Poll until the device reports target_version (or timeout seconds pass).

    Returns the version string on success, None on timeout.
    """
    c = Client(device.ip, device.port)
    t0 = time.time()
    prev_version: Optional[str] = None

    while time.time() - t0 < timeout:
        v = _get_version(c)
        if v == target_version:
            return v
        if v and v != prev_version:
            logger.debug(
                "%s: version=%s (waiting for %s, %.0fs remaining)",
                device.ip, v, target_version, timeout - (time.time() - t0),
            )
            prev_version = v
        time.sleep(5)

    logger.error(
        "%s: timed out waiting for %s (currently %s)",
        device.ip, target_version, _get_version(c),
    )
    return None


def verify(device: "Device", target_version: str) -> bool:
    """Return True when device is running target_version and ota_validated=True."""
    c = Client(device.ip, device.port)
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    if info is None:
        return False
    return info.get("version") == target_version and bool(info.get("ota_validated"))


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _get_uptime(c: Client) -> Optional[int]:
    d = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    return d.get("uptime_ms") if d else None


def _get_version(c: Client) -> Optional[str]:
    d = c.get_json("/api/info", timeout=TIMEOUT_INFO)
    return d.get("version") if d else None
