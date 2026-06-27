"""Tests for fleetlib.safety — method classification, dry-run, identity mismatch."""
import os
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device
from fleetlib.safety import (
    DeviceUnreachable,
    Guard,
    IdentityMismatch,
    MUTATING,
    RefusedWithoutConfirmation,
)


def _dev(board: str = "test-board") -> Device:
    return Device(
        hostname="test-host",
        ip="192.0.2.1",
        port=80,
        board=board,
        version="v1.0.0",
    )


# Helper: patch _read_identity to return a specific (board, hostname) tuple.
def _patch_identity(board="test-board", hostname="test-host"):
    return patch("fleetlib.discovery._read_identity", return_value=(board, hostname))


class TestMutatingSet(unittest.TestCase):
    def test_post_is_mutating(self):
        self.assertIn("POST", MUTATING)

    def test_put_is_mutating(self):
        self.assertIn("PUT", MUTATING)

    def test_patch_is_mutating(self):
        self.assertIn("PATCH", MUTATING)

    def test_delete_is_mutating(self):
        self.assertIn("DELETE", MUTATING)

    def test_get_not_mutating(self):
        self.assertNotIn("GET", MUTATING)

    def test_head_not_mutating(self):
        self.assertNotIn("HEAD", MUTATING)


class TestGuardGetPassThrough(unittest.TestCase):
    def test_get_no_identity_check(self):
        g = Guard(dry_run=True, confirm=False, expect_board="wrong-board")
        dev = _dev()
        # GET must not trigger _read_identity; no patch needed
        result = g.check(dev, "GET", "/api/info")
        self.assertIsNone(result)
        self.assertFalse(Guard.is_dry_run_skip(result))


class TestGuardDryRun(unittest.TestCase):
    def test_dry_run_returns_sentinel(self):
        g = Guard(dry_run=True, confirm=True, expect_board="test-board")
        dev = _dev()
        with _patch_identity("test-board", "test-host"):
            result = g.check(dev, "POST", "/api/update/push")
        self.assertTrue(Guard.is_dry_run_skip(result))

    def test_dry_run_case_insensitive_method(self):
        g = Guard(dry_run=True, confirm=True, expect_board="test-board")
        dev = _dev()
        with _patch_identity("test-board", "test-host"):
            result = g.check(dev, "post", "/api/update/push")
        self.assertTrue(Guard.is_dry_run_skip(result))

    def test_dry_run_delete(self):
        g = Guard(dry_run=True, confirm=False, expect_board="test-board")
        dev = _dev()
        with _patch_identity("test-board", "test-host"):
            result = g.check(dev, "DELETE", "/api/reset")
        self.assertTrue(Guard.is_dry_run_skip(result))


# ---------------------------------------------------------------------------
# TA-475: DeviceUnreachable classification
# ---------------------------------------------------------------------------

class TestGuardDeviceUnreachable(unittest.TestCase):
    """Unreadable identity (board=None, hostname=None) -> DeviceUnreachable, not IdentityMismatch."""

    def test_unreachable_raises_device_unreachable(self):
        """(None, None) identity must raise DeviceUnreachable."""
        g = Guard(dry_run=False, confirm=True, expect_board="test-board")
        dev = _dev()
        with patch("fleetlib.discovery._read_identity", return_value=(None, None)):
            with self.assertRaises(DeviceUnreachable):
                g.check(dev, "POST", "/api/update/push")

    def test_unreachable_does_not_raise_identity_mismatch(self):
        """(None, None) identity must NOT raise IdentityMismatch."""
        g = Guard(dry_run=False, confirm=True, expect_board="test-board")
        dev = _dev()
        with patch("fleetlib.discovery._read_identity", return_value=(None, None)):
            try:
                g.check(dev, "POST", "/api/update/push")
            except DeviceUnreachable:
                pass  # correct
            except IdentityMismatch:
                self.fail("unreachable identity raised IdentityMismatch instead of DeviceUnreachable")

    def test_unreachable_no_expect_still_raises_device_unreachable(self):
        """Unreachable with no expectations still raises DeviceUnreachable (can't proceed safely)."""
        g = Guard(dry_run=False, confirm=True)
        dev = _dev()
        with patch("fleetlib.discovery._read_identity", return_value=(None, None)):
            with self.assertRaises(DeviceUnreachable):
                g.check(dev, "POST", "/api/update/push")


# ---------------------------------------------------------------------------
# TA-475: IdentityMismatch only for genuinely differing known identity
# ---------------------------------------------------------------------------

class TestGuardIdentityMismatch(unittest.TestCase):
    def test_mismatch_raises_when_known_board_differs(self):
        """Genuinely differing board -> IdentityMismatch (real safety guard)."""
        g = Guard(dry_run=False, confirm=True, expect_board="expected-board")
        dev = _dev()
        with patch("fleetlib.discovery._read_identity", return_value=("other-board", "test-host")):
            with self.assertRaises(IdentityMismatch):
                g.check(dev, "POST", "/api/update/push")

    def test_mismatch_raises_when_known_hostname_differs(self):
        """Genuinely differing hostname -> IdentityMismatch."""
        g = Guard(dry_run=False, confirm=True,
                  expect_board="test-board", expect_hostname="expected-host")
        dev = _dev()
        with patch("fleetlib.discovery._read_identity", return_value=("test-board", "other-host")):
            with self.assertRaises(IdentityMismatch):
                g.check(dev, "POST", "/api/update/push")

    def test_match_ok(self):
        """Matching known identity -> no exception, returns None."""
        g = Guard(dry_run=False, confirm=True, expect_board="test-board")
        dev = _dev()
        with _patch_identity("test-board", "test-host"):
            result = g.check(dev, "POST", "/api/update/push")
        self.assertIsNone(result)


class TestGuardConfirmation(unittest.TestCase):
    def test_no_confirm_raises(self):
        g = Guard(dry_run=False, confirm=False, expect_board="test-board")
        dev = _dev()
        with _patch_identity("test-board", "test-host"):
            with self.assertRaises(RefusedWithoutConfirmation):
                g.check(dev, "POST", "/api/update/push")

    def test_confirm_true_passes(self):
        g = Guard(dry_run=False, confirm=True, expect_board="test-board")
        dev = _dev()
        with _patch_identity("test-board", "test-host"):
            result = g.check(dev, "POST", "/api/update/push")
        self.assertIsNone(result)


class TestGuardNoExpectations(unittest.TestCase):
    def test_no_expect_board_still_calls_read_identity(self):
        g = Guard(dry_run=False, confirm=True)
        dev = _dev()
        with patch("fleetlib.discovery._read_identity", return_value=("test-board", "test-host")) as mock_ri:
            g.check(dev, "PATCH", "/api/settings")
        mock_ri.assert_called_once()


if __name__ == "__main__":
    unittest.main()
