"""Tests for cli.py — subparser construction and dispatch."""
import os
import sys
import unittest
from unittest.mock import patch, MagicMock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import registry as _registry_module
from cli import _build_cli_parser


class TestSubparsersMatchCommands(unittest.TestCase):
    """cli builds exactly one subparser per COMMANDS entry."""

    def test_each_command_has_a_subparser(self):
        import commands  # noqa: F401 — trigger side-effect registration
        parser = _build_cli_parser()
        # argparse stores subparsers in the _subparsers._group_actions structure.
        # The most portable check: assert parse_args([name, '--help']) exits 0
        # for every registered command name.
        for name in _registry_module.COMMANDS:
            with self.subTest(command=name):
                with self.assertRaises(SystemExit) as cm:
                    parser.parse_args([name, "--help"])
                self.assertEqual(cm.exception.code, 0,
                                 f"expected exit 0 for '{name} --help', got {cm.exception.code}")

    def test_unknown_command_exits_nonzero(self):
        parser = _build_cli_parser()
        with self.assertRaises(SystemExit) as cm:
            parser.parse_args(["__nonexistent_cmd__"])
        self.assertNotEqual(cm.exception.code, 0)


class TestDispatchCallsRun(unittest.TestCase):
    """cli.main() dispatches to the selected command's run() and propagates its return code."""

    def setUp(self):
        # Snapshot COMMANDS so we can inject a sentinel command.
        self._snapshot = dict(_registry_module.COMMANDS)

    def tearDown(self):
        _registry_module.COMMANDS.clear()
        _registry_module.COMMANDS.update(self._snapshot)

    def test_run_is_called_and_return_code_propagates(self):
        # Build a sentinel module with a monkeypatched run().
        sentinel_called = []

        class _SentinelCmd:
            NAME = "sentinel"
            HELP = "sentinel command for dispatch test"

            @staticmethod
            def add_arguments(p):
                pass  # no extra args

            @staticmethod
            def run(args):
                sentinel_called.append(args)
                return 42

        _registry_module.COMMANDS["sentinel"] = _SentinelCmd

        from cli import main
        with patch("sys.argv", ["fleet", "sentinel"]):
            # load_config returns {} (no fleet.toml needed here; patch it to be safe)
            with patch("cli.load_config", return_value={}):
                code = main()

        self.assertEqual(code, 42)
        self.assertEqual(len(sentinel_called), 1)

    def test_dispatch_picks_correct_command(self):
        """When two sentinel commands exist, the one named on argv is called."""
        called = {"a": False, "b": False}

        class _CmdA:
            NAME = "cmd-a"
            HELP = "cmd a"
            @staticmethod
            def add_arguments(p): pass
            @staticmethod
            def run(args):
                called["a"] = True
                return 0

        class _CmdB:
            NAME = "cmd-b"
            HELP = "cmd b"
            @staticmethod
            def add_arguments(p): pass
            @staticmethod
            def run(args):
                called["b"] = True
                return 0

        _registry_module.COMMANDS["cmd-a"] = _CmdA
        _registry_module.COMMANDS["cmd-b"] = _CmdB

        from cli import main
        with patch("sys.argv", ["fleet", "cmd-b"]):
            with patch("cli.load_config", return_value={}):
                main()

        self.assertFalse(called["a"])
        self.assertTrue(called["b"])


if __name__ == "__main__":
    unittest.main()
