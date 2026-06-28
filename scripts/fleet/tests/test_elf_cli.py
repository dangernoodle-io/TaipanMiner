"""Offline unit tests for fleet.py elf / decode CLI commands.

Covers (mock client + store):
  - cmd_elf_archive: success, missing file
  - cmd_elf_list: empty store; with entries + in-use column
  - cmd_elf_prune: budget prune; dry-run; in-use GC + safety guards
    (grace-keep protection; partial-fleet conservatism)
  - cmd_decode: no panic / no ELF message; ELF found + decoded;
    --elf override; available=false + stale panic (graceful)
"""
import os
import sys
import tempfile
import hashlib
import json
from io import StringIO
from pathlib import Path
from unittest.mock import patch, MagicMock
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import commands.elf as elf_cmd
import commands.decode as decode_cmd


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_args(**kwargs):
    """Build a minimal argparse.Namespace-like object."""
    class NS:
        pass
    ns = NS()
    defaults = {
        "log_level": "WARNING",
        "hosts": None,
        "discover_timeout": 10,
        "board": None,
        "dry_run": False,
        "yes": False,
    }
    defaults.update(kwargs)
    for k, v in defaults.items():
        setattr(ns, k, v)
    return ns


def _fake_device(ip="192.0.2.1", board="esp32-wroom32", version="v1.0.0"):
    class D:
        pass
    d = D()
    d.ip = ip
    d.board = board
    d.version = version
    d.port = 80
    return d


def _make_elf(content: bytes, dirpath: Path) -> Path:
    p = dirpath / f"fw_{hashlib.md5(content).hexdigest()[:8]}.elf"
    p.write_bytes(content)
    return p


# ---------------------------------------------------------------------------
# cmd_elf_archive
# ---------------------------------------------------------------------------

class TestCmdElfArchive(unittest.TestCase):
    def test_archive_success(self, capsys=None):
        with tempfile.TemporaryDirectory() as td:
            data = b"fake_elf_archive_test"
            elf_path = Path(td) / "fw.elf"
            elf_path.write_bytes(data)
            store = Path(td) / "store"
            store.mkdir()
            args = _make_args(elf_path=str(elf_path), board="esp32-wroom32",
                              version="v2.0.0")
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    rc = elf_cmd.cmd_elf_archive(args)
            self.assertEqual(rc, 0)
            out = mock_out.getvalue()
            self.assertIn("Archived", out)
            self.assertIn("sha256", out)

    def test_archive_missing_file(self):
        args = _make_args(elf_path="/nonexistent/firmware.elf", board="", version="")
        with patch("sys.stdout", new_callable=StringIO):
            rc = elf_cmd.cmd_elf_archive(args)
        self.assertEqual(rc, 1)


# ---------------------------------------------------------------------------
# cmd_elf_list
# ---------------------------------------------------------------------------

class TestCmdElfList(unittest.TestCase):
    def test_empty_store(self):
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            args = _make_args()
            with patch("commands.elf.resolve_devices", return_value=[]):
                with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                    with patch("sys.stdout", new_callable=StringIO) as mock_out:
                        rc = elf_cmd.cmd_elf_list(args)
        self.assertEqual(rc, 0)
        self.assertIn("No archived", mock_out.getvalue())

    def test_list_with_entries_and_in_use(self):
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            data = b"elf_list_test"
            sha = hashlib.sha256(data).hexdigest()
            (store / f"{sha}.elf").write_bytes(data)
            (store / f"{sha}.json").write_text(json.dumps({
                "sha256": sha, "board": "esp32-wroom32", "version": "v1.0.0",
                "build_time": "", "git_sha": "", "dirty": False,
                "archived_at": "2024-01-01T00:00:00Z",
            }))
            # Mock a device that exposes the short sha via build.app_sha256 (B1-360)
            short_sha = sha[:9]
            mock_client = MagicMock()
            mock_client.get_json.return_value = {"build": {"app_sha256": short_sha}}
            device = _fake_device()
            with patch("commands.elf.resolve_devices", return_value=[device]):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                        with patch("sys.stdout", new_callable=StringIO) as mock_out:
                            rc = elf_cmd.cmd_elf_list(_make_args())
        self.assertEqual(rc, 0)
        out = mock_out.getvalue()
        self.assertIn(sha[:16], out)
        self.assertIn("esp32-wroom32", out)


# ---------------------------------------------------------------------------
# cmd_elf_prune
# ---------------------------------------------------------------------------

