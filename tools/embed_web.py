"""Packt docs/index.html gzip-komprimiert in include/dashboard_html.h.

Wird von PlatformIO vor jedem Build automatisch ausgeführt (extra_scripts),
kann aber auch direkt aufgerufen werden:  python tools/embed_web.py
"""
import gzip
import os

try:
    Import("env")  # noqa: F821  (nur unter PlatformIO vorhanden)
    ROOT = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

src = os.path.join(ROOT, "docs", "index.html")
dst = os.path.join(ROOT, "include", "dashboard_html.h")

with open(src, "rb") as f:
    data = gzip.compress(f.read(), compresslevel=9, mtime=0)

lines = []
for i in range(0, len(data), 20):
    lines.append("  " + ", ".join("0x%02x" % b for b in data[i:i + 20]) + ",")

out = (
    "#pragma once\n"
    "// Automatisch erzeugt aus docs/index.html durch tools/embed_web.py - nicht von Hand bearbeiten.\n"
    "#include <Arduino.h>\n"
    "static const uint8_t DASHBOARD_HTML_GZ[] PROGMEM = {\n" + "\n".join(lines) + "\n};\n"
)

old = open(dst).read() if os.path.exists(dst) else None
if old != out:
    with open(dst, "w") as f:
        f.write(out)
    print("embed_web: %s aktualisiert (%d Bytes gzip)" % (dst, len(data)))
