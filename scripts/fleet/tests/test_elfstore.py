"""Offline unit tests for fleetlib.elfstore.

Covers:
  - sha256_of_elf: correct hash computation
  - archive: file is copied, sidecar written, returns full sha256
  - archive: idempotent (same sha -> no second copy, sidecar updated)
  - list_entries: correct sort (oldest first)
  - find: prefix match success + no-match
  - prune: count budget (keep N)
  - prune: max_age budget
  - prune: protected_shas are never deleted
  - prune: dry_run doesn't delete
  - prune-on-write budget enforced after archive()
"""
import os
import sys
import tempfile
import time
import hashlib
import json
from pathlib import Path
from unittest.mock import patch
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib import elfstore
from fleetlib.elfstore import (
    sha256_of_elf, archive, list_entries, find, prune,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_elf(content: bytes, dir: Path) -> Path:
    """Write a fake ELF file with given content; return its path."""
    p = dir / f"fw_{hashlib.md5(content).hexdigest()[:8]}.elf"
    p.write_bytes(content)
    return p


def _sha(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


# ---------------------------------------------------------------------------
# sha256_of_elf
# ---------------------------------------------------------------------------

class TestSha256OfElf(unittest.TestCase):
    def test_known_hash(self):
        with tempfile.TemporaryDirectory() as td:
            data = b"hello world"
            p = Path(td) / "fw.elf"
            p.write_bytes(data)
            result = sha256_of_elf(str(p))
            self.assertEqual(result, hashlib.sha256(data).hexdigest())

    def test_empty_elf(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "empty.elf"
            p.write_bytes(b"")
            result = sha256_of_elf(str(p))
            self.assertEqual(result, hashlib.sha256(b"").hexdigest())


# ---------------------------------------------------------------------------
# archive + list
# ---------------------------------------------------------------------------

class TestArchive(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.store = Path(self.td.name)

    def tearDown(self):
        self.td.cleanup()

    def test_archive_basic(self):
        data = b"fake_elf_content_1"
        elf = _make_elf(data, self.store)
        key = archive(str(elf), board="esp32-wroom32", version="v1.0.0",
                      store_dir=self.store, keep=0)
        self.assertEqual(key, _sha(data))
        self.assertTrue((self.store / f"{key}.elf").exists())
        self.assertTrue((self.store / f"{key}.json").exists())
        meta_raw = json.loads((self.store / f"{key}.json").read_text())
        self.assertEqual(meta_raw["board"], "esp32-wroom32")
        self.assertEqual(meta_raw["version"], "v1.0.0")

    def test_archive_idempotent(self):
        data = b"fake_elf_content_idempotent"
        elf = _make_elf(data, self.store)
        key1 = archive(str(elf), board="a", store_dir=self.store, keep=0)
        key2 = archive(str(elf), board="b", store_dir=self.store, keep=0)
        self.assertEqual(key1, key2)
        # Sidecar updated with new board
        meta_raw = json.loads((self.store / f"{key1}.json").read_text())
        self.assertEqual(meta_raw["board"], "b")

    def test_archive_missing_file(self):
        with self.assertRaises(FileNotFoundError):
            archive("/nonexistent/firmware.elf", store_dir=self.store, keep=0)


class TestListEntries(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.store = Path(self.td.name)

    def tearDown(self):
        self.td.cleanup()

    def test_empty_store(self):
        entries = list_entries(self.store)
        self.assertEqual(entries, [])

    def test_sorted_oldest_first(self):
        # Write two entries manually with controlled archived_at
        for i, ts in enumerate(["2024-01-01T00:00:00Z", "2024-06-01T00:00:00Z"]):
            sha = f"a" * 63 + str(i)
            (self.store / f"{sha}.elf").write_bytes(b"x")
            (self.store / f"{sha}.json").write_text(
                json.dumps({"sha256": sha, "board": f"b{i}", "version": "",
                            "build_time": "", "git_sha": "", "dirty": False,
                            "archived_at": ts})
            )
        entries = list_entries(self.store)
        self.assertEqual(len(entries), 2)
        self.assertLess(entries[0][0].archived_at, entries[1][0].archived_at)

    def test_skips_missing_elf(self):
        sha = "b" * 64
        (self.store / f"{sha}.json").write_text(
            json.dumps({"sha256": sha, "board": "", "version": "",
                        "build_time": "", "git_sha": "", "dirty": False,
                        "archived_at": "2024-01-01T00:00:00Z"})
        )
        # No .elf file
        entries = list_entries(self.store)
        self.assertEqual(entries, [])


# ---------------------------------------------------------------------------
# find (prefix match)
# ---------------------------------------------------------------------------

class TestFind(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.store = Path(self.td.name)

    def tearDown(self):
        self.td.cleanup()

    def _put(self, key: str):
        (self.store / f"{key}.elf").write_bytes(b"x")
        (self.store / f"{key}.json").write_text(
            json.dumps({"sha256": key, "board": "", "version": "",
                        "build_time": "", "git_sha": "", "dirty": False,
                        "archived_at": "2024-01-01T00:00:00Z"})
        )

    def test_prefix_match(self):
        key = "b268e2426" + "0" * 55  # 64-char full key
        self._put(key)
        result = find("b268e2426", self.store)
        self.assertEqual(result, str(self.store / f"{key}.elf"))

    def test_no_match(self):
        key = "aabbccdd" + "0" * 56
        self._put(key)
        result = find("deadbeef1", self.store)
        self.assertIsNone(result)

    def test_full_key_match(self):
        key = "c" * 64
        self._put(key)
        result = find(key, self.store)
        self.assertEqual(result, str(self.store / f"{key}.elf"))

    def test_empty_store(self):
        result = find("b268e2426", self.store)
        self.assertIsNone(result)

    def test_case_insensitive(self):
        key = "B268E2426".lower() + "0" * 55
        self._put(key)
        # Upper-case query should still match
        result = find("B268E2426", self.store)
        self.assertEqual(result, str(self.store / f"{key}.elf"))


# ---------------------------------------------------------------------------
# prune
# ---------------------------------------------------------------------------

class TestPrune(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.store = Path(self.td.name)

    def tearDown(self):
        self.td.cleanup()

    def _put(self, key: str, archived_at: str = "2024-01-01T00:00:00Z"):
        (self.store / f"{key}.elf").write_bytes(b"x")
        (self.store / f"{key}.json").write_text(
            json.dumps({"sha256": key, "board": "", "version": "",
                        "build_time": "", "git_sha": "", "dirty": False,
                        "archived_at": archived_at})
        )

    def _make_keys(self, n: int) -> list:
        keys = []
        for i in range(n):
            k = f"{i:02x}" * 32  # 64-char key from repeating 2-hex digit
            # Ensure unique
            k = k[:64]
            keys.append(k)
        return keys

    def test_keep_budget(self):
        # 5 entries, keep=3 -> oldest 2 deleted
        keys = []
        for i in range(5):
            k = f"{i:02x}" * 32
            k = k[:64]
            ts = f"2024-0{i+1}-01T00:00:00Z"
            self._put(k, ts)
            keys.append(k)
        deleted = prune(keep=3, store_dir=self.store)
        self.assertEqual(len(deleted), 2)
        # Oldest two should be deleted
        for k in keys[:2]:
            self.assertFalse((self.store / f"{k}.elf").exists())
        # Newest three remain
        for k in keys[2:]:
            self.assertTrue((self.store / f"{k}.elf").exists())

    def test_max_age(self):
        import datetime
        now = datetime.datetime.now(datetime.timezone.utc)
        old_ts = (now - datetime.timedelta(days=10)).strftime("%Y-%m-%dT%H:%M:%SZ")
        new_ts = (now - datetime.timedelta(hours=1)).strftime("%Y-%m-%dT%H:%M:%SZ")
        old_key = "00" * 32
        new_key = "11" * 32
        self._put(old_key, old_ts)
        self._put(new_key, new_ts)
        # max_age = 5 days = 432000s; old entry (10 days) should be deleted
        deleted = prune(keep=0, max_age=5 * 86400, store_dir=self.store)
        self.assertIn(old_key, deleted)
        self.assertNotIn(new_key, deleted)

    def test_protected_shas_not_deleted(self):
        keys = []
        for i in range(5):
            k = f"{i:02x}" * 32
            k = k[:64]
            ts = f"2024-0{i+1}-01T00:00:00Z"
            self._put(k, ts)
            keys.append(k)
        # Protect oldest two
        protected = {keys[0], keys[1]}
        deleted = prune(keep=3, protected_shas=protected, store_dir=self.store)
        # Neither of the protected entries should be deleted
        for pk in protected:
            self.assertNotIn(pk, deleted)
            self.assertTrue((self.store / f"{pk}.elf").exists())

    def test_dry_run_no_delete(self):
        keys = []
        for i in range(5):
            k = f"{i:02x}" * 32
            k = k[:64]
            ts = f"2024-0{i+1}-01T00:00:00Z"
            self._put(k, ts)
            keys.append(k)
        deleted = prune(keep=3, store_dir=self.store, dry_run=True)
        self.assertEqual(len(deleted), 2)
        # All files still exist
        for k in keys:
            self.assertTrue((self.store / f"{k}.elf").exists())

    def test_nothing_to_prune(self):
        k = "aa" * 32
        self._put(k)
        deleted = prune(keep=20, store_dir=self.store)
        self.assertEqual(deleted, [])

    def test_prune_on_write(self):
        """archive() with keep=3 removes oldest entries after adding the 4th."""
        payloads = [f"elf_data_{i}".encode() for i in range(4)]
        with tempfile.TemporaryDirectory() as td2:
            elf_dir = Path(td2)
            for i, data in enumerate(payloads):
                elf_path = elf_dir / f"fw{i}.elf"
                elf_path.write_bytes(data)
                archive(str(elf_path), board=f"b{i}",
                        _archived_at=f"2024-0{i+1}-01T00:00:00Z",
                        store_dir=self.store, keep=3)
        remaining = list_entries(self.store)
        # Only 3 should remain
        self.assertEqual(len(remaining), 3)


# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main()
