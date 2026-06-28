"""THE GATE: help-snapshot regression tests.

For every entry in help_snapshots/_MANIFEST.txt, run the CLI via subprocess
and assert stdout matches the fixture file byte-for-byte.  Locks CLI output
stability into the test suite.

Fixtures were captured with COLUMNS=80.  The manifest maps filename -> args
in the form:
    root.txt = fleet --help (COLUMNS=80)
    discover.txt = fleet discover --help (COLUMNS=80)
    ota-push.txt = fleet ota push --help (COLUMNS=80)
    ...

The test strips the trailing " (COLUMNS=80)" annotation, drops the leading
"fleet " prefix, and passes the remaining tokens as argv.
"""
from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path

_FLEET_DIR = Path(__file__).parent.parent
_SNAPSHOT_DIR = _FLEET_DIR / "tests" / "help_snapshots"
_MANIFEST_PATH = _SNAPSHOT_DIR / "_MANIFEST.txt"

# The CLI entry point to invoke.
_FLEET_PY = str(_FLEET_DIR / "fleet.py")


def _parse_manifest() -> list[tuple[str, list[str]]]:
    """Return [(fixture_filename, argv_tokens), ...] from _MANIFEST.txt.

    Each line has the form:
        <filename> = fleet [sub [sub]] --help (COLUMNS=80)

    We drop the leading 'fleet' and the trailing ' (COLUMNS=80)' annotation,
    then split into tokens.
    """
    entries: list[tuple[str, list[str]]] = []
    with open(_MANIFEST_PATH) as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            filename, _, rest = line.partition(" = ")
            filename = filename.strip()
            # Strip trailing "(COLUMNS=80)" annotation
            rest = rest.strip()
            if rest.endswith("(COLUMNS=80)"):
                rest = rest[: -len("(COLUMNS=80)")].rstrip()
            # rest is now "fleet discover --help" or "fleet --help" etc.
            # Drop the leading "fleet" token.
            tokens = rest.split()
            if tokens and tokens[0] == "fleet":
                tokens = tokens[1:]
            entries.append((filename, tokens))
    return entries


class TestHelpSnapshots(unittest.TestCase):
    """Assert live --help output matches committed fixtures byte-for-byte."""

    @classmethod
    def setUpClass(cls):
        cls._entries = _parse_manifest()

    def _run_help(self, argv: list[str]) -> str:
        """Run fleet.py with given argv and return stdout as a string."""
        env = dict(os.environ)
        env["COLUMNS"] = "80"
        result = subprocess.run(
            [sys.executable, _FLEET_PY] + argv,
            capture_output=True,
            text=True,
            cwd=str(_FLEET_DIR),
            env=env,
        )
        # --help always exits 0; surface stderr if it doesn't.
        self.assertEqual(
            result.returncode, 0,
            f"fleet {' '.join(argv)} exited {result.returncode}:\n{result.stderr}",
        )
        return result.stdout

    def test_snapshots(self):
        for filename, argv in self._entries:
            with self.subTest(fixture=filename, argv=argv):
                fixture_path = _SNAPSHOT_DIR / filename
                self.assertTrue(
                    fixture_path.exists(),
                    f"fixture file missing: {fixture_path}",
                )
                expected = fixture_path.read_text()
                actual = self._run_help(argv)
                self.assertEqual(
                    actual,
                    expected,
                    f"help output mismatch for '{' '.join(argv)}'.\n"
                    f"Re-capture with: COLUMNS=80 python fleet.py {' '.join(argv)} > "
                    f"tests/help_snapshots/{filename}",
                )


if __name__ == "__main__":
    unittest.main()
