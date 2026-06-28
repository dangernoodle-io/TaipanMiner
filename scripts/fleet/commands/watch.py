"""watch command — poll an endpoint per-device and observe field values."""
from __future__ import annotations

NAME = "watch"
HELP = "poll an endpoint per-device and observe field values (CI-safe, read-only)"


def add_arguments(parser) -> None:
    from core import add_watch_flags
    add_watch_flags(parser)
    parser.add_argument("watch_path", metavar="PATH",
                        help="endpoint path to poll (e.g. /api/diag/heap)")
    parser.add_argument("--fields", metavar="F,F,…",
                        help="comma-separated dotted fields to extract (default: whole response)")
    parser.add_argument("--interval", type=float, default=5.0, metavar="SEC",
                        help="seconds between polls (default: 5)")
    parser.add_argument("--duration", metavar="DUR",
                        help="stop after this duration: 30s / 5m / 1h / bare seconds")
    parser.add_argument("--count", type=int, default=None, metavar="N",
                        help="stop after N ticks per device")
    parser.add_argument("--on-change", dest="on_change", action="store_true",
                        help="only emit a row when tracked fields change")
    parser.add_argument("--until", metavar="EXPR",
                        help="exit 0 when expression is satisfied (all devices, or --any)")
    parser.add_argument("--any", dest="any_device", action="store_true",
                        help="with --until/--alert: condition met when ANY device satisfies")
    parser.add_argument("--alert", metavar="EXPR",
                        help="flag rows when expression is true (non-terminating)")
    parser.add_argument("--out-json", metavar="PATH", dest="out_json",
                        help="write time-series records as JSON list to file")
    parser.add_argument("--out-csv", metavar="PATH", dest="out_csv",
                        help="write time-series records as CSV to file")


def run(args) -> int:
    from fleet import cmd_watch
    return cmd_watch(args)
