"""elf command — ELF archive management (nested subcommands: archive/list/prune)."""
from __future__ import annotations

import argparse

NAME = "elf"
HELP = "ELF archive management"


def add_arguments(parser) -> None:
    elf_sub = parser.add_subparsers(dest="elf_op", metavar="OP")
    elf_sub.required = True

    elf_archive = elf_sub.add_parser("archive", help="manually archive a firmware ELF")
    elf_archive.add_argument("elf_path", metavar="PATH", help="path to firmware .elf")
    elf_archive.add_argument("--board", default="", metavar="BOARD", help="board name")
    elf_archive.add_argument("--version", default="", metavar="VER", help="firmware version")
    elf_archive.add_argument("--log-level", default=argparse.SUPPRESS,
                             choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                             help="log level (default: WARNING)")

    elf_list = elf_sub.add_parser("list", help="list archived ELFs with in-use status")
    elf_list.add_argument("--hosts", metavar="H,H,…",
                          help="comma-separated IPs/hostnames (skip mDNS discovery)")
    elf_list.add_argument("--discover-timeout", type=int, default=10, metavar="SEC",
                          help="mDNS browse window in seconds (default: 10)")
    elf_list.add_argument("--board", metavar="CLASS",
                          help="filter devices by board class substring")
    elf_list.add_argument("--log-level", default=argparse.SUPPRESS,
                          choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                          help="log level (default: WARNING)")

    elf_prune = elf_sub.add_parser("prune", help="prune archived ELFs by budget or in-use GC")
    elf_prune.add_argument("--keep", type=int, default=20, metavar="N",
                           help="keep N most-recent entries (default: 20)")
    elf_prune.add_argument("--max-age", dest="max_age", metavar="DUR",
                           help="delete entries older than DUR (e.g. 30d, 7d, 1h)")
    elf_prune.add_argument("--in-use", dest="in_use", action="store_true",
                           help="fleet-aware GC: delete ELFs NOT running on any discovered device")
    elf_prune.add_argument("--hosts", metavar="H,H,…",
                           help="comma-separated IPs/hostnames (authoritative set for --in-use)")
    elf_prune.add_argument("--discover-timeout", type=int, default=10, metavar="SEC",
                           help="mDNS browse window in seconds (default: 10)")
    elf_prune.add_argument("--board", metavar="CLASS",
                           help="filter devices by board class substring")
    elf_prune.add_argument("--grace-keep", type=int, default=5, dest="grace_keep",
                           metavar="N",
                           help="always keep the N most-recently archived entries "
                                "regardless of in-use status (default: 5)")
    elf_prune.add_argument("--dry-run", action="store_true",
                           help="show what would be deleted without deleting")
    elf_prune.add_argument("--yes", action="store_true",
                           help="skip confirmation prompt")
    elf_prune.add_argument("--log-level", default=argparse.SUPPRESS,
                           choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                           help="log level (default: WARNING)")


def run(args) -> int:
    op = getattr(args, "elf_op", None)
    if op == "archive":
        from fleet import cmd_elf_archive
        return cmd_elf_archive(args)
    elif op == "list":
        from fleet import cmd_elf_list
        return cmd_elf_list(args)
    elif op == "prune":
        from fleet import cmd_elf_prune
        return cmd_elf_prune(args)
    return 1
