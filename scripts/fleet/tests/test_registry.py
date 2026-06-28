"""Tests for registry.py — COMMANDS dict and PluginAPI."""
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import registry as _registry_module
from registry import PluginAPI


class TestPluginAPIAddCommand(unittest.TestCase):
    """PluginAPI.add_command registers a module into COMMANDS."""

    def setUp(self):
        # Snapshot and clear the registry so tests are isolated.
        self._original = dict(_registry_module.COMMANDS)
        _registry_module.COMMANDS.clear()

    def tearDown(self):
        _registry_module.COMMANDS.clear()
        _registry_module.COMMANDS.update(self._original)

    def _stub(self, name="test-cmd"):
        """Return a minimal command stub object."""
        class Stub:
            NAME = name
            HELP = f"help for {name}"
            def run(self, args):
                return 0
        return Stub()

    def test_add_command_registers_module(self):
        api = PluginAPI()
        stub = self._stub("noop")
        api.add_command("noop", stub)
        self.assertIn("noop", _registry_module.COMMANDS)
        self.assertIs(_registry_module.COMMANDS["noop"], stub)

    def test_collision_is_first_wins(self):
        api = PluginAPI()
        stub1 = self._stub("cmd-a")
        stub2 = self._stub("cmd-a")
        api.add_command("cmd-a", stub1)
        api.add_command("cmd-a", stub2)
        # First registration wins.
        self.assertIs(_registry_module.COMMANDS["cmd-a"], stub1)

    def test_collision_prints_warning_to_stderr(self):
        api = PluginAPI()
        stub1 = self._stub("cmd-b")
        stub2 = self._stub("cmd-b")
        api.add_command("cmd-b", stub1)
        buf = io.StringIO()
        with unittest.mock.patch("sys.stderr", buf):
            api.add_command("cmd-b", stub2)
        warning = buf.getvalue()
        self.assertIn("cmd-b", warning)
        self.assertIn("collision", warning)

    def test_multiple_different_commands_all_registered(self):
        api = PluginAPI()
        names = ["alpha", "beta", "gamma"]
        for n in names:
            api.add_command(n, self._stub(n))
        for n in names:
            self.assertIn(n, _registry_module.COMMANDS)


import unittest.mock  # ensure mock available (re-imported for test_collision)


class TestCommandsImport(unittest.TestCase):
    """COMMANDS is non-empty after importing commands (side-effect registration)."""

    def test_commands_populated_after_import(self):
        # commands/__init__.py registers built-in commands as a side-effect.
        # COMMANDS is shared state; just assert it's non-empty (without clearing it,
        # since other tests may rely on the real registry).
        import commands  # noqa: F401
        from registry import COMMANDS
        self.assertGreater(len(COMMANDS), 0)

    def test_known_builtin_commands_present(self):
        import commands  # noqa: F401
        from registry import COMMANDS
        for name in ("discover", "status", "ota", "elf"):
            self.assertIn(name, COMMANDS, f"expected built-in '{name}' in COMMANDS")


if __name__ == "__main__":
    unittest.main()
