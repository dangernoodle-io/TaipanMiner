"""probe-endpoints command — spec-driven endpoint crash probe."""
from __future__ import annotations

NAME = "probe-endpoints"
HELP = "spec-driven endpoint crash probe: hit each GET once and flag uptime regressions"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)
    parser.add_argument(
        "--include-mutating", dest="include_mutating", action="store_true",
        help="also probe POST/PUT/PATCH/DELETE endpoints (requires --yes guard)",
    )
    parser.add_argument(
        "--include-streaming", dest="include_streaming", action="store_true",
        help="also probe streaming endpoints (/api/logs, /api/diag/events, /ws) that "
             "would otherwise hang workers",
    )


def run(args) -> int:
    from fleet import cmd_probe_endpoints
    return cmd_probe_endpoints(args)
