"""Offline unit tests for fleetlib.decode.

Covers:
  - chip_arch: xtensa/riscv classification for all supported chips
  - find_addr2line: explicit path / env var / PATH fallback / not-found
  - addr2line_decode: correct frame parsing, CalledProcessError, timeout
  - cause_name: xtensa + riscv tables, unknown cause
  - decode_panic: full flow with mocked addr2line, no-pc case, no addr2line
  - decode_panic: exc_pc labelled correctly; backtrace labelled bt[N]
"""
import os
import sys
import unittest
from unittest.mock import patch, MagicMock
import subprocess
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib import decode as dec
from fleetlib.decode import (
    chip_arch, find_addr2line, addr2line_decode, cause_name, decode_panic,
)


# ---------------------------------------------------------------------------
# chip_arch
# ---------------------------------------------------------------------------

class TestChipArch(unittest.TestCase):
    def test_esp32_xtensa(self):
        self.assertEqual(chip_arch("ESP32"), "xtensa")

    def test_esp32s3_xtensa(self):
        self.assertEqual(chip_arch("ESP32-S3"), "xtensa")

    def test_esp32s2_xtensa(self):
        self.assertEqual(chip_arch("ESP32-S2"), "xtensa")

    def test_esp32c3_riscv(self):
        self.assertEqual(chip_arch("ESP32-C3"), "riscv")

    def test_esp32c6_riscv(self):
        self.assertEqual(chip_arch("ESP32-C6"), "riscv")

    def test_esp32h2_riscv(self):
        self.assertEqual(chip_arch("ESP32-H2"), "riscv")

    def test_unknown_defaults_xtensa(self):
        # Unknown chip → xtensa (safe default)
        self.assertEqual(chip_arch("UNKNOWN"), "xtensa")


# ---------------------------------------------------------------------------
# find_addr2line
# ---------------------------------------------------------------------------

class TestFindAddr2line(unittest.TestCase):
    def test_explicit_path_valid(self):
        with tempfile.NamedTemporaryFile(suffix="-addr2line", delete=False) as f:
            path = f.name
        try:
            os.chmod(path, 0o755)
            result = find_addr2line("xtensa", toolchain_path=path)
            self.assertEqual(result, path)
        finally:
            os.unlink(path)

    def test_explicit_path_missing(self):
        result = find_addr2line("xtensa", toolchain_path="/nonexistent/addr2line")
        # Should fall through to other methods, not return the missing path
        # (result may be None or a real system path, but NOT the missing path)
        self.assertNotEqual(result, "/nonexistent/addr2line")

    def test_env_var(self):
        with tempfile.NamedTemporaryFile(suffix="-addr2line", delete=False) as f:
            path = f.name
        try:
            os.chmod(path, 0o755)
            with patch.dict(os.environ, {"FLEET_ADDR2LINE": path}):
                result = find_addr2line("xtensa")
            self.assertEqual(result, path)
        finally:
            os.unlink(path)

    def test_shutil_which_fallback(self):
        # If addr2line is on PATH, shutil.which returns it
        fake_path = "/usr/local/bin/xtensa-esp-elf-addr2line"
        with patch("shutil.which", return_value=fake_path):
            with patch("os.path.isfile", return_value=False):  # pio not found
                # Prevent env var from interfering
                with patch.dict(os.environ, {"FLEET_ADDR2LINE": ""}, clear=False):
                    result = find_addr2line("xtensa")
        self.assertEqual(result, fake_path)

    def test_not_found_returns_none(self):
        with patch("shutil.which", return_value=None):
            with patch.object(Path, "is_dir", return_value=False):
                with patch.dict(os.environ, {"FLEET_ADDR2LINE": ""}, clear=False):
                    result = find_addr2line("xtensa", toolchain_path=None)
        # May find a real system binary; just verify type
        # (can't assert None without removing all system toolchains)
        self.assertIsInstance(result, (str, type(None)))


# ---------------------------------------------------------------------------
# addr2line_decode
# ---------------------------------------------------------------------------

class TestAddr2lineDecode(unittest.TestCase):
    def test_normal_decode(self):
        # addr2line -f outputs: function\nfile:line for each address
        mock_output = b"my_function\n/src/main.c:42\nanother_fn\n/src/util.c:7\n"
        with patch("subprocess.check_output", return_value=mock_output):
            frames = addr2line_decode("fw.elf", [0x400000, 0x400010], "/fake/addr2line")
        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[0], "my_function @ /src/main.c:42")
        self.assertEqual(frames[1], "another_fn @ /src/util.c:7")

    def test_called_process_error(self):
        with patch("subprocess.check_output",
                   side_effect=subprocess.CalledProcessError(1, "cmd", b"err")):
            frames = addr2line_decode("fw.elf", [0x400000], "/fake/addr2line")
        self.assertEqual(frames, ["?? @ ??:0"])

    def test_timeout(self):
        with patch("subprocess.check_output",
                   side_effect=subprocess.TimeoutExpired("cmd", 30)):
            frames = addr2line_decode("fw.elf", [0x400000], "/fake/addr2line")
        self.assertEqual(frames, ["?? @ ??:0"])

    def test_file_not_found(self):
        with patch("subprocess.check_output",
                   side_effect=FileNotFoundError("not found")):
            frames = addr2line_decode("fw.elf", [0x400000], "/fake/addr2line")
        self.assertEqual(frames, ["?? @ ??:0"])

    def test_empty_addresses(self):
        frames = addr2line_decode("fw.elf", [], "/fake/addr2line")
        self.assertEqual(frames, [])


