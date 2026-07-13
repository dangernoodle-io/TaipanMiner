"""Native-env-only PlatformIO pre-script.

bb_json's CMakeLists.txt (the ESP-IDF/idf_component_register build) compiles
components/bb_json/src/bb_json.c together with
platform/espidf/bb_json/bb_json_cjson.c as ONE component -- the latter file
is portable (guarded internally; the platform/espidf/ path is historical
naming, matching several other bb_* components -- see bb_log's own
CMakeLists.txt comment) but lives outside components/bb_json/'s own
directory tree.

PlatformIO's Library Dependency Finder (used by this project's [env:native]
-- see platformio.ini's `lib_extra_dirs` comment) treats each
lib_extra_dirs subdirectory as an independent library scoped to its own
folder; it has no notion of a CMakeLists.txt-style SRCS list spanning two
directories. Symlinking the one file this project actually needs into
bb_json's own src/ folder (native env only; never touches the ESP-IDF/CMake
build, which reads CMakeLists.txt directly) lets LDF's single-folder model
see the whole component.
"""
Import("env")  # noqa: F821
import os

DEST = os.path.join(env.subst("$PROJECT_DIR"), ".breadboard")  # noqa: F821
src = os.path.join(DEST, "platform", "espidf", "bb_json", "bb_json_cjson.c")
dst_dir = os.path.join(DEST, "components", "bb_json", "src")
dst = os.path.join(dst_dir, "bb_json_cjson.c")

if os.path.exists(src) and not os.path.exists(dst):
    os.makedirs(dst_dir, exist_ok=True)
    os.symlink(src, dst)

# bb_json_cjson.c's error path calls bb_log_e(), which on host (see
# bb_log.h's non-ESP_PLATFORM/non-Arduino branch) expands directly to a
# fprintf() macro -- zero linkage into the real bb_log component. But
# PlatformIO's LDF does plain textual #include scanning (no preprocessing),
# so it still treats bb_log as a hard dependency and tries to compile its
# whole library, which needs bb_str (and more) that this native env has no
# reason to pull in just to log one rare JSON parse-error path. A
# library.json with an empty srcFilter makes LDF treat bb_log as
# header-only for the native env -- the ESP-IDF/CMake build (which reads
# CMakeLists.txt, not library.json) is completely unaffected.
bb_log_dir = os.path.join(DEST, "components", "bb_log")
bb_log_manifest = os.path.join(bb_log_dir, "library.json")
if os.path.isdir(bb_log_dir) and not os.path.exists(bb_log_manifest):
    with open(bb_log_manifest, "w") as f:
        f.write('{"name": "bb_log", "build": {"srcFilter": ["-<*>"]}}\n')
