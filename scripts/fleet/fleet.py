#!/usr/bin/env python3
"""fleet.py — TaipanMiner fleet test harness CLI (TA-433).

Subcommands:
  discover         — discover devices via mDNS, print table
  status           — GET /api/info + /api/health per device, print summary
  probe-endpoints  — spec-driven endpoint crash probe (TA-469)
  describe         — inspect the served OpenAPI spec (paths, request/response schemas)
  call             — make an arbitrary API request (safety-gated for mutating methods)
  logs             — retrieve device kernel log via GET /api/logs (SSE)
  functional       — run functional suite (schema validation per device)
  soak             — run soak suite (long-running monitor)
  stress           — run stress suite (concurrent load)
  faults           — run fault-injection suite
  telemetry        — run telemetry transport suite
  ota              — OTA operations (push/pull/mark-valid/recover/status/verify)
  decode           — decode a panic backtrace from a live device using an archived ELF
  elf              — ELF archive management (archive/list/prune)
"""
from __future__ import annotations

# Python version floor (TA-450): fail fast with a clear message.
# CI target is 3.11; walrus operator (:=) requires 3.8+ and dataclasses 3.7+,
# but 3.11 is the tested baseline and matches CI.
import sys as _sys
if _sys.version_info < (3, 11):
    print(
        f"ERROR: fleet requires Python >= 3.11 "
        f"(found {_sys.version_info.major}.{_sys.version_info.minor}). "
        f"Install Python 3.11+ and retry.",
        file=_sys.stderr,
    )
    _sys.exit(1)

import os
import sys

# Ensure fleetlib and suites are importable when run directly
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cli import main

if __name__ == "__main__":
    sys.exit(main())
