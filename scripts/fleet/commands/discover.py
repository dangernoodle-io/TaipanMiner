"""discover command — discover devices via mDNS and print table."""
from __future__ import annotations

NAME = "discover"
HELP = "discover devices via mDNS and print table"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)


def run(args) -> int:
    from fleet import cmd_discover
    return cmd_discover(args)