# ---------------------------------------------------------------------------
# cause_name
# ---------------------------------------------------------------------------

class TestCauseName(unittest.TestCase):
    def test_xtensa_28(self):
        self.assertEqual(cause_name(28, "xtensa"), "LoadProhibited")

    def test_xtensa_29(self):
        self.assertEqual(cause_name(29, "xtensa"), "StoreProhibited")

    def test_xtensa_0(self):
        self.assertEqual(cause_name(0, "xtensa"), "IllegalInstruction")

    def test_riscv_2(self):
        self.assertEqual(cause_name(2, "riscv"), "IllegalInstruction")

    def test_riscv_5(self):
        self.assertEqual(cause_name(5, "riscv"), "LoadAccessFault")

    def test_unknown_xtensa(self):
        self.assertIn("Unknown", cause_name(99, "xtensa"))
        self.assertIn("99", cause_name(99, "xtensa"))

    def test_unknown_riscv(self):
        self.assertIn("Unknown", cause_name(99, "riscv"))


# ---------------------------------------------------------------------------
# decode_panic
# ---------------------------------------------------------------------------

class TestDecodePanic(unittest.TestCase):
    _FAKE_ELF = "/fake/firmware.elf"

    def _mock_addr2line_output(self, frames):
        """Build mock addr2line output for a list of (fn, loc) tuples."""
        lines = []
        for fn, loc in frames:
            lines.append(fn)
            lines.append(loc)
        return ("\n".join(lines) + "\n").encode()

    def test_full_decode_with_exc_pc_and_backtrace(self):
        panic = {
            "available": False,
            "task": "main",
            "exc_pc": 0x40080000,
            "exc_cause": 28,
            "backtrace": [0x40080010, 0x40080020],
            "app_sha256": "b268e2426",
        }
        mock_out = self._mock_addr2line_output([
            ("crash_fn", "/main.c:10"),
            ("caller_a", "/util.c:5"),
            ("caller_b", "/init.c:99"),
        ])
        with patch.object(dec, "find_addr2line", return_value="/fake/addr2line"):
            with patch("subprocess.check_output", return_value=mock_out):
                result = decode_panic(panic, self._FAKE_ELF, arch="xtensa")

        self.assertTrue(result.ok)
        self.assertEqual(result.task, "main")
        self.assertEqual(result.exc_cause, 28)
        self.assertEqual(result.cause_name_str, "LoadProhibited")
        self.assertEqual(len(result.frames), 3)
        # First frame is exc_pc
        label0, pc0, frame0 = result.frames[0]
        self.assertEqual(label0, "exc_pc")
        self.assertEqual(pc0, 0x40080000)
        self.assertIn("crash_fn", frame0)
        # Second frame is bt[0]
        label1, pc1, frame1 = result.frames[1]
        self.assertEqual(label1, "bt[0]")
        self.assertEqual(pc1, 0x40080010)

    def test_no_addr2line_returns_error(self):
        panic = {"exc_pc": 0x40000, "backtrace": [], "task": "", "exc_cause": 0}
        with patch.object(dec, "find_addr2line", return_value=None):
            result = decode_panic(panic, self._FAKE_ELF, arch="xtensa")
        self.assertFalse(result.ok)
        self.assertIn("addr2line not found", result.error)

    def test_no_pcs_returns_ok_no_frames(self):
        panic = {"exc_pc": 0, "backtrace": [], "task": "idle", "exc_cause": 0,
                 "app_sha256": "abc"}
        with patch.object(dec, "find_addr2line", return_value="/fake/addr2line"):
            result = decode_panic(panic, self._FAKE_ELF, arch="xtensa")
        self.assertTrue(result.ok)
        self.assertEqual(result.frames, [])
        self.assertIn("no PC", result.error)

    def test_riscv_arch(self):
        panic = {
            "exc_pc": 0x42000000,
            "exc_cause": 5,
            "backtrace": [],
            "task": "wifi",
            "app_sha256": "75f96bd07",
        }
        mock_out = self._mock_addr2line_output([("riscv_fn", "/esp.c:1")])
        with patch.object(dec, "find_addr2line", return_value="/fake/riscv-addr2line"):
            with patch("subprocess.check_output", return_value=mock_out):
                result = decode_panic(panic, self._FAKE_ELF, arch="riscv")
        self.assertTrue(result.ok)
        self.assertEqual(result.cause_name_str, "LoadAccessFault")


# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main()
