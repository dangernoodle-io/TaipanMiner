"""ota command — OTA firmware operations (nested subcommands: push/pull/mark-valid/recover/status/verify)."""
from __future__ import annotations

import sys

from core import resolve_devices, unwrap_devices, no_devices_message, ota_guard, ota_settle

NAME = "ota"
HELP = "OTA firmware operations"


def add_arguments(parser) -> None:
    from core import add_common_flags
    add_common_flags(parser)
    ota_sub = parser.add_subparsers(dest="ota_op", metavar="OP")
    ota_sub.required = True

    op_push = ota_sub.add_parser("push", help="push a local .bin to devices")
    add_common_flags(op_push)
    op_push.add_argument("--bin", dest="binfile", metavar="PATH", required=True,
                         help="path to firmware .bin")
    op_push.add_argument("--target", metavar="VER", dest="target_version",
                         help="expected version after flashing")
    op_push.add_argument("--mark-valid", dest="mark_valid", action="store_true",
                         default=False,
                         help="after push succeeds, POST /api/update/mark-valid to "
                              "force-validate the image (prevents rollback); "
                              "default: let firmware self-validate")

    op_pull = ota_sub.add_parser("pull", help="trigger pull-OTA on devices")
    add_common_flags(op_pull)
    op_pull.add_argument("--mode", default="auto", choices=["auto", "pull"],
                         help="OTA mode: auto=detect boot/pull mode (default), pull=force")
    op_pull.add_argument("--target", metavar="VER", dest="target_version",
                         help="assert devices land on this version")

    op_mark = ota_sub.add_parser("mark-valid", help="mark current image valid")
    add_common_flags(op_mark)

    op_recover = ota_sub.add_parser("recover", help="rollback to previous image")
    add_common_flags(op_recover)

    op_ostatus = ota_sub.add_parser("status", help="read /api/update/status + /api/update/progress")
    add_common_flags(op_ostatus)

    op_verify = ota_sub.add_parser("verify", help="verify version + ota_validated")
    add_common_flags(op_verify)
    op_verify.add_argument("--target", metavar="VER", dest="target_version", required=True,
                           help="expected version string")


def add_common_flags(p) -> None:
    """Local alias to avoid import at module level."""
    from core import add_common_flags as _add
    _add(p)


def cmd_ota_push(args) -> int:
    """OTA push a local binary to devices."""
    import os as _os
    from fleetlib import ota
    from fleetlib.client import Client
    from fleetlib.criteria import load as load_criteria
    from fleetlib.profiles import Profiles, profile_for
    from fleetlib.safety import DeviceUnreachable, IdentityMismatch

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = ota_guard(args)
    settle_cfg = ota_settle(args)
    settle_secs = settle_cfg.settle_delay if settle_cfg.enabled else None
    target = getattr(args, "target_version", None)
    binfile = args.binfile
    dry_run = getattr(args, "dry_run", False)
    do_mark_valid = getattr(args, "mark_valid", False)

    # Load criteria (from --criteria path or harness defaults).
    criteria_path = getattr(args, "criteria", None)
    criteria = load_criteria(criteria_path) if criteria_path else load_criteria()

    # Load profiles once for per-device profile resolution.
    _profiles = Profiles.load()

    _push = getattr(ota, "push", None)
    if _push is None:
        print("ERROR: fleetlib.ota.push not available")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        prof = profile_for(getattr(d, "board", "") or "", _profiles)

        if dry_run:
            # Run identity verify manually so dry-run still checks board identity.
            from fleetlib.discovery import verify_identity
            id_ok = verify_identity(d)
            id_str = "PASS" if id_ok else "FAIL"
            try:
                bin_size = _os.path.getsize(binfile)
                size_str = f"{bin_size:,} bytes"
            except OSError:
                size_str = "(file not found)"
            print(f"[DRY-RUN] push plan for {d.ip} ({d.board}):")
            print(f"  identity-verify : {id_str}")
            print(f"  bin file        : {binfile}")
            print(f"  image size      : {size_str}")
            print(f"  target host     : {d.ip}:{getattr(d, 'port', 80)}")
            print(f"  post-push expect: device reboots, boots new image, "
                  f"firmware self-validates")
            if do_mark_valid:
                print(f"  mark-valid      : will POST after readiness (--mark-valid)")
            if target:
                print(f"  target version  : {target}")
            print(f"  (no HTTP sent)")
            continue

        print(f"Pushing {binfile} to {d.ip}…")
        try:
            r = _push(c, guard, binfile, target_version=target, settle=settle_secs,
                      do_mark_valid=do_mark_valid, criteria=criteria, profile=prof)
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

        if r.ok and r.pending:
            print(f"  {d.ip}: push PENDING — {r.detail}")
        elif r.ok:
            print(f"  {d.ip}: push OK" + (f" ({r.detail})" if r.detail not in ("ok", "") else ""))
        else:
            print(f"  {d.ip}: push FAILED ({r.detail})")
            ok = False

    return 0 if ok else 1


