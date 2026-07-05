"""tm_codegen.py — PlatformIO pre-script driving `bbtool codegen` for
TaipanMiner's own composition root (src/tm_wire.h + bbtool.toml).

breadboard's `commands.codegen.pio_main` exists but is NOT yet wired into
`scripts/bbtool_pio.py` (B1-1085 — see .breadboard/scripts/bbtool/README.md's
"`pio_main` (not yet wired)" note) and has no `--consumer-manifest` support
either, so this project drives the CLI directly instead, mirroring the exact
invocation breadboard's own Makefile uses for its `smoke-gen-*` targets:

  python3 .breadboard/scripts/bbtool.py codegen --root . \\
      --board <PIOENV> --extra-root .breadboard \\
      --consumer-manifest src/tm_wire.h \\
      --components-out src/generated/bb_autowire_components.cmake \\
      --wire-out src/generated/bb_app_init.c

Runs as a subprocess (not an in-process import) so a codegen failure's
stdout/stderr surface exactly as `make smoke-gen-*` would show them, and so
this stays a thin, easily-standalone-runnable wrapper (see this file's own
invocation documented in bbtool.toml/CLAUDE.md for reproducing it by hand).

Wire as (after fetch_breadboard.py/bbtool_pio.py, both of which must run
first — this needs .breadboard/scripts/bbtool.py to exist):

  extra_scripts =
      pre:scripts/fetch_breadboard.py
      pre:.breadboard/scripts/bbtool_pio.py
      pre:scripts/tm_codegen.py

Only runs for boards with a `[board.<PIOENV>]` table in the project's own
bbtool.toml (native/host envs have none — skipped as a no-op, matching
bbtool_pio.py's own custom_bb_board opt-in pattern).
"""
import os
import subprocess
import sys
import tomllib

Import("env")  # noqa: F821  -- PlatformIO SCons pre-script

project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
pioenv = env.subst("$PIOENV")  # noqa: F821

bbtool_toml = os.path.join(project_dir, "bbtool.toml")
if not os.path.exists(bbtool_toml):
    sys.exit(0)

with open(bbtool_toml, "rb") as fh:
    config = tomllib.load(fh)

if pioenv not in (config.get("board", {}) or {}):
    # No manifest entry for this env (e.g. `native`) -- codegen is
    # ESP-IDF-composition-only; nothing to regenerate for a host build.
    sys.exit(0)

bbtool_py = os.path.join(project_dir, ".breadboard", "scripts", "bbtool.py")
gen_dir = os.path.join(project_dir, "src", "generated")

cmd = [
    sys.executable, bbtool_py, "codegen",
    "--root", project_dir,
    "--board", pioenv,
    "--extra-root", os.path.join(project_dir, ".breadboard"),
    "--consumer-manifest", os.path.join(project_dir, "src", "tm_wire.h"),
    "--components-out", os.path.join(gen_dir, "bb_autowire_components.cmake"),
    "--wire-out", os.path.join(gen_dir, "bb_app_init.c"),
]
print("tm_codegen: " + " ".join(cmd))
result = subprocess.run(cmd)
if result.returncode != 0:
    env.Exit(result.returncode)  # noqa: F821
