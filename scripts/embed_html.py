"""Pre-build script: convert HTML files to C source with const char arrays.

Reads .html files from components/http_server/ and generates corresponding
_html.c files in components/http_server/src/ that can be compiled as part
of the component. This avoids PlatformIO's EMBED_TXTFILES path issues.
"""
import os

Import("env")

HTML_DIR = os.path.join("components", "http_server")
OUT_DIR = os.path.join("components", "http_server", "src")

FILES = [
    ("prov_form.html", "prov_form_html"),
    ("theme.css", "theme_css"),
    ("logo.svg", "logo_svg"),
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
        data = f.read()

    with open(c_path, "w") as out:
        out.write(f"// Auto-generated from {html_name} — do not edit\n")
        out.write(f"const char {var_name}[] = {{\n")
        for i, b in enumerate(data):
            out.write(f"0x{b:02x},")
            if (i + 1) % 16 == 0:
                out.write("\n")
        out.write("0x00};\n")
        out.write(f"const unsigned int {var_name}_len = sizeof({var_name}) - 1;\n")

    print(f"embed_html: {html_name} -> {c_path} ({len(data)} bytes)")
