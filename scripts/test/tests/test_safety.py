"""Tests for fleetlib.safety — method classification, dry-run, identity mismatch."""
import os
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.discovery import Device
from fleetlib.safety import (
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
        # GET must not trigger verify_identity; no patch needed
        result = g.check(dev, "GET", "/api/info")
        self.assertIsNone(result)
        self.assertFalse(Guard.is_dry_run_skip(result))


class TestGuardDryRun(unittest.TestCase):
    def test_dry_run_returns_sentinel(self):
        g = Guard(dry_run=True, confirm=True, expect_board="test-board")
        dev = _dev()
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            result = g.check(dev, "POST", "/api/update/push")
        self.assertTrue(Guard.is_dry_run_skip(result))

    def test_dry_run_case_insensitive_method(self):
        g = Guard(dry_run=True, confirm=True, expect_board="test-board")
        dev = _dev()
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            result = g.check(dev, "post", "/api/update/push")
        self.assertTrue(Guard.is_dry_run_skip(result))

    def test_dry_run_delete(self):
        g = Guard(dry_run=True, confirm=False, expect_board="test-board")
        dev = _dev()
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            result = g.check(dev, "DELETE", "/api/reset")
        self.assertTrue(Guard.is_dry_run_skip(result))


class TestGuardIdentityMismatch(unittest.TestCase):
    def test_mismatch_raises(self):
        g = Guard(dry_run=False, confirm=True, expect_board="wrong-board")
        dev = _dev()
        with patch("fleetlib.discovery.verify_identity", return_value=False):
            with self.assertRaises(IdentityMismatch):
                g.check(dev, "POST", "/api/update/push")

    def test_match_ok(self):
        g = Guard(dry_run=False, confirm=True, expect_board="test-board")
        dev = _dev()
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            result = g.check(dev, "POST", "/api/update/push")
        self.assertIsNone(result)


class TestGuardConfirmation(unittest.TestCase):
    def test_no_confirm_raises(self):
        g = Guard(dry_run=False, confirm=False, expect_board="test-board")
        dev = _dev()
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            with self.assertRaises(RefusedWithoutConfirmation):
                g.check(dev, "POST", "/api/update/push")

    def test_confirm_true_passes(self):
        g = Guard(dry_run=False, confirm=True, expect_board="test-board")
        dev = _dev()
        with patch("fleetlib.discovery.verify_identity", return_value=True):
            result = g.check(dev, "POST", "/api/update/push")
        self.assertIsNone(result)


class TestGuardNoExpectations(unittest.TestCase):
    def test_no_expect_board_still_calls_verify(self):
        g = Guard(dry_run=False, confirm=True)
        dev = _dev()
        # verify_identity with no expectations should return True
        with patch("fleetlib.discovery.verify_identity", return_value=True) as mock_vi:
            g.check(dev, "PATCH", "/api/settings")
        mock_vi.assert_called_once()


if __name__ == "__main__":
    unittest.main()
