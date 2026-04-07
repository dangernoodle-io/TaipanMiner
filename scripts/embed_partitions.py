"""Pre-build script: convert partitions.bin to C source with const uint8_t array.

Reads partitions.bin from the PlatformIO build output and generates a
partition_fixup_data.c file in src/ that can be compiled as part of the app.
"""
import os

Import("env")

env_name = env.subst("$PIOENV")
bin_path = os.path.join(".pio", "build", env_name, "partitions.bin")
c_path = os.path.join("src", "partition_fixup_data.c")


def write_c_file(data):
    with open(c_path, "w") as out:
        out.write("// Auto-generated from partitions.bin — do not edit\n")
        out.write("#include <stdint.h>\n")
        out.write("const uint8_t g_partitions_bin[] = {\n")
        for i, b in enumerate(data):
            out.write(f"0x{b:02x}")
            if i < len(data) - 1:
                out.write(",")
            if (i + 1) % 16 == 0:
                out.write("\n")
        out.write("};\n")
        out.write(f"const unsigned int g_partitions_bin_len = {len(data)};\n")


if not os.path.exists(bin_path):
    # First build or native env: generate stub so compilation succeeds.
    # Fixup code treats len=0 as "table matches" (memcmp of 0 bytes = 0).
    if not os.path.exists(c_path):
        write_c_file(b"\x00")
        print(f"embed_partitions: {bin_path} not found, wrote stub")
else:
    # Skip if .c is newer than .bin
    if os.path.exists(c_path) and os.path.getmtime(c_path) >= os.path.getmtime(bin_path):
        pass
    else:
        with open(bin_path, "rb") as f:
            data = f.read()
        write_c_file(data)
        print(f"embed_partitions: partitions.bin -> {c_path} ({len(data)} bytes)")
