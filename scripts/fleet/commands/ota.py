"""ota command — OTA firmware operations (nested subcommands: push/pull/mark-valid/recover/status/verify)."""
from __future__ import annotations

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


def run(args) -> int:
    op = getattr(args, "ota_op", None)
    if op == "push":
        from fleet import cmd_ota_push
        return cmd_ota_push(args)
    elif op == "pull":
        from fleet import cmd_ota_pull
        return cmd_ota_pull(args)
    elif op == "mark-valid":
        from fleet import cmd_ota_mark_valid
        return cmd_ota_mark_valid(args)
    elif op == "recover":
        from fleet import cmd_ota_recover
        return cmd_ota_recover(args)
    elif op == "status":
        from fleet import cmd_ota_status
        return cmd_ota_status(args)
    elif op == "verify":
        from fleet import cmd_ota_verify
        return cmd_ota_verify(args)
    return 1
