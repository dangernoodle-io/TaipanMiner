"""ELF panic decoder for TaipanMiner fleet harness (TA-445).

Given a /api/diag/panic response and an archived ELF, decode each PC address
to function/file/line using the ESP toolchain's addr2line.

Toolchain selection
-------------------
  XTENSA chips (ESP32, ESP32-S2, ESP32-S3): xtensa-esp-elf-addr2line
  RISCV chips  (ESP32-C3, ESP32-C6, ESP32-H2): riscv32-esp-elf-addr2line

  Toolchains are searched under ~/.platformio/packages/toolchain-*/bin/
  The path is overridable via the FLEET_ADDR2LINE env var or --toolchain-path.

  chip_model -> arch mapping:
    "ESP32"    -> xtensa   (classic WROOM-32, etc.)
    "ESP32-S2" -> xtensa
    "ESP32-S3" -> xtensa
    "ESP32-C3" -> riscv
    "ESP32-C6" -> riscv
    "ESP32-H2" -> riscv

Exception cause names (Xtensa EXCCAUSE register)
-------------------------------------------------
  Subset covering the most common panics; full table in Xtensa ISA ref §4.4.
"""
from __future__ import annotations

import logging
import os
import shutil
import subprocess
from pathlib import Path
from typing import List, Optional, Tuple

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Chip -> arch mapping
# ---------------------------------------------------------------------------
_XTENSA_CHIPS = {"ESP32", "ESP32-S2", "ESP32-S3", "ESP32-S2S", "ESP32-S3S"}
_RISCV_CHIPS  = {"ESP32-C3", "ESP32-C6", "ESP32-H2", "ESP32-C2",
                  "ESP32-C3V", "ESP32-C6V", "ESP32-H2V"}


def chip_arch(chip_model: str) -> str:
    """Return 'xtensa' or 'riscv' for a chip_model string from /api/info."""
    m = chip_model.upper().replace(" ", "").replace("-", "")
    # RISCV check first (more specific)
    for c in _RISCV_CHIPS:
        if m.startswith(c.upper().replace("-", "")):
            return "riscv"
    return "xtensa"


# ---------------------------------------------------------------------------
# Toolchain discovery
# ---------------------------------------------------------------------------

def find_addr2line(arch: str, toolchain_path: Optional[str] = None) -> Optional[str]:
    """Locate the addr2line binary for the given arch.

    Search order:
      1. toolchain_path arg (explicit override)
      2. FLEET_ADDR2LINE env var
      3. Scan ~/.platformio/packages/toolchain-*/bin/ for the right binary
      4. System PATH (shutil.which)
    """
    # Canonical binary names
    if arch == "riscv":
        names = ["riscv32-esp-elf-addr2line"]
    else:
        names = ["xtensa-esp-elf-addr2line"]

    # Explicit overrides
    for candidate in [toolchain_path, os.environ.get("FLEET_ADDR2LINE")]:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate

    # ~/.platformio/packages/toolchain-*/bin/
    pio_packages = Path.home() / ".platformio" / "packages"
    if pio_packages.is_dir():
        for tc_dir in sorted(pio_packages.glob("toolchain-*")):
            bin_dir = tc_dir / "bin"
            for name in names:
                p = bin_dir / name
                if p.is_file() and os.access(str(p), os.X_OK):
                    return str(p)

    # System PATH
    for name in names:
        found = shutil.which(name)
        if found:
            return found

    return None


# ---------------------------------------------------------------------------
# addr2line invocation
# ---------------------------------------------------------------------------

def addr2line_decode(
    binary: str,
    addresses: List[int],
    addr2line_bin: str,
) -> List[str]:
    """Decode a list of integer PC addresses using addr2line.

    Returns a list of decoded frame strings, one per address.
    Format: "function @ file:line"  or  "?? @ ??:0"  on failure.
    """
    if not addresses:
        return []

    hex_addrs = [hex(a) for a in addresses]
    cmd = [addr2line_bin, "-e", binary, "-f", "-C"] + hex_addrs
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT, timeout=30)
        lines = out.decode(errors="replace").splitlines()
    except subprocess.CalledProcessError as exc:
        logger.warning("addr2line failed (rc=%d): %s", exc.returncode,
                       exc.output.decode(errors="replace")[:200])
        return [f"?? @ ??:0" for _ in addresses]
    except FileNotFoundError:
        logger.error("addr2line not found: %s", addr2line_bin)
        return [f"?? @ ??:0" for _ in addresses]
    except subprocess.TimeoutExpired:
        logger.warning("addr2line timed out")
        return [f"?? @ ??:0" for _ in addresses]

    # addr2line -f emits two lines per address: function name, then file:line
    frames: List[str] = []
    for i in range(len(addresses)):
        fn_line  = lines[i * 2]     if i * 2 < len(lines) else "??"
        loc_line = lines[i * 2 + 1] if i * 2 + 1 < len(lines) else "??:0"
        frames.append(f"{fn_line} @ {loc_line}")
    return frames