class TestCmdElfPrune(unittest.TestCase):
    def _populate_store(self, store: Path, n: int):
        keys = []
        for i in range(n):
            data = f"elf_prune_{i}".encode()
            sha = hashlib.sha256(data).hexdigest()
            (store / f"{sha}.elf").write_bytes(data)
            (store / f"{sha}.json").write_text(json.dumps({
                "sha256": sha, "board": "b", "version": "", "build_time": "",
                "git_sha": "", "dirty": False,
                "archived_at": f"2024-0{i+1:02d}-01T00:00:00Z",
            }))
            keys.append(sha)
        return keys

    def test_budget_prune_dry_run(self):
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            keys = self._populate_store(store, 5)
            args = _make_args(keep=3, max_age=None, in_use=False, grace_keep=0,
                              dry_run=True, yes=False)
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    rc = elf_cmd.cmd_elf_prune(args)
            # dry-run: should return 0
            self.assertEqual(rc, 0)
            out = mock_out.getvalue()
            self.assertIn("[DRY-RUN]", out)
            # all files still exist (no actual deletion in dry-run)
            for k in keys:
                self.assertTrue((store / f"{k}.elf").exists())

    def test_budget_prune_with_yes(self):
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            keys = self._populate_store(store, 5)
            args = _make_args(keep=3, max_age=None, in_use=False, grace_keep=0,
                              dry_run=False, yes=True)
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                with patch("sys.stdout", new_callable=StringIO):
                    rc = elf_cmd.cmd_elf_prune(args)
            self.assertEqual(rc, 0)
            remaining = [k for k in keys if (store / f"{k}.elf").exists()]
            self.assertEqual(len(remaining), 3)

    def test_nothing_to_prune(self):
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            self._populate_store(store, 2)
            args = _make_args(keep=20, max_age=None, in_use=False, grace_keep=0,
                              dry_run=False, yes=True)
            with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    rc = elf_cmd.cmd_elf_prune(args)
            self.assertEqual(rc, 0)
            self.assertIn("Nothing to prune", mock_out.getvalue())

    # --- in-use GC ---

    def test_in_use_gc_protects_running(self):
        """Entries matching a running device sha are NOT deleted."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            # 3 entries; device is running entry[1]
            keys = self._populate_store(store, 3)
            running_short = keys[1][:9]
            mock_client = MagicMock()
            mock_client.get_json.return_value = {"build": {"app_sha256": running_short}}
            device = _fake_device()
            args = _make_args(keep=1, max_age=None, in_use=True, grace_keep=0,
                              dry_run=False, yes=True)
            with patch("commands.elf.resolve_devices", return_value=[device]):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                        with patch("sys.stdout", new_callable=StringIO):
                            rc = elf_cmd.cmd_elf_prune(args)
            self.assertEqual(rc, 0)
            # running entry must survive
            self.assertTrue((store / f"{keys[1]}.elf").exists())

    def test_in_use_gc_safety_guard_1_grace_keep(self):
        """grace_keep most-recently archived entries are always protected."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            keys = self._populate_store(store, 5)
            # No device is running any of these
            mock_client = MagicMock()
            mock_client.get_json.return_value = {"build": {"app_sha256": ""}}
            device = _fake_device()
            args = _make_args(keep=1, max_age=None, in_use=True, grace_keep=3,
                              dry_run=False, yes=True)
            with patch("commands.elf.resolve_devices", return_value=[device]):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                        with patch("sys.stdout", new_callable=StringIO):
                            rc = elf_cmd.cmd_elf_prune(args)
            self.assertEqual(rc, 0)
            # grace_keep=3 → newest 3 (keys[2], keys[3], keys[4]) must survive
            for k in keys[2:]:
                self.assertTrue((store / f"{k}.elf").exists(),
                                f"grace-protected key {k[:8]} was deleted")

    def test_in_use_gc_safety_guard_2_partial_fleet(self):
        """Without --hosts, any unreachable device aborts the prune."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            self._populate_store(store, 3)
            # client returns None for first device (unreachable)
            mock_client = MagicMock()
            mock_client.get_json.return_value = None
            device = _fake_device()
            args = _make_args(keep=1, max_age=None, in_use=True, grace_keep=0,
                              dry_run=False, yes=True, hosts=None)
            with patch("commands.elf.resolve_devices", return_value=[device]):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                        with patch("sys.stdout", new_callable=StringIO) as mock_out:
                            rc = elf_cmd.cmd_elf_prune(args)
            # Should refuse prune due to unreachable device
            self.assertEqual(rc, 1)
            self.assertIn("unreachable", mock_out.getvalue().lower())

    def test_in_use_gc_with_explicit_hosts_allows_partial(self):
        """With --hosts, we proceed and warn about unreachable devices."""
        with tempfile.TemporaryDirectory() as td:
            store = Path(td)
            self._populate_store(store, 3)
            mock_client = MagicMock()
            mock_client.get_json.return_value = None  # all unreachable
            device = _fake_device()
            # --hosts supplied → authoritative set
            args = _make_args(keep=1, max_age=None, in_use=True, grace_keep=0,
                              dry_run=True, yes=True, hosts="192.0.2.1")
            with patch("commands.elf.resolve_devices", return_value=[device]):
                with patch("fleetlib.client.Client", return_value=mock_client):
                    with patch("fleetlib.elfstore._ARCHIVE_DEFAULT", store):
                        with patch("sys.stdout", new_callable=StringIO) as mock_out:
                            rc = elf_cmd.cmd_elf_prune(args)
            # Should warn but proceed (dry-run, rc=0)
            self.assertEqual(rc, 0)
            self.assertIn("unreachable", mock_out.getvalue().lower())


# ---------------------------------------------------------------------------
# cmd_decode
# ---------------------------------------------------------------------------

class TestCmdDecode(unittest.TestCase):
    def test_no_panic_available(self):
        """Device with available=False and no backtrace/pc -> graceful message."""
        mock_client = MagicMock()
        mock_client.get_json.side_effect = lambda path, **kw: (
            {"available": False, "app_sha256": "", "backtrace": [], "exc_pc": 0,
             "task": "", "exc_cause": 0}
            if "panic" in path else
            {"chip_model": "ESP32"}
        )
        args = _make_args(host="192.0.2.1", elf_path=None, toolchain_path=None)
        with patch("fleetlib.client.Client", return_value=mock_client):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                rc = decode_cmd.run(args)
        self.assertEqual(rc, 0)
        self.assertIn("no panic", mock_out.getvalue().lower())

    def test_no_elf_in_archive(self):
        """Panic with app_sha256 but no matching ELF -> error message."""
        mock_client = MagicMock()
        mock_client.get_json.side_effect = lambda path, **kw: (
            {"available": False, "app_sha256": "b268e2426", "backtrace": [0x40000000],
             "exc_pc": 0x40000001, "task": "main", "exc_cause": 28}
            if "panic" in path else
            {"chip_model": "ESP32"}
        )
        args = _make_args(host="192.0.2.1", elf_path=None, toolchain_path=None)
        with patch("fleetlib.client.Client", return_value=mock_client):
            with patch("fleetlib.elfstore.find", return_value=None):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    rc = decode_cmd.run(args)
        self.assertEqual(rc, 1)
        out = mock_out.getvalue()
        self.assertIn("b268e2426", out)
        self.assertIn("--elf", out)

    def test_unreachable_device(self):
        mock_client = MagicMock()
        mock_client.get_json.return_value = None
        args = _make_args(host="192.0.2.99", elf_path=None, toolchain_path=None)
        with patch("fleetlib.client.Client", return_value=mock_client):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                rc = decode_cmd.run(args)
        self.assertEqual(rc, 1)
        self.assertIn("could not reach", mock_out.getvalue())

    def test_decode_with_explicit_elf(self):
        """--elf override bypasses archive lookup and decodes."""
        mock_client = MagicMock()
        mock_client.get_json.side_effect = lambda path, **kw: (
            {"available": False, "app_sha256": "b268e2426",
             "backtrace": [0x40000010],
             "exc_pc": 0x40000000, "task": "main", "exc_cause": 28}
            if "panic" in path else
            {"chip_model": "ESP32"}
        )
        fake_result = MagicMock()
        fake_result.ok = True
        fake_result.task = "main"
        fake_result.exc_cause = 28
        fake_result.cause_name_str = "LoadProhibited"
        fake_result.app_sha256 = "b268e2426"
        fake_result.frames = [("exc_pc", 0x40000000, "fn @ /f.c:10"),
                               ("bt[0]", 0x40000010, "fn2 @ /g.c:20")]
        fake_result.error = ""
        with patch("fleetlib.client.Client", return_value=mock_client):
            with patch("fleetlib.decode.decode_panic", return_value=fake_result):
                with patch("sys.stdout", new_callable=StringIO) as mock_out:
                    args = _make_args(host="192.0.2.1", elf_path="/fake/fw.elf",
                                      toolchain_path=None)
                    rc = decode_cmd.run(args)
        self.assertEqual(rc, 0)
        out = mock_out.getvalue()
        self.assertIn("main", out)
        self.assertIn("LoadProhibited", out)
        self.assertIn("fn @", out)


# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main()