def cmd_ota_pull(args) -> int:
    """Trigger pull-OTA on devices."""
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = ota_guard(args)
    settle_cfg = ota_settle(args)
    settle_secs = settle_cfg.settle_delay if settle_cfg.enabled else None
    target = getattr(args, "target_version", None)
    mode = getattr(args, "mode", "auto")

    _pull = getattr(ota, "pull", None)
    if _pull is None:
        print("ERROR: fleetlib.ota.pull not available")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        print(f"Triggering pull ({mode}) on {d.ip}…")
        r = _pull(c, guard, mode=mode, target_version=target, settle=settle_secs)
        if r.ok:
            print(f"  {d.ip}: pulled to {r.version}")
            if target and r.version != target:
                print(f"  {d.ip}: WARNING expected {target}, got {r.version}")
        else:
            print(f"  {d.ip}: pull FAILED ({r.detail})")
            ok = False

    return 0 if ok else 1


def cmd_ota_mark_valid(args) -> int:
    """Mark current image valid on devices."""
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = ota_guard(args)

    _mark = getattr(ota, "mark_valid", None)
    if _mark is None:
        print("ERROR: fleetlib.ota.mark_valid not available")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        success = _mark(c, guard)
        label = "OK" if success else "FAILED"
        print(f"  {d.ip}: mark-valid {label}")
        if not success:
            ok = False

    return 0 if ok else 1


def cmd_ota_recover(args) -> int:
    """Rollback to previous OTA image on devices."""
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    guard = ota_guard(args)

    _recover = getattr(ota, "recover", None)
    if _recover is None:
        print("ERROR: fleetlib.ota.recover not yet available (OTA agent pending)")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        success = _recover(c, guard)
        label = "OK" if success else "FAILED"
        print(f"  {d.ip}: recover {label}")
        if not success:
            ok = False

    return 0 if ok else 1


def cmd_ota_status(args) -> int:
    """Read OTA status + progress from devices (read-only)."""
    from fleetlib import ota
    from fleetlib.client import Client

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    _status = getattr(ota, "status", None)
    if _status is None:
        print("ERROR: fleetlib.ota.status not yet available (OTA agent pending)")
        return 1

    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        result = _status(c)
        if result is None:
            print(f"  {d.ip}: unreachable")
        else:
            print(f"  {d.ip}: {result}")

    return 0


def cmd_ota_verify(args) -> int:
    """Verify version + mining state post-settle on devices."""
    from fleetlib import ota
    from fleetlib.client import Client
    from fleetlib.criteria import load as load_criteria

    _resolve_result = resolve_devices(args)
    devices = unwrap_devices(_resolve_result)
    if not devices:
        print(no_devices_message(_resolve_result), file=sys.stderr)
        return 1

    target = args.target_version
    settle_cfg = ota_settle(args)
    settle_secs = settle_cfg.settle_delay if settle_cfg.enabled else None
    criteria_path = getattr(args, "criteria", None)
    criteria = load_criteria(criteria_path) if criteria_path else load_criteria()

    _verify = getattr(ota, "verify", None)
    if _verify is None:
        print("ERROR: fleetlib.ota.verify not available")
        return 1

    ok = True
    for d in devices:
        c = Client(d.ip, getattr(d, "port", 80))
        c.board = d.board
        r = _verify(c, None, criteria, target, settle_secs)
        label = "OK" if r.ok else "FAIL"
        print(f"  {d.ip}: verify {target} -> {label}" + (f" ({r.detail})" if not r.ok else ""))
        if not r.ok:
            ok = False

    return 0 if ok else 1


def run(args) -> int:
    op = getattr(args, "ota_op", None)
    if op == "push":
        return cmd_ota_push(args)
    elif op == "pull":
        return cmd_ota_pull(args)
    elif op == "mark-valid":
        return cmd_ota_mark_valid(args)
    elif op == "recover":
        return cmd_ota_recover(args)
    elif op == "status":
        return cmd_ota_status(args)
    elif op == "verify":
        return cmd_ota_verify(args)
    return 1
