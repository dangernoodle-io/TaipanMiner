"""decode command — decode a panic backtrace from a live device."""
from __future__ import annotations

import argparse

NAME = "decode"
HELP = "decode a panic backtrace from a live device (uses archived ELF)"


def add_arguments(parser) -> None:
    parser.add_argument("host", metavar="HOST",
                        help="device IP or hostname to fetch /api/diag/panic from")
    parser.add_argument("--elf", dest="elf_path", metavar="PATH",
                        help="explicit ELF file (overrides archive lookup)")
    parser.add_argument("--toolchain-path", dest="toolchain_path", metavar="PATH",
                        help="explicit path to addr2line binary (overrides auto-detect)")
    parser.add_argument("--log-level", default=argparse.SUPPRESS,
                        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                        help="log level (default: WARNING)")


def run(args) -> int:
    """Decode a panic backtrace from a live device using an archived ELF.

    GET /api/diag/panic  ->  prefix-match ELF in archive  ->  addr2line decode.
    """
    import json as _json
    from fleetlib.client import Client, TIMEOUT_INFO
    from fleetlib.elfstore import find as elf_find
    from fleetlib.decode import chip_arch, decode_panic

    host = args.host
    port = 80
    if ":" in host:
        host, port_s = host.rsplit(":", 1)
        port = int(port_s)

    c = Client(host, port)
    panic = c.get_json("/api/diag/panic", timeout=TIMEOUT_INFO)
    if panic is None:
        print(f"ERROR: could not reach {host} or /api/diag/panic unavailable")
        return 1

    available = panic.get("available", False)
    app_sha = panic.get("app_sha256", "")

    if not available and not panic.get("backtrace") and not panic.get("exc_pc"):
        print(f"{host}: no panic available (available=false, no backtrace/pc)")
        return 0

    # Determine arch from /api/info build.chip_model (B1-360)
    from fleetlib.client import info_field as _info_field
    info = c.get_json("/api/info", timeout=TIMEOUT_INFO) or {}
    chip_model = _info_field(info, "chip_model") or "ESP32"
    arch = chip_arch(chip_model)

    # Resolve ELF
    elf_path = getattr(args, "elf_path", None)
    if elf_path is None and app_sha:
        elf_path = elf_find(app_sha)
        if elf_path is None:
            print(f"ERROR: no archived ELF for '{app_sha}'; "
                  f"reflash with a tracked build (fleet ota push) or pass --elf <path>")
            return 1
    elif elf_path is None:
        print("ERROR: no app_sha256 in panic response and no --elf given")
        return 1

    toolchain_path = getattr(args, "toolchain_path", None)
    result = decode_panic(panic, elf_path, arch=arch, toolchain_path=toolchain_path)

    # Print result
    print(f"\nPanic decode for {host}")
    print(f"  ELF     : {elf_path}")
    print(f"  arch    : {arch}")
    print(f"  task    : {result.task or '?'}")
    print(f"  cause   : {result.exc_cause} ({result.cause_name_str})")
    if app_sha:
        print(f"  sha256  : {app_sha} (truncated; {len(app_sha)} chars)")
    if not result.ok:
        print(f"  ERROR   : {result.error}")
        return 1

    if not result.frames:
        print("  (no frames decoded)")
    else:
        print(f"\n  {'LABEL':<10} {'PC':>12}   FUNCTION @ FILE:LINE")
        print(f"  {'-' * 70}")
        for label, pc, frame in result.frames:
            print(f"  {label:<10} {pc:#012x}   {frame}")
    return 0
