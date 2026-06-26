"""Safety guardrails for mutating fleet operations.

Guards against the stale-IP near-miss documented in AUDIT §2f:
a reassigned IP nearly caused an OTA flash of the wrong board.
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
    """Device at the target IP is not the board we expected."""


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

        # Always re-verify identity before any destructive action
        from .discovery import verify_identity
        if not verify_identity(
            device,
            expect_board=self.expect_board,
            expect_hostname=self.expect_hostname,
        ):
            raise IdentityMismatch(
                f"Identity check failed for {device.ip} — "
                f"expected board={self.expect_board!r} hostname={self.expect_hostname!r}. "
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
