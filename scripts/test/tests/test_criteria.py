"""Tests for fleetlib.criteria — defaults, YAML merge, profile override."""
import os
import sys
import json
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.criteria import Criteria, load, for_profile
from fleetlib.profiles import profile_for, Profiles

_PROFILES_YAML = os.path.join(os.path.dirname(__file__), "..", "config", "profiles.yaml")


class TestCriteriaDefaults(unittest.TestCase):
    def test_poll_interval(self):
        self.assertEqual(Criteria().poll_interval, 60.0)

    def test_duration(self):
        self.assertEqual(Criteria().duration, 3600.0)

    def test_heap_floor(self):
        self.assertEqual(Criteria().heap_floor, 50_000)

    def test_bad_reset_reasons(self):
        rr = Criteria().bad_reset_reasons
        for r in ("panic", "task_wdt", "int_wdt", "brownout"):
            self.assertIn(r, rr)

    def test_publisher_max_polls(self):
        self.assertEqual(Criteria().publisher_max_polls, 6)

    def test_max_missed_polls(self):
        self.assertEqual(Criteria().max_missed_polls, 4)

    def test_vcore_floor_mv(self):
        self.assertEqual(Criteria().vcore_floor_mv, 500)

    def test_hashrate_floor_pct(self):
        self.assertEqual(Criteria().hashrate_floor_pct, 80.0)


class TestCriteriaLoad(unittest.TestCase):
    def test_missing_file_returns_defaults(self):
        c = load("/nonexistent/criteria.yaml")
        self.assertEqual(c.poll_interval, 60.0)
        self.assertEqual(c.heap_floor, 50_000)

    def test_yaml_merge(self):
        try:
            import yaml
        except ImportError:
            self.skipTest("pyyaml not installed")
        content = "poll_interval: 30\nheap_floor: 40000\n"
        with tempfile.NamedTemporaryFile(
            suffix=".yaml", delete=False, mode="w"
        ) as f:
            f.write(content)
            path = f.name
        try:
            c = load(path)
            self.assertEqual(c.poll_interval, 30)
            self.assertEqual(c.heap_floor, 40_000)
            # unset fields keep defaults
            self.assertEqual(c.duration, 3600.0)
        finally:
            os.unlink(path)

    def test_bad_reset_reasons_yaml(self):
        try:
            import yaml
        except ImportError:
            self.skipTest("pyyaml not installed")
        content = "bad_reset_reasons: [panic, brownout]\n"
        with tempfile.NamedTemporaryFile(
            suffix=".yaml", delete=False, mode="w"
        ) as f:
            f.write(content)
            path = f.name
        try:
            c = load(path)
            self.assertEqual(c.bad_reset_reasons, {"panic", "brownout"})
        finally:
            os.unlink(path)


class TestCriteriaForProfile(unittest.TestCase):
    def test_asic_sets_poll_and_vcore(self):
        c = Criteria()
        p = profile_for("bitaxe-403")
        c2 = for_profile(c, p)
        # ASIC profile sets poll_interval=60 and vcore_floor_mv=500 (same as defaults)
        self.assertEqual(c2.poll_interval, 60.0)
        self.assertEqual(c2.vcore_floor_mv, 500)

    def test_c3_overrides_heap_floor(self):
        c = Criteria()
        p = profile_for("esp32-c3-supermini")
        c2 = for_profile(c, p)
        self.assertEqual(c2.heap_floor, 30_000)

    def test_wroom_no_heap_override(self):
        c = Criteria()
        p = profile_for("esp32-wroom32")
        c2 = for_profile(c, p)
        self.assertEqual(c2.heap_floor, 50_000)  # default preserved

    def test_for_profile_does_not_mutate_original(self):
        c = Criteria()
        p = profile_for("esp32-c3-supermini")
        _ = for_profile(c, p)
        self.assertEqual(c.heap_floor, 50_000)  # original untouched

    def test_bad_reset_reasons_deep_copied(self):
        c = Criteria()
        p = profile_for("esp32-wroom32")
        c2 = for_profile(c, p)
        c2.bad_reset_reasons.add("custom")
        self.assertNotIn("custom", c.bad_reset_reasons)

    def test_s2_heap_floor_from_yaml_profile(self):
        """for_profile with esp32-s2 YAML profile sets heap_floor=8000 (TA-460)."""
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("pyyaml not installed")
        profiles = Profiles.load(_PROFILES_YAML)
        p = profile_for("esp32-s2-mini", profiles=profiles)
        c = Criteria()
        c2 = for_profile(c, p)
        self.assertEqual(c2.heap_floor, 8000)
        self.assertEqual(c.heap_floor, 50_000)  # original untouched


if __name__ == "__main__":
    unittest.main()
