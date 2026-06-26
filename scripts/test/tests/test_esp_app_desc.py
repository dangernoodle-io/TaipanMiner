"""Offline unit tests for elfstore.parse_esp_app_desc and archive metadata (TA-461).

Covers:
  - parse_esp_app_desc: extracts version, project_name, build_time, build_date
    from a minimal fixture ELF containing the magic struct
  - parse_esp_app_desc: returns None for data without the magic
  - archive: auto-populates board (project_name) and version when not supplied
  - archive: caller-supplied board/version take precedence over parsed values
  - cmd_elf_archive CLI: shows auto-populated annotation when values came from struct
"""
import os
import sys
import struct
import hashlib
import tempfile
import json
from io import StringIO
from pathlib import Path
from unittest.mock import patch
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.elfstore import parse_esp_app_desc, archive, AppDesc


# ---------------------------------------------------------------------------
# Fixture builder: minimal ELF-like bytes with embedded esp_app_desc_t
# ---------------------------------------------------------------------------

def _make_desc_bytes(
    version: str = "dev-test-abc1234",
    project_name: str = "taipanminer-esp32-wroom32",
    build_time: str = "12:34:56",
    build_date: str = "Jan  1 2026",
) -> bytes:
    """Build a minimal byte blob containing a valid esp_app_desc_t struct."""
    MAGIC = 0xABCD5432

    def _pad(s: str, length: int) -> bytes:
        b = s.encode("utf-8")[:length]
        return b.ljust(length, b"\x00")

    # Build the struct at the correct offsets (see elfstore.py module docstring)
    # total struct up to idf_ver end = 144 bytes; we use 256 for safety
    buf = bytearray(256)
    struct.pack_into("<I", buf, 0, MAGIC)       # magic_word
    struct.pack_into("<I", buf, 4, 0)            # secure_version
    # reserv1 (8 bytes) at 8 — zeros
    buf[16:48] = _pad(version, 32)              # version
    buf[48:80] = _pad(project_name, 32)         # project_name
    buf[80:96] = _pad(build_time, 16)           # time
    buf[96:112] = _pad(build_date, 16)          # date
    buf[112:144] = _pad("5.5.4", 32)            # idf_ver
    # app_elf_sha256 (32 bytes at 144) — zeros (filled by esptool, not here)

    # Wrap in some junk bytes before and after (simulates real ELF sections)
    prefix = b"\x7fELF" + b"\x00" * 4092      # 4096 bytes of junk before
    return prefix + bytes(buf)


# ---------------------------------------------------------------------------
# Tests: parse_esp_app_desc
# ---------------------------------------------------------------------------

class TestParseEspAppDesc(unittest.TestCase):
    def test_parses_version_and_project_name(self):
        data = _make_desc_bytes(
            version="dev-test-abc1234",
            project_name="taipanminer-esp32-wroom32",
        )
        desc = parse_esp_app_desc(data)
        self.assertIsNotNone(desc)
        self.assertEqual(desc.version, "dev-test-abc1234")
        self.assertEqual(desc.project_name, "taipanminer-esp32-wroom32")

    def test_parses_build_time_and_date(self):
        data = _make_desc_bytes(build_time="16:26:09", build_date="Jun 26 2026")
        desc = parse_esp_app_desc(data)
        self.assertIsNotNone(desc)
        self.assertEqual(desc.build_time, "16:26:09")
        self.assertEqual(desc.build_date, "Jun 26 2026")

    def test_returns_none_for_no_magic(self):
        data = b"\x00" * 512 + b"not-a-real-elf" + b"\x00" * 256
        desc = parse_esp_app_desc(data)
        self.assertIsNone(desc)

    def test_returns_none_for_empty(self):
        desc = parse_esp_app_desc(b"")
        self.assertIsNone(desc)

    def test_handles_truncated_struct(self):
        # Magic present but struct truncated — should return None
        magic_bytes = struct.pack("<I", 0xABCD5432)
        data = b"\x00" * 100 + magic_bytes + b"\x00" * 10  # too short after magic
        desc = parse_esp_app_desc(data)
        self.assertIsNone(desc)

    def test_strips_null_padding(self):
        # project_name shorter than 32 chars — trailing nulls stripped
        data = _make_desc_bytes(project_name="short")
        desc = parse_esp_app_desc(data)
        self.assertIsNotNone(desc)
        self.assertEqual(desc.project_name, "short")


