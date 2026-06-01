Import("env")
import os, subprocess

VERSION = "v0.43.4"
DEST = os.path.join(env.subst("$PROJECT_DIR"), ".breadboard")
LOCAL = os.environ.get("BREADBOARD_LOCAL")
STAMP = os.path.join(DEST, ".version")


def stamp_matches():
    """True if a prior fetch left a .version stamp equal to the current pin."""
    try:
        with open(STAMP) as f:
            return f.read().strip() == VERSION
    except OSError:
        return False


if LOCAL:
    # Local override: symlink .breadboard at the working checkout so edits there
    # are picked up without a fetch. Always honored, regardless of any prior state.
    target = os.path.abspath(LOCAL)
    if os.path.islink(DEST) and os.path.realpath(DEST) == os.path.realpath(target):
        print(f"breadboard: .breadboard -> {target} (local, already linked)")
    else:
        if os.path.islink(DEST):
            os.unlink(DEST)
        elif os.path.exists(DEST):
            subprocess.check_call(["rm", "-rf", DEST])
        os.symlink(target, DEST)
        print(f"breadboard: linked .breadboard -> {target}")
elif os.path.islink(DEST):
    # A symlink from a prior BREADBOARD_LOCAL build, with no override this run.
    # Respect it (intentional local dev); `rm .breadboard` to return to the pin.
    print(f"breadboard: .breadboard symlink -> {os.path.realpath(DEST)} (left as-is)")
elif os.path.isdir(os.path.join(DEST, "components")) and stamp_matches():
    print(f"breadboard: .breadboard at {VERSION} (up to date)")
else:
    # Missing, or a stale fetch from a different pin. Re-fetch so the build never
    # silently links against breadboard code that doesn't match VERSION (this is
    # how a no-PSRAM OTA-pull fix once shipped missing from local dev images).
    if os.path.exists(DEST):
        print(f"breadboard: .breadboard does not match {VERSION}; refetching")
        subprocess.check_call(["rm", "-rf", DEST])
    subprocess.check_call([
        "git", "clone", "--depth", "1", "--branch", VERSION,
        "https://github.com/dangernoodle-io/breadboard.git", DEST,
    ])
    with open(STAMP, "w") as f:
        f.write(VERSION + "\n")
    print(f"breadboard: fetched {VERSION}")
