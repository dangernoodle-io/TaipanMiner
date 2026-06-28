"""describe command — inspect the served OpenAPI spec."""
from __future__ import annotations

NAME = "describe"
HELP = "inspect the served OpenAPI spec"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)
    parser.add_argument("path", nargs="?", metavar="PATH",
                        help="endpoint path to inspect (e.g. /api/settings)")
    parser.add_argument("method", nargs="?", metavar="METHOD",
                        help="HTTP method to inspect (e.g. PATCH)")
    parser.add_argument("--json", dest="json_raw", action="store_true",
                        help="dump raw schema JSON instead of pretty table")


def run(args) -> int:
    from fleet import cmd_describe
    return cmd_describe(args)
