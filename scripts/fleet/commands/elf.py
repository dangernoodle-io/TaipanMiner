"""elf command — ELF archive management (nested subcommands: archive/list/prune)."""
from __future__ import annotations

import argparse
import sys

from core import resolve_devices, unwrap_devices

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


def _parse_age_duration(s: str) -> float:
    """Parse '30d', '7d', '24h', '1h' to seconds."""
    s = s.strip()
    if s.endswith("d"):
        return float(s[:-1]) * 86400
    if s.endswith("h"):
        return float(s[:-1]) * 3600
    if s.endswith("m"):
        return float(s[:-1]) * 60
    if s.endswith("s"):
        return float(s[:-1])
    return float(s)


def cmd_elf_archive(args) -> int:
    """Manually archive a firmware ELF into the store.

    board and version are populated from esp_app_desc_t when not given (TA-461).
    """
    from fleetlib.elfstore import archive, sha256_of_elf, list_entries
    from pathlib import Path

    elf_path = args.elf_path
    board = getattr(args, "board", "")
    version = getattr(args, "version", "")

    try:
        key = archive(elf_path, board=board, version=version)

        # Read back sidecar to show final (possibly auto-populated) values
        from fleetlib.elfstore import _load_meta, _archive_root
        root = _archive_root()
        meta = _load_meta(root / f"{key}.json")

        print(f"Archived: {elf_path}")
        print(f"  sha256     : {key}")
        print(f"  board      : {meta.board or '(unset)'}")
        print(f"  version    : {meta.version or '(unset)'}")
        print(f"  build_time : {meta.build_time or '(unset)'}")
        if not board and meta.board:
            print(f"  (board auto-populated from esp_app_desc_t)")
        if not version and meta.version:
            print(f"  (version auto-populated from esp_app_desc_t)")
        return 0
    except FileNotFoundError:
        print(f"ERROR: ELF not found: {elf_path}")
        return 1
    except Exception as exc:
        print(f"ERROR: archive failed: {exc}")
        return 1


def cmd_elf_list(args) -> int:
    """List archived ELFs with in-use status from the live fleet."""
    import datetime
    from fleetlib.client import Client, TIMEOUT_INFO, info_field
    from fleetlib.elfstore import list_entries

    entries = list_entries()
    if not entries:
        print("No archived ELFs.")
        return 0

    # Collect running shas from live fleet (best-effort; failures -> empty).
    # Primary source: /api/info build.app_sha256 (B1-360 — running image sha).
    # Fallback: /api/diag/panic app_sha256 (only populated after a crash).
    # Both are truncated ELF sha256 prefixes matching the elfstore archive key.
    running_shas: set = set()
    unreachable: list = []
    try:
        devices = unwrap_devices(resolve_devices(args))
        for d in devices:
            c = Client(d.ip, getattr(d, "port", 80))
            info = c.get_json("/api/info", timeout=TIMEOUT_INFO)
            sha = info_field(info or {}, "app_sha256") or ""
            if sha:
                running_shas.add(sha.lower())
            else:
                # fallback: crash-build sha from /api/diag/panic
                panic = c.get_json("/api/diag/panic", timeout=TIMEOUT_INFO)
                sha = (panic or {}).get("app_sha256", "")
                if sha:
                    running_shas.add(sha.lower())
    except Exception:
        unreachable = []

    if unreachable:
        print(f"WARNING: {len(unreachable)} device(s) unreachable; IN-USE column may be incomplete")

    print(f"\n{'SHA256 (prefix)':<20} {'BOARD':<20} {'VERSION':<18} {'DIRTY':<6} "
          f"{'ARCHIVED':<22} {'SIZE':>10}  IN-USE")
    print("-" * 105)
    for meta, size in entries:
        sha_short = meta.sha256[:16]
        dirty_str = "yes" if meta.dirty else "no"
        size_str = f"{size:,}"
        # IN-USE: any running sha (truncated prefix) matches the start of the archived full sha.
        # Compare lower-case so dev/-dirty sha prefixes from /api/info match the archive key.
        in_use = any(meta.sha256.lower().startswith(s.lower()) for s in running_shas) if running_shas else "?"
        in_use_str = "yes" if in_use is True else ("no" if in_use is False else "?")
        print(f"{sha_short + '…':<20} {meta.board:<20} {meta.version:<18} {dirty_str:<6} "
              f"{meta.archived_at:<22} {size_str:>10}  {in_use_str}")

    total_size = sum(s for _, s in entries)
    print(f"\n{len(entries)} archived ELF(s), {total_size:,} bytes total")
    return 0


