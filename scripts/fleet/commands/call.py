"""call command — make an arbitrary API request (safety-gated for mutating methods)."""
from __future__ import annotations

NAME = "call"
HELP = "make an arbitrary API request (safety-gated for mutating methods)"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)
    parser.add_argument("call_method", metavar="METHOD",
                        help="HTTP method: GET, POST, PUT, PATCH, DELETE")
    parser.add_argument("call_path", metavar="PATH",
                        help="endpoint path (e.g. /api/settings)")
    parser.add_argument("--json", dest="json_body", metavar="JSON",
                        help="request body as inline JSON string")
    parser.add_argument("--json-file", dest="json_file", metavar="FILE",
                        help="request body from JSON file")
    parser.add_argument("--no-validate", dest="no_validate", action="store_true",
                        help="skip request body schema validation against the served spec")


def run(args) -> int:
    from fleet import cmd_call
    return cmd_call(args)
