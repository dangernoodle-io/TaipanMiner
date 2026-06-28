"""Tests for settle flag parsing in fleet.py _build_context and _ota_settle.

Covers:
  - no --settle flag  → SettleConfig(settle_delay=0, enabled=False)
  - --settle (bare)   → SettleConfig(settle_delay=criteria.settle_delay, enabled=True)
  - --settle N        → SettleConfig(settle_delay=N, enabled=True)
  - --no-settle is gone (not a recognised flag)
  - _ota_settle mirrors the same three cases
"""
import argparse
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import core as core_mod
from fleetlib.criteria import Criteria
from suites import SettleConfig


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_args(**kwargs):
    """Build a minimal argparse.Namespace for _build_context settle tests."""
    defaults = dict(
        settle=None,
        hosts=None,
        discover_timeout=10,
        board=None,
        fields=None,
        gates=[],
        skip_gates=[],
        out_json=None,
        out_junit=None,
        baseline=None,
        criteria=None,
        dry_run=False,
        yes=False,
        log_level="WARNING",
        subcommand="soak",
        func=None,
    )
    defaults.update(kwargs)
    return argparse.Namespace(**defaults)


def _build_context_settle(args) -> SettleConfig:
    """Call core.build_suite_context and return just the settle field."""
    ctx = core_mod.build_suite_context(args, name="soak")
    return ctx.settle


# ---------------------------------------------------------------------------
# _build_context settle resolution
# ---------------------------------------------------------------------------

class TestBuildContextSettle(unittest.TestCase):

    def test_no_flag_settle_disabled(self):
        """No --settle → settle disabled, delay 0."""
        args = _make_args(settle=None)
        settle = _build_context_settle(args)
        self.assertFalse(settle.enabled, "settle must be disabled when --settle not given")
        self.assertEqual(settle.settle_delay, 0)

    def test_bare_flag_uses_criteria_default(self):
        """--settle (bare) → enabled with criteria.settle_delay (120)."""
        args = _make_args(settle=core_mod.SETTLE_BARE)
        settle = _build_context_settle(args)
        self.assertTrue(settle.enabled)
        # criteria.settle_delay default is 120
        self.assertEqual(settle.settle_delay, Criteria().settle_delay)

    def test_explicit_seconds(self):
        """--settle 45 → enabled with settle_delay=45."""
        args = _make_args(settle=45)
        settle = _build_context_settle(args)
        self.assertTrue(settle.enabled)
        self.assertEqual(settle.settle_delay, 45)

    def test_explicit_zero(self):
        """--settle 0 → enabled with settle_delay=0 (explicit, not same as absent)."""
        args = _make_args(settle=0)
        settle = _build_context_settle(args)
        self.assertTrue(settle.enabled)
        self.assertEqual(settle.settle_delay, 0)


# ---------------------------------------------------------------------------
# _ota_settle
# ---------------------------------------------------------------------------

class TestOtaSettle(unittest.TestCase):

    def test_no_flag_disabled(self):
        """No --settle → SettleConfig disabled."""
        args = argparse.Namespace(settle=None)
        s = core_mod.ota_settle(args)
        self.assertFalse(s.enabled)

    def test_bare_flag_enabled_120(self):
        """--settle bare → enabled, delay 120."""
        args = argparse.Namespace(settle=core_mod.SETTLE_BARE)
        s = core_mod.ota_settle(args)
        self.assertTrue(s.enabled)
        self.assertEqual(s.settle_delay, 120)

    def test_explicit_seconds(self):
        """--settle 30 → enabled, delay 30."""
        args = argparse.Namespace(settle=30)
        s = core_mod.ota_settle(args)
        self.assertTrue(s.enabled)
        self.assertEqual(s.settle_delay, 30)


# ---------------------------------------------------------------------------
# Argparse: --no-settle must not be a recognised flag
# ---------------------------------------------------------------------------

class TestNoSettleRemoved(unittest.TestCase):

    def test_no_settle_flag_unrecognised(self):
        """--no-settle must not exist on the soak subparser."""
        p = argparse.ArgumentParser()
        # Replicate what add_common_flags adds (just the settle flag)
        core_mod.add_common_flags(p)
        with self.assertRaises(SystemExit):
            p.parse_args(["--no-settle"])


# ---------------------------------------------------------------------------
# SettleConfig dataclass defaults
# ---------------------------------------------------------------------------

class TestSettleConfigDefaults(unittest.TestCase):

    def test_default_disabled(self):
        """SettleConfig() default must be disabled (opt-in semantics)."""
        s = SettleConfig()
        self.assertFalse(s.enabled)
        self.assertEqual(s.settle_delay, 0)


if __name__ == "__main__":
    unittest.main()