def cmd_elf_prune(args) -> int:
    """Prune archived ELFs by mtime budget or fleet-aware GC.

    SAFETY GUARDS (--in-use mode):
      1. The most-recently archived N entries (--grace-keep, default 5) are
         ALWAYS protected regardless of in-use status.
      2. If any fleet target is unreachable, prune is REFUSED unless --hosts
         supplies an authoritative set and all listed hosts are reachable.
    """
    from fleetlib.client import Client, TIMEOUT_INFO
    from fleetlib.elfstore import list_entries, prune as elf_prune

    keep = args.keep
    max_age_str = getattr(args, "max_age", None)
    in_use_mode = getattr(args, "in_use", False)
    grace_keep = getattr(args, "grace_keep", 5)
    dry_run = getattr(args, "dry_run", False)
    yes = getattr(args, "yes", False)

    max_age_secs: float = None
    if max_age_str:
        max_age_secs = _parse_age_duration(max_age_str)

    protected_shas: set = set()

    if in_use_mode:
        # Fleet-aware GC: collect running shas
        _elf_resolve = resolve_devices(args)
        devices = unwrap_devices(_elf_resolve)
        if not devices:
            print("ERROR: no devices found; cannot perform fleet-aware GC safely")
            return 1

        unreachable = []
        running_shas: set = set()
        for d in devices:
            c = Client(d.ip, getattr(d, "port", 80))
            from fleetlib.client import info_field as _info_field_prune
            info_resp = c.get_json("/api/info", timeout=TIMEOUT_INFO)
            if info_resp is None:
                unreachable.append(d.ip)
            else:
                sha = _info_field_prune(info_resp, "app_sha256") or ""
                if sha:
                    running_shas.add(sha)
                else:
                    # fallback: crash-build sha from /api/diag/panic
                    panic = c.get_json("/api/diag/panic", timeout=TIMEOUT_INFO)
                    sha = (panic or {}).get("app_sha256", "")
                    if sha:
                        running_shas.add(sha)

        # SAFETY GUARD 2: refuse on incomplete discovery (unless --hosts authoritative)
        if unreachable and not getattr(args, "hosts", None):
            print("ERROR: the following devices are unreachable — cannot safely determine "
                  "which ELFs are in use:")
            for ip in unreachable:
                print(f"  {ip}")
            print("Pass --hosts with an authoritative list of ALL fleet devices, "
                  "or fix device connectivity first.")
            return 1
        elif unreachable:
            print(f"WARNING: {len(unreachable)} device(s) unreachable; proceeding because "
                  f"--hosts provides an authoritative set")
            for ip in unreachable:
                print(f"  UNREACHABLE: {ip}")

        # Expand running short-shas to full archive keys
        entries = list_entries()
        for meta, _ in entries:
            for short_sha in running_shas:
                if meta.sha256.startswith(short_sha):
                    protected_shas.add(meta.sha256)

        # SAFETY GUARD 1: always protect the N most-recently archived entries
        if grace_keep > 0:
            sorted_entries = sorted(entries, key=lambda t: t[0].archived_at, reverse=True)
            for meta, _ in sorted_entries[:grace_keep]:
                protected_shas.add(meta.sha256)

        print(f"Fleet-aware GC: {len(running_shas)} running sha(s) found, "
              f"{len(protected_shas)} entries protected")

    else:
        # Mtime budget prune: protect the grace_keep most recent
        entries = list_entries()
        if grace_keep > 0:
            sorted_entries = sorted(entries, key=lambda t: t[0].archived_at, reverse=True)
            for meta, _ in sorted_entries[:grace_keep]:
                protected_shas.add(meta.sha256)

    # Preview what will be deleted
    would_delete = elf_prune(
        keep=keep, max_age=max_age_secs,
        protected_shas=protected_shas, dry_run=True,
    )
    if not would_delete:
        print("Nothing to prune.")
        return 0

    print(f"{'[DRY-RUN] ' if dry_run else ''}Would delete {len(would_delete)} entry(ies):")
    for sha in would_delete:
        print(f"  {sha[:16]}…")

    if dry_run:
        return 0

    if not yes:
        try:
            ans = input(f"Delete {len(would_delete)} entry(ies)? [y/N] ")
        except EOFError:
            ans = ""
        if ans.strip().lower() not in ("y", "yes"):
            print("Aborted.")
            return 1

    deleted = elf_prune(
        keep=keep, max_age=max_age_secs,
        protected_shas=protected_shas, dry_run=False,
    )
    print(f"Deleted {len(deleted)} entry(ies).")
    return 0


def run(args) -> int:
    op = getattr(args, "elf_op", None)
    if op == "archive":
        return cmd_elf_archive(args)
    elif op == "list":
        return cmd_elf_list(args)
    elif op == "prune":
        return cmd_elf_prune(args)
    return 1
