"""Argparse dispatcher — builds subparsers from the COMMANDS registry."""
from __future__ import annotations

import argparse
import logging
import os
import sys

# Ensure fleet package directory is on path when invoked directly
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from core import load_config, load_plugins
from registry import COMMANDS, PluginAPI
import commands  # registers built-in commands as a side-effect


def _build_cli_parser(config: dict | None = None) -> argparse.ArgumentParser:
    """Build and return the root ArgumentParser (without parsing args).

    Exposed for tests that need a parser reference (e.g. test_log_level.py).
    """
    if config is None:
        config = {}

    # Load plugins so their commands are registered before subparsers are built.
    plugin_paths = config.get("plugins", {}).get("paths", [])
    config_dir = os.getcwd()
    api = PluginAPI()
    load_plugins(plugin_paths, config_dir, api)

    parser = argparse.ArgumentParser(
        prog="fleet",
        description="TaipanMiner fleet test harness",
    )
    parser.set_defaults(log_level="WARNING")

    from core import add_common_flags
    add_common_flags(parser)

    sub = parser.add_subparsers(dest="subcommand", metavar="SUBCOMMAND")
    sub.required = True

    for name, mod in COMMANDS.items():
        help_str = getattr(mod, "HELP", "")
        cmd_sub = sub.add_parser(name, help=help_str)
        if hasattr(mod, "add_arguments"):
            mod.add_arguments(cmd_sub)

    return parser


def main() -> int:
    # Load config early (before building subparsers) so plugins can register
    config = load_config("fleet.toml")

    parser = _build_cli_parser(config)
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(levelname)s %(name)s: %(message)s",
    )

    return COMMANDS[args.subcommand].run(args)


if __name__ == "__main__":
    sys.exit(main())
