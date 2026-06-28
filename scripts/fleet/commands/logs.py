"""logs command — retrieve device kernel log via GET /api/logs (SSE)."""
from __future__ import annotations

NAME = "logs"
HELP = "retrieve device kernel log via GET /api/logs (SSE, read-only)"


def add_arguments(parser) -> None:
    from core import add_logs_flags
    add_logs_flags(parser)
    parser.add_argument("--follow", "-f", action="store_true",
                        help="stream until Ctrl-C (clean exit)")
    parser.add_argument("--duration", metavar="DUR",
                        help="stop after this duration: 30s / 5m / 1h / bare seconds")
    parser.add_argument("--lines", type=int, default=None, metavar="N",
                        help="stop after N log lines (default: 50 when neither --follow nor --duration given)")
    parser.add_argument("--out", metavar="PATH", dest="out_path",
                        help="also write captured lines to a file (in addition to stdout)")


def run(args) -> int:
    from fleet import cmd_logs
    return cmd_logs(args)