# ---------------------------------------------------------------------------
# Exception cause name table (Xtensa EXCCAUSE)
# ---------------------------------------------------------------------------
_XTENSA_CAUSE_NAMES = {
    0:  "IllegalInstruction",
    1:  "Syscall",
    2:  "InstructionFetchError",
    3:  "LoadStoreError",
    4:  "Level1Interrupt",
    5:  "Alloca",
    6:  "IntegerDivideByZero",
    7:  "PCValueError",
    8:  "PrivilegedInstruction",
    9:  "LoadStoreAlignmentCause",
    12: "InstrPIFDataError",
    13: "LoadStorePIFDataError",
    14: "InstrPIFAddrError",
    15: "LoadStorePIFAddrError",
    16: "InstTLBMiss",
    17: "InstTLBMultiHit",
    18: "InstFetchPrivilege",
    20: "InstFetchProhibited",
    24: "LoadStoreTLBMiss",
    25: "LoadStoreTLBMultiHit",
    26: "LoadStorePrivilege",
    28: "LoadProhibited",
    29: "StoreProhibited",
    32: "Coprocessor0Disabled",
    33: "Coprocessor1Disabled",
    34: "Coprocessor2Disabled",
    35: "Coprocessor3Disabled",
    36: "Coprocessor4Disabled",
    37: "Coprocessor5Disabled",
    38: "Coprocessor6Disabled",
    39: "Coprocessor7Disabled",
    # ESP-IDF pseudo-causes surfaced in panic output
    128: "DoubleException",
}

# RISCV mcause values (ESP32-C3/C6)
_RISCV_CAUSE_NAMES = {
    0:  "InstructionAddressMisaligned",
    1:  "InstructionAccessFault",
    2:  "IllegalInstruction",
    3:  "Breakpoint",
    4:  "LoadAddressMisaligned",
    5:  "LoadAccessFault",
    6:  "StoreAddressMisaligned",
    7:  "StoreAccessFault",
    8:  "EnvironmentCallFromU",
    9:  "EnvironmentCallFromS",
    11: "EnvironmentCallFromM",
    12: "InstructionPageFault",
    13: "LoadPageFault",
    15: "StorePageFault",
}


def cause_name(exc_cause: int, arch: str = "xtensa") -> str:
    """Return a human-readable exception cause name."""
    table = _RISCV_CAUSE_NAMES if arch == "riscv" else _XTENSA_CAUSE_NAMES
    return table.get(exc_cause, f"Unknown({exc_cause})")


# ---------------------------------------------------------------------------
# High-level decode function
# ---------------------------------------------------------------------------

def decode_panic(
    panic: dict,
    elf_path: str,
    arch: str = "xtensa",
    toolchain_path: Optional[str] = None,
) -> "DecodeResult":
    """Decode a panic dict (from /api/diag/panic) using the given ELF.

    Returns a DecodeResult with decoded frames and metadata.
    """
    addr2line_bin = find_addr2line(arch, toolchain_path)
    if addr2line_bin is None:
        return DecodeResult(
            ok=False,
            error=f"addr2line not found for arch '{arch}'; "
                  "install ESP-IDF toolchain or set FLEET_ADDR2LINE",
            elf_path=elf_path,
            arch=arch,
        )

    exc_pc   = panic.get("exc_pc", 0)
    bt       = panic.get("backtrace") or []
    task     = panic.get("task", "")
    exc_cause = panic.get("exc_cause", 0)
    app_sha  = panic.get("app_sha256", "")

    # Collect all PCs: exc_pc first, then backtrace
    all_pcs: List[int] = []
    if exc_pc:
        all_pcs.append(exc_pc)
    all_pcs.extend(int(a) for a in bt if a)

    if not all_pcs:
        return DecodeResult(
            ok=True,
            error="no PC addresses to decode",
            elf_path=elf_path,
            arch=arch,
            task=task,
            exc_cause=exc_cause,
            cause_name_str=cause_name(exc_cause, arch),
            app_sha256=app_sha,
        )

    frames = addr2line_decode(elf_path, all_pcs, addr2line_bin)

    # Label: first frame is exc_pc, remaining are backtrace
    labeled: List[Tuple[str, int, str]] = []
    if exc_pc and frames:
        labeled.append(("exc_pc", exc_pc, frames[0]))
        bt_frames = frames[1:]
    else:
        bt_frames = frames

    for i, (addr, frame) in enumerate(zip(bt, bt_frames)):
        labeled.append((f"bt[{i}]", int(addr), frame))

    return DecodeResult(
        ok=True,
        elf_path=elf_path,
        arch=arch,
        addr2line=addr2line_bin,
        task=task,
        exc_cause=exc_cause,
        cause_name_str=cause_name(exc_cause, arch),
        app_sha256=app_sha,
        frames=labeled,
    )


class DecodeResult:
    """Result of decode_panic()."""
    def __init__(
        self,
        ok: bool,
        elf_path: str = "",
        arch: str = "xtensa",
        addr2line: str = "",
        task: str = "",
        exc_cause: int = 0,
        cause_name_str: str = "",
        app_sha256: str = "",
        frames: Optional[List[Tuple[str, int, str]]] = None,
        error: str = "",
    ):
        self.ok = ok
        self.elf_path = elf_path
        self.arch = arch
        self.addr2line = addr2line
        self.task = task
        self.exc_cause = exc_cause
        self.cause_name_str = cause_name_str
        self.app_sha256 = app_sha256
        self.frames = frames or []
        self.error = error
