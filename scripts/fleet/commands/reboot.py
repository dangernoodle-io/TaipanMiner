"""reboot command — reboot devices via POST /api/reboot, safety-gated."""
from __future__ import annotations

import sys

from core import resolve_devices, unwrap_devices, no_devices_message, ota_guard, ota_settle, SETTLE_BARE

NAME = "reboot"
HELP = "Reboot devices (POST /api/reboot), safety-gated"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)


def run(args) -> int:
    from fleetlib import ota as _ota_lib
    from fleetlib.client import Client
    from fleetlib.safety import DeviceUnreachable, IdentityMismatch

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = ota_guard(args)
    settle_cfg = ota_settle(args)
    settle_secs = settle_cfg.settle_delay if settle_cfg.enabled else None
    dry_run = getattr(args, "dry_run", False)

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board

        if dry_run:
            from fleetlib.discovery import verify_identity
            id_ok = verify_identity(d)
            id_str = "PASS" if id_ok else "FAIL"
            print(f"[DRY-RUN] reboot plan for {d.ip} ({d.board}):")
            print(f"  identity-verify : {id_str}")
            print(f"  target host     : {d.ip}:{getattr(d, 'port', 80)}")
            print(f"  would POST      : /api/reboot")
            if settle_secs is not None:
                print(f"  would wait until ready (settle={settle_secs}s)")
            print(f"  (no HTTP sent)")
            continue

        try:
            result = _ota_lib.reboot(c, guard, settle=settle_secs)
        except DeviceUnreachable as exc:
            print(f"  {d.ip}: SKIPPED (unreachable: {exc})")
            ok = False
            continue
        except IdentityMismatch as exc:
            print(f"  {d.ip}: SKIPPED (identity mismatch: {exc})")
            ok = False
            continue
        except Exception as exc:
            print(f"  {d.ip}: FAILED (unexpected error: {exc})")
            ok = False
            continue

        if result.ok:
            if settle_secs is not None:
                state = "ready" if result.ready else "not-ready"
                print(f"  {d.ip}: reboot OK, settle {state}")
            else:
                print(f"  {d.ip}: reboot issued")
        else:
            print(f"  {d.ip}: reboot FAILED ({result.detail})")
            ok = False

    return 0 if ok else 1
