"""Suite command factory — creates NAME/HELP/add_arguments/run objects for each suite."""
from __future__ import annotations


class _SuiteCommand:
    """Module-like command object for a single suite."""

    def __init__(self, name: str) -> None:
        self.NAME = name
        self.HELP = self._get_help(name)

    @staticmethod
    def _get_help(name: str) -> str:
        from core import suite_help
        return suite_help(name)

    def add_arguments(self, parser) -> None:
        from core import add_common_flags, add_suite_arguments
        add_common_flags(parser)
        add_suite_arguments(parser, self.NAME)

    def run(self, args) -> int:
        from fleet import cmd_suite
        return cmd_suite(args, self.NAME)


def suite_command(name: str) -> _SuiteCommand:
    """Return a command module-like object for the named suite."""
    return _SuiteCommand(name)
