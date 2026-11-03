#!/usr/bin/env python3
"""Apply a surgical macro-name conflict fix to scratch KVXputer's FastLED tree.

KVXputer's -DFP=2 and -DHAS_ENCODER=1 board flags are globally propagated
to FastLED 3.10+. FastLED uses FP as a C++ alias and HAS_ENCODER as a template
parameter in multiple internal headers, which causes errors throughout the
library. Rename only those two tokens consistently *within FastLED*.
No board macros, startup, main menu, input or existing KVX code are changed.
"""
from pathlib import Path
import sys

patch_script=Path(sys.argv[1])
source=patch_script.read_text(encoding="utf-8")
tag="# Nexus Neon KVX: consistent FastLED local identifiers (FP/HAS_ENCODER)"
if tag in source:
    print("FastLED patch already installed")
    sys.exit(0)
append = """
""" + tag + """
import pathlib
for root in glob.glob(".pio/libdeps/*/FastLED/src"):
    root_path=pathlib.Path(root)
    count=0
    for path in root_path.rglob("*"):
        if path.suffix.lower() not in {".h", ".hpp", ".cpp", ".c", ".inc", ".cxx"}:
            continue
        try:
            original=path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        updated=re.sub(r"\\bFP\\b", "FASTLED_FIXED_POINT_ALIAS", original)
        updated=re.sub(r"\\bHAS_ENCODER\\b", "FASTLED_TEMPLATE_HAS_ENCODER", updated)
        if updated!=original:
            path.write_text(updated, encoding="utf-8")
            count += 1
    print(f"Patched FastLED macro collisions in {count} internal source files")
"""
patch_script.write_text(source + "\n" + append,encoding="utf-8")
print("Installed consistent FastLED internal macro collision fix.")
