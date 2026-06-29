"""Bootstrap breadboard at the pinned VERSION, then delegate reconcile to bbtool.
Standardized stub — the only value to edit is VERSION (and REPO if you fork).
Wire as: extra_scripts = pre:scripts/fetch_breadboard.py
See breadboard scripts/bbtool/README.md."""
Import("env")  # PlatformIO SCons pre-script
import os, sys, subprocess

VERSION = "8264eda53d733f8db6ffe76a6f5f17419ce2f291"  # pinned at bb #595 — bb_lint() cmake advisory lint target
REPO = "https://github.com/dangernoodle-io/breadboard.git"
DEST = os.path.join(env.subst("$PROJECT_DIR"), ".breadboard")
LOCAL = os.environ.get("BREADBOARD_LOCAL")

# Cold start: make a .breadboard exist so bbtool is importable. Minimal + irreducible.
import re as _re
if LOCAL and not os.path.exists(DEST):
    os.symlink(os.path.abspath(LOCAL), DEST)
elif not LOCAL and not os.path.exists(DEST):
    if _re.fullmatch(r"[0-9a-f]{40}", VERSION):
        os.makedirs(DEST)
        subprocess.check_call(["git", "-C", DEST, "init", "-q"])
        subprocess.check_call(["git", "-C", DEST, "remote", "add", "origin", REPO])
        subprocess.check_call(["git", "-C", DEST, "fetch", "--depth", "1", "origin", VERSION])
        subprocess.check_call(["git", "-C", DEST, "checkout", "-q", "FETCH_HEAD"])
    else:
        subprocess.check_call(["git", "clone", "--depth", "1", "--branch", VERSION, REPO, DEST])

# Delegate the rest (stamp reconcile, stale refetch, symlink idempotency) to bbtool.
sys.path.insert(0, os.path.join(DEST, "scripts", "bbtool"))
from commands.fetch import reconcile
reconcile(dest=DEST, version=VERSION, repo=REPO, local=LOCAL)