# ---------------------------------------------------------------------------
# Tests: archive auto-populates from esp_app_desc_t (TA-461)
# ---------------------------------------------------------------------------

class TestArchiveMetadataAutoPopulate(unittest.TestCase):
    def _make_fixture_elf(self, dirpath: Path, **desc_kwargs) -> Path:
        data = _make_desc_bytes(**desc_kwargs)
        p = dirpath / "fw_fixture.elf"
        p.write_bytes(data)
        return p

    def test_archive_auto_populates_version_and_board(self):
        """When board/version not supplied, they come from esp_app_desc_t."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td) / "store"
            store.mkdir()
            elf_path = self._make_fixture_elf(
                Path(td),
                version="dev-ta461-test",
                project_name="taipanminer-esp32-s2-mini",
            )
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                key = archive(str(elf_path))

            sidecar = json.loads((store / f"{key}.json").read_text())
            self.assertEqual(sidecar["version"], "dev-ta461-test")
            self.assertEqual(sidecar["board"], "taipanminer-esp32-s2-mini")

    def test_archive_build_time_auto_populated(self):
        """build_time is set to 'date time' from esp_app_desc_t."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td) / "store"
            store.mkdir()
            elf_path = self._make_fixture_elf(
                Path(td),
                build_time="10:20:30",
                build_date="Dec 31 2025",
            )
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                key = archive(str(elf_path))

            sidecar = json.loads((store / f"{key}.json").read_text())
            self.assertIn("Dec 31 2025", sidecar["build_time"])
            self.assertIn("10:20:30", sidecar["build_time"])

    def test_caller_supplied_board_takes_precedence(self):
        """Explicit board arg overrides esp_app_desc_t project_name."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td) / "store"
            store.mkdir()
            elf_path = self._make_fixture_elf(
                Path(td), project_name="taipanminer-esp32-wroom32"
            )
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                key = archive(str(elf_path), board="custom-board")

            sidecar = json.loads((store / f"{key}.json").read_text())
            self.assertEqual(sidecar["board"], "custom-board")

    def test_caller_supplied_version_takes_precedence(self):
        """Explicit version arg overrides esp_app_desc_t version."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td) / "store"
            store.mkdir()
            elf_path = self._make_fixture_elf(
                Path(td), version="auto-version"
            )
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                key = archive(str(elf_path), version="v1.2.3-manual")

            sidecar = json.loads((store / f"{key}.json").read_text())
            self.assertEqual(sidecar["version"], "v1.2.3-manual")

    def test_archive_no_magic_leaves_fields_empty(self):
        """ELF without esp_app_desc magic -> board/version remain empty."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td) / "store"
            store.mkdir()
            elf_path = Path(td) / "plain.elf"
            elf_path.write_bytes(b"\x7fELF" + b"\x00" * 256)  # no magic
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                key = archive(str(elf_path))

            sidecar = json.loads((store / f"{key}.json").read_text())
            self.assertEqual(sidecar["board"], "")
            self.assertEqual(sidecar["version"], "")


# ---------------------------------------------------------------------------
# Tests: cmd_elf_archive CLI output
# ---------------------------------------------------------------------------

class TestCmdElfArchiveCLI(unittest.TestCase):
    def test_shows_auto_populated_annotation(self):
        """When board comes from esp_app_desc_t, the CLI notes it."""
        import fleet

        with tempfile.TemporaryDirectory() as td:
            store = Path(td) / "store"
            store.mkdir()
            data = _make_desc_bytes(
                version="dev-cli-test",
                project_name="taipanminer-esp32-wroom32",
            )
            elf_path = Path(td) / "fw.elf"
            elf_path.write_bytes(data)

            class NS:
                pass
            args = NS()
            args.elf_path = str(elf_path)
            args.board = ""
            args.version = ""

            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    rc = fleet.cmd_elf_archive(args)

        out = mock_out.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("taipanminer-esp32-wroom32", out)
        self.assertIn("dev-cli-test", out)
        self.assertIn("auto-populated", out)


if __name__ == "__main__":
    unittest.main()
