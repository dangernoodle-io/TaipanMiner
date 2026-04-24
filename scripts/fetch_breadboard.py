Import("env")
import os, subprocess

VERSION = "v0.12.0"
DEST = os.path.join(env.subst("$PROJECT_DIR"), ".breadboard")
LOCAL = os.environ.get("BREADBOARD_LOCAL")

if os.path.isdir(os.path.join(DEST, "components")):
    pass  # symlink or prior fetch already in place
elif LOCAL:
    target = os.path.abspath(LOCAL)
    if os.path.islink(DEST) or os.path.exists(DEST):
        if os.path.islink(DEST):
            os.unlink(DEST)
        else:
            subprocess.check_call(["rm", "-rf", DEST])
    os.symlink(target, DEST)
    print(f"breadboard: linked .breadboard -> {target}")
else:
    subprocess.check_call([
        "git", "clone", "--depth", "1", "--branch", VERSION,
        "https://github.com/dangernoodle-io/breadboard.git", DEST,
    ])
