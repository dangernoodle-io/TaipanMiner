"""Pre-build script: convert HTML files to C source with gzip-compressed byte arrays.

Reads .html files from components/http_server/ and generates corresponding
_html.c files in components/http_server/src/ that can be compiled as part
of the component. This avoids PlatformIO's EMBED_TXTFILES path issues.
Compresses with gzip (level 9) before embedding.
"""
import os
import gzip
import sys

# Support both standalone execution and PlatformIO pre-build script usage
try:
    Import("env")
except NameError:
    # Running standalone, not in PlatformIO SCons context
    pass

HTML_DIR = os.path.join("components", "taipan_web")
OUT_DIR = os.path.join("components", "taipan_web", "src")

FILES = [
    ("prov_form.html", "prov_form_html_gz"),
    ("theme.css", "theme_css_gz"),
    ("logo.svg", "logo_svg_gz"),
    ("favicon.svg", "favicon_svg_gz"),
    ("mining.html", "mining_html_gz"),
    ("mining.js", "mining_js_gz"),
    ("prov_save.html", "prov_save_html_gz"),
]


for html_name, var_name in FILES:
    html_path = os.path.join(HTML_DIR, html_name)
    c_path = os.path.join(OUT_DIR, f"{var_name}.c")

    if not os.path.exists(html_path):
        print(f"embed_html: {html_path} not found, skipping")
        continue

    # Skip if .c is newer than .html
    if os.path.exists(c_path):
        if os.path.getmtime(c_path) >= os.path.getmtime(html_path):
            continue

    with open(html_path, "rb") as f:
        raw_data = f.read()

    # Compress with gzip at maximum compression
    data = gzip.compress(raw_data, compresslevel=9)

    with open(c_path, "w") as out:
        out.write(f"// Auto-generated from {html_name} (gzip-compressed) — do not edit\n")
        out.write(f"const unsigned char {var_name}[] = {{\n")
        for i, b in enumerate(data):
            out.write(f"0x{b:02x},")
            if (i + 1) % 16 == 0:
                out.write("\n")
        out.write("};\n")
        out.write(f"const unsigned int {var_name}_len = sizeof({var_name});\n")

    print(f"embed_html: {html_name} -> {c_path} ({len(raw_data)} bytes -> {len(data)} bytes compressed)")
