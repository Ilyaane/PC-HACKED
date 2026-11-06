#!/usr/bin/env python3
"""Render keyboard entry in clear text throughout KVXputer.

The operator requested visible characters in every credential form.
This changes display behavior only; it does not print values to logs.
"""
from pathlib import Path
import sys

def patch(kvx_root: Path) -> None:
    source=kvx_root / "src/root/input/mykeyboard.cpp"
    original=source.read_text(encoding="utf-8")
    marker=("    bool mask_input = false\n"
            ") {\n"
            "    max_FM_size = tftWidth / (LW * FM) - 1;")
    replacement=("    bool mask_input = false\n"
                 ") {\n"
                 "    // Intentionally show input as characters, not stars.\n"
                 "    mask_input = false;\n"
                 "    max_FM_size = tftWidth / (LW * FM) - 1;")
    if replacement in original:
        return
    if original.count(marker)!=1:
        raise RuntimeError("Keyboard signature changed: cannot safely patch visibility")
    source.write_text(original.replace(marker,replacement),encoding="utf-8")

if __name__=="__main__":
    if len(sys.argv)!=2:
        sys.exit("usage: patch_keyboard_plaintext.py <kvx_root>")
    patch(Path(sys.argv[1]))
