"""Tests for fleetlib.profiles — board-class detection."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.profiles import profile_for, Profiles

_PROFILES_YAML = os.path.join(os.path.dirname(__file__), "..", "config", "profiles.yaml")


class TestProfileFor(unittest.TestCase):
    # ASIC boards
    def test_bitaxe_403(self):
        p = profile_for("bitaxe-403")
        self.assertTrue(p.is_asic)
        self.assertFalse(p.has_psram)
        self.assertEqual(p.vcore_floor_mv, 500)
        self.assertLessEqual(p.max_concurrent, 2)

    def test_bitaxe_601(self):
        p = profile_for("bitaxe-601")
        self.assertTrue(p.is_asic)

    def test_bitaxe_650(self):
        p = profile_for("bitaxe-650")
        self.assertTrue(p.is_asic)

    # no-PSRAM SW-miner boards
    def test_wroom32(self):
        p = profile_for("esp32-wroom32")
        self.assertFalse(p.is_asic)
        self.assertFalse(p.has_psram)
        self.assertLessEqual(p.max_concurrent, 2)

    def test_tdongle_s3(self):
        p = profile_for("tdongle-s3")
        self.assertFalse(p.is_asic)
        self.assertFalse(p.has_psram)

    # C3 — single-core, small heap
    def test_c3_supermini(self):
        p = profile_for("esp32-c3-supermini")
        self.assertFalse(p.is_asic)
        self.assertFalse(p.has_psram)
        self.assertEqual(p.heap_floor, 30_000)
        self.assertEqual(p.max_concurrent, 1)

    # S2 — single-core, no PSRAM
    def test_s2_mini(self):
        p = profile_for("esp32-s2-mini")
        self.assertFalse(p.is_asic)
        self.assertFalse(p.has_psram)

    def test_s2_mini_yaml_heap_floor(self):
        """esp32-s2 entry in profiles.yaml must set heap_floor=8000 (TA-460)."""
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("pyyaml not installed")
        profiles = Profiles.load(_PROFILES_YAML)
        p = profile_for("esp32-s2-mini", profiles=profiles)
        self.assertEqual(p.heap_floor, 8000)

    def test_s2_mini_yaml_readiness_heap_floor(self):
        """esp32-s2 entry in profiles.yaml must set readiness_heap_floor=8000 (TA-484)."""
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("pyyaml not installed")
        profiles = Profiles.load(_PROFILES_YAML)
        p = profile_for("esp32-s2-mini", profiles=profiles)
        self.assertEqual(p.readiness_heap_floor, 8000)

    def test_c3_yaml_readiness_heap_floor(self):
        """esp32-c3 entry in profiles.yaml must set readiness_heap_floor=30000 (TA-484)."""
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("pyyaml not installed")
        profiles = Profiles.load(_PROFILES_YAML)
        p = profile_for("esp32-c3-supermini", profiles=profiles)
        self.assertEqual(p.readiness_heap_floor, 30000)

    def test_wroom32_yaml_readiness_heap_floor(self):
        """esp32-wroom32 entry in profiles.yaml must set readiness_heap_floor=30000 (TA-484)."""
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("pyyaml not installed")
        profiles = Profiles.load(_PROFILES_YAML)
        p = profile_for("esp32-wroom32", profiles=profiles)
        self.assertEqual(p.readiness_heap_floor, 30000)

    def test_tdongle_yaml_readiness_heap_floor(self):
        """tdongle-s3 entry in profiles.yaml must set readiness_heap_floor=30000 (TA-484)."""
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("pyyaml not installed")
        profiles = Profiles.load(_PROFILES_YAML)
        p = profile_for("tdongle-s3", profiles=profiles)
        self.assertEqual(p.readiness_heap_floor, 30000)

    def test_bitaxe_yaml_readiness_heap_floor_absent(self):
        """bitaxe entry in profiles.yaml must NOT set readiness_heap_floor (keeps global default) (TA-484)."""
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("pyyaml not installed")
        profiles = Profiles.load(_PROFILES_YAML)
        p = profile_for("bitaxe-403", profiles=profiles)
        self.assertIsNone(p.readiness_heap_floor)

    def test_profile_dataclass_readiness_fields_default_none(self):
        """Profile readiness_* fields default to None (TA-484)."""
        from fleetlib.profiles import Profile
        p = Profile(board="test-board")
        self.assertIsNone(p.readiness_heap_floor)
        self.assertIsNone(p.readiness_hashrate_min)
        self.assertIsNone(p.readiness_vcore_floor)

    # S3 — dual-core, has PSRAM
    def test_s3_devkit(self):
        p = profile_for("esp32-s3-devkit")
        self.assertFalse(p.is_asic)
        self.assertTrue(p.has_psram)

    # Unknown board
    def test_unknown_board(self):
        p = profile_for("some-unknown-board-xyz")
        self.assertFalse(p.is_asic)
        self.assertFalse(p.has_psram)

    # Case insensitivity
    def test_case_insensitive(self):
        p1 = profile_for("Bitaxe-403")
        p2 = profile_for("bitaxe-403")
        self.assertEqual(p1.is_asic, p2.is_asic)


if __name__ == "__main__":
    unittest.main()
