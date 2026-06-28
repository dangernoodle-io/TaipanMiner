"""status command — GET /api/info + /api/health per device."""
from __future__ import annotations

NAME = "status"
HELP = "GET /api/info + /api/health per device"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)


def run(args) -> int:
    from fleet import cmd_status
    return cmd_status(args)
