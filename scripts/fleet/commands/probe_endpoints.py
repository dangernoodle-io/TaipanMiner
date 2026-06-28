"""probe-endpoints command — spec-driven endpoint crash probe."""
from __future__ import annotations

import sys

from core import resolve_devices, unwrap_devices, no_devices_message

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


def _fmt_uptime(uptime_ms: int) -> str:
    s = uptime_ms // 1000
    h, rem = divmod(s, 3600)
    m, sec = divmod(rem, 60)
    if h > 0:
        return f"{h}h{m:02d}m{sec:02d}s"
    if m > 0:
        return f"{m}m{sec:02d}s"
    return f"{sec}s"


def run(args) -> int:
    """Spec-driven endpoint crash probe (TA-469).

    Enumerates GET paths from /api/openapi.json, probes each once, and
    detects uptime regressions (crash/reboot) after each hit.  Mutating
    methods (POST/PUT/PATCH/DELETE) and streaming endpoints (/api/logs,
    /api/diag/events, /ws) are skipped by default.
    """
    from fleetlib.client import Client, TIMEOUT_INFO
    from fleetlib.spec import Spec

    # Endpoints that block indefinitely — skip unless opt-in
    _STREAMING = {"/api/logs", "/api/diag/events", "/ws"}
    # Mutating HTTP methods — skip unless --include-mutating
    _MUTATING = {"post", "put", "patch", "delete"}

    include_mutating = getattr(args, "include_mutating", False)
    include_streaming = getattr(args, "include_streaming", False)

    result = resolve_devices(args)
    devices = unwrap_devices(result)
    if not devices:
        print(no_devices_message(result), file=sys.stderr)
        return 1

    all_ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        print(f"\nProbing {d.ip} ({d.board} {d.version})")

        # Fetch spec
        spec_doc = c.get_json("/api/openapi.json", timeout=TIMEOUT_INFO)
        if spec_doc is None:
            print(f"  ERROR: could not fetch /api/openapi.json — device unreachable?")
            all_ok = False
            continue
        spec = Spec(spec_doc)

        # Collect probe targets: safe GETs only by default
        targets = []
        for path in sorted(spec.paths()):
            methods = spec.methods(path)
            is_streaming = path in _STREAMING
            has_get = "get" in methods

            if is_streaming and not include_streaming:
                continue
            if not has_get and not include_mutating:
                continue
            # Determine method to use
            if has_get and (not is_streaming or include_streaming):
                targets.append((path, "GET"))
            elif include_mutating:
                for m in methods:
                    if m in _MUTATING:
                        targets.append((path, m.upper()))
                        break

        if not targets:
            print("  no probeable endpoints found")
            continue

        # Read baseline uptime
        def _uptime(client: Client):
            info = client.get_json("/api/info", timeout=TIMEOUT_INFO)
            if info is None:
                return None
            return info.get("uptime_ms")

        baseline_uptime = _uptime(c)
        if baseline_uptime is None:
            print("  ERROR: could not read baseline uptime from /api/info")
            all_ok = False
            continue

        print(f"  baseline uptime: {_fmt_uptime(baseline_uptime)}  "
              f"({len(targets)} endpoints to probe)")
        print()
        print(f"  {'ENDPOINT':<40} {'STATUS':>8}  RESULT")
        print(f"  {'-' * 60}")

        prev_uptime = baseline_uptime
        for path, method in targets:
            status_code = None
            result_str = "ok"
            if method == "GET":
                resp = c.get_json(path, timeout=TIMEOUT_INFO)
                if resp is None:
                    # Could be a genuine error response (e.g. 404) or unreachable
                    status_code, _ = c.request("GET", path, timeout=TIMEOUT_INFO)
                    result_str = "err" if status_code is not None else "unreachable"
                else:
                    status_code = 200
            else:
                status_code, _ = c.request(method, path, timeout=TIMEOUT_INFO)
                result_str = "ok" if (status_code is not None and status_code < 500) else "err"

            # Re-read uptime to detect crash
            new_uptime = _uptime(c)
            if new_uptime is None:
                result_str = "CRASH (unreachable after)"
                all_ok = False
            elif new_uptime < prev_uptime:
                result_str = f"CRASH (uptime regressed: {_fmt_uptime(prev_uptime)} -> {_fmt_uptime(new_uptime)})"
                all_ok = False
            else:
                prev_uptime = new_uptime

            status_str = str(status_code) if status_code is not None else "??"
            print(f"  {path:<40} {status_str:>8}  {result_str}")

        # Final uptime
        final_uptime = _uptime(c)
        if final_uptime is not None:
            print(f"\n  final uptime: {_fmt_uptime(final_uptime)}")
            if final_uptime < baseline_uptime:
                print(f"  WARNING: overall uptime regressed — device rebooted during probe")
                all_ok = False
            else:
                print(f"  no crashes detected")

    return 0 if all_ok else 1
