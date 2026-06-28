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
    from fleet import cmd_decode
    return cmd_decode(args)
