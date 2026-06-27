"""Safety guardrails for mutating fleet operations.

Guards against a stale-IP near-miss: a reassigned IP nearly caused an
OTA flash of the wrong board.
Every mutating operation must pass through Guard.check() before executing.
"""
from __future__ import annotations
import logging
from typing import Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from .discovery import Device

logger = logging.getLogger(__name__)

MUTATING: set = {"POST", "PUT", "PATCH", "DELETE"}

# Sentinel returned when a mutating call is skipped in dry-run mode.
# Callers check with Guard.is_dry_run_skip(result).
_SENTINEL = object()


class IdentityMismatch(Exception):
    """Device at the target IP is not the board we expected.

    Only raised when a KNOWN (non-None) identity is read and it differs from
    the expected board/hostname.  This is the real safety case.
    """


class DeviceUnreachable(Exception):
    """Device identity could not be read (unreachable / mid-reboot).

    Distinct from IdentityMismatch: the device returned no identity at all,
    so we cannot confirm OR deny board match.  Callers should skip/retry
    rather than treating this as a safety violation.
    """


class RefusedWithoutConfirmation(Exception):
    """Mutating operation refused because explicit confirmation was not provided."""


class Guard:
    """Safety gate applied before every mutating HTTP operation.

    Args:
        dry_run: Log intended operations but skip all actual mutating calls.
        confirm: True if the caller has obtained explicit user confirmation (--yes flag).
                 Required for any live mutating op.
        expect_board: Board string that must match /api/info before mutating.
        expect_hostname: Hostname that must match /api/info before mutating (optional).
    """

    def __init__(
        self,
        dry_run: bool = False,
        confirm: bool = False,
        expect_board: Optional[str] = None,
        expect_hostname: Optional[str] = None,
    ):
        self.dry_run = dry_run
        self.confirm = confirm
        self.expect_board = expect_board
        self.expect_hostname = expect_hostname

    def check(self, device: "Device", method: str, path: str) -> Optional[object]:
        """Validate prerequisites before a request.

        For read methods (GET, HEAD, OPTIONS): no-op, returns None.
        For mutating methods:
          1. Re-fetch /api/info and verify board/hostname identity.
             Raises IdentityMismatch if it doesn't match.
          2. In dry-run mode: log the intended call and return _SENTINEL.
          3. Without confirmation: raise RefusedWithoutConfirmation.
          4. Otherwise: return None (call may proceed).
        """
        method = method.upper()
        if method not in MUTATING:
            return None

        # Always re-verify identity before any destructive action.
        # Distinguish three outcomes:
        #   - device unreachable (info=None)     -> DeviceUnreachable (skip, not a safety violation)
        #   - known identity differs from expect -> IdentityMismatch  (real safety guard)
        #   - identity matches (or no expectation) -> proceed
        from .discovery import _read_identity
        board, hostname = _read_identity(device)
        if board is None and hostname is None:
            raise DeviceUnreachable(
                f"Could not read identity from {device.ip} (unreachable / mid-reboot). "
                f"Refusing {method} {path}."
            )
        if self.expect_board is not None and board != self.expect_board:
            raise IdentityMismatch(
                f"Identity mismatch for {device.ip} — "
                f"expected board={self.expect_board!r}, got board={board!r}. "
                f"Refusing {method} {path}."
            )
        if self.expect_hostname is not None and hostname != self.expect_hostname:
            raise IdentityMismatch(
                f"Identity mismatch for {device.ip} — "
                f"expected hostname={self.expect_hostname!r}, got hostname={hostname!r}. "
                f"Refusing {method} {path}."
            )

        if self.dry_run:
            logger.info(
                "[DRY-RUN] would %s http://%s:%d%s",
                method, device.ip, device.port, path,
            )
            return _SENTINEL

        if not self.confirm:
            raise RefusedWithoutConfirmation(
                f"Mutating op {method} {path} on {device.ip} requires explicit "
                f"confirmation (pass confirm=True / --yes flag)."
            )

        return None

    @staticmethod
    def is_dry_run_skip(result: object) -> bool:
        """True when check() returned the dry-run sentinel (call should be skipped)."""
        return result is _SENTINEL
