#!/usr/bin/env python3
"""Patch only KVXputer's scratch checkout FastLED dependency conflicts.

Upstream board flags use -DFP=2 and -DHAS_ENCODER=1. FastLED 3.10+
declares identifiers with exactly those names in unrelated internal C++
templates, so replacing those local identifiers avoids global macro damage
without changing board controls or other upstream components.
"""
from pathlib import Path
import sys

patch_script=Path(sys.argv[1])
source=patch_script.read_text(encoding="utf-8")
tag="# Nexus Neon KVX: FastLED internal names and board macro collisions"
if tag in source:
    print("FastLED patch already installed")
    sys.exit(0)
append = """
""" + tag + """
for file_pattern, original, new in [
    (".pio/libdeps/*/FastLED/src/fl/chipsets/clockless_encoder.h", "HAS_ENCODER", "FASTLED_TEMPLATE_ENCODER"),
    (".pio/libdeps/*/FastLED/src/fl/audio/fft/fft_impl.cpp.hpp", "FP", "FASTLED_FFT_FP"),
]:
    for path in glob.glob(file_pattern):
        with open(path, encoding="utf-8") as file:
            old_content = file.read()
        updated = re.sub(r"\\\\b" + original + r"\\\\b", new, old_content)
        if updated != old_content:
            with open(path, "w", encoding="utf-8") as file:
                file.write(updated)
            print(f"Patched FastLED board-name collision: {path} ({original} -> {new})")
"""
# Caution: the source string assembled here should contain regex \b and not \\b.
append = append.replace('r"\\\\b"', 'r"\\b"')
assert 'r"\\b"' in append
patch_script.write_text(source + "\n" + append,encoding="utf-8")
print("Installed targeted FastLED macro collision patch into pre-build script.")
