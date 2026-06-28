"""Tests for core.load_plugins and the plugin mechanism."""
import os
import sys
import tempfile
import textwrap
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import registry as _registry_module
from core import load_plugins
from registry import PluginAPI


class _RegistryFixture(unittest.TestCase):
    """Mixin that snapshots/restores COMMANDS around each test."""

    def setUp(self):
        self._snapshot = dict(_registry_module.COMMANDS)

    def tearDown(self):
        _registry_module.COMMANDS.clear()
        _registry_module.COMMANDS.update(self._snapshot)


class TestLoadPluginsSuccess(_RegistryFixture):
    """A valid plugin with register(api) has its command registered."""

    def _write_plugin(self, tmpdir, name="plugin_noop", cmd_name="plugin-noop"):
        """Write a minimal plugin file and return its path."""
        src = textwrap.dedent(f"""\
            class _NoopCmd:
                NAME = "{cmd_name}"
                HELP = "noop plugin command"
                def run(self, args):
                    return 0

            def register(api):
                api.add_command("{cmd_name}", _NoopCmd())
        """)
        path = os.path.join(tmpdir, f"{name}.py")
        with open(path, "w") as fh:
            fh.write(src)
        return path

    def test_plugin_command_lands_in_commands(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            plugin_path = self._write_plugin(tmpdir)
            api = PluginAPI()
            load_plugins([plugin_path], tmpdir, api)
            self.assertIn("plugin-noop", _registry_module.COMMANDS)

    def test_relative_path_resolved_from_config_dir(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            self._write_plugin(tmpdir, name="rel_plugin", cmd_name="rel-noop")
            api = PluginAPI()
            # Relative path: just the filename; config_dir = tmpdir
            load_plugins(["rel_plugin.py"], tmpdir, api)
            self.assertIn("rel-noop", _registry_module.COMMANDS)


class TestLoadPluginsFailures(_RegistryFixture):
    """Failure modes: import error, missing register — both are non-fatal."""

    def test_import_error_is_nonfatal(self):
        """A plugin that raises on import should not propagate the exception."""
        with tempfile.TemporaryDirectory() as tmpdir:
            bad_path = os.path.join(tmpdir, "bad_plugin.py")
            with open(bad_path, "w") as fh:
                fh.write("raise RuntimeError('intentional import failure')\n")
            api = PluginAPI()
            # Must not raise; load_plugins should warn and continue.
            try:
                load_plugins([bad_path], tmpdir, api)
            except Exception as exc:
                self.fail(f"load_plugins raised unexpectedly: {exc}")

    def test_plugin_without_register_is_skipped(self):
        """A plugin without a register() function is silently skipped."""
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "no_register.py")
            with open(path, "w") as fh:
                fh.write("# no register function\nX = 42\n")
            api = PluginAPI()
            before = dict(_registry_module.COMMANDS)
            load_plugins([path], tmpdir, api)
            # COMMANDS must be unchanged.
            self.assertEqual(_registry_module.COMMANDS, before)

    def test_nonexistent_path_is_nonfatal(self):
        """A path that doesn't exist should be handled without raising."""
        api = PluginAPI()
        try:
            load_plugins(["/nonexistent/plugin.py"], "/tmp", api)
        except Exception as exc:
            self.fail(f"load_plugins raised on nonexistent path: {exc}")

    def test_failure_does_not_block_subsequent_plugins(self):
        """A bad plugin must not prevent subsequent plugins from loading."""
        with tempfile.TemporaryDirectory() as tmpdir:
            bad_path = os.path.join(tmpdir, "bad.py")
            with open(bad_path, "w") as fh:
                fh.write("raise RuntimeError('oops')\n")

            good_path = os.path.join(tmpdir, "good.py")
            with open(good_path, "w") as fh:
                fh.write(textwrap.dedent("""\
                    def register(api):
                        class _Stub:
                            NAME = "after-error"
                            HELP = "loaded after bad plugin"
                            def run(self, args): return 0
                        api.add_command("after-error", _Stub())
                """))

            api = PluginAPI()
            load_plugins([bad_path, good_path], tmpdir, api)
            self.assertIn("after-error", _registry_module.COMMANDS)


if __name__ == "__main__":
    unittest.main()
