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
    if marker not in original and replacement not in original:
        raise RuntimeError("Keyboard signature changed: cannot safely patch visibility")
    updated=original
    if marker in updated:
        updated=updated.replace(marker,replacement)
    elif replacement not in updated:
        raise RuntimeError("Keyboard signature changed: cannot safely patch visibility")
    # Physical keyboards sometimes report several characters in one event.
    # Joining them with a '+' invents a character in passwords and SSIDs.
    old_join='''                String keyStr = "";
                for (auto i : KeyStroke.word) {
                    if (keyStr != "") {
                        keyStr = keyStr + "+" + i;
                    } else {
                        keyStr += i;
                    }
                }
'''
    new_join='''                String keyStr = "";
                for (auto i : KeyStroke.word) {
                    keyStr += i;
                }
'''
    if old_join in updated:
        updated=updated.replace(old_join,new_join)
    # A multi-character event must not overflow the maximum permitted size.
    old_append='''                    current_text += keyStr;
                    if (current_text.length() != (max_FM_size + 1) &&
                        current_text.length() != (max_FM_size + 1))'''
    new_append='''                    keyStr = keyStr.substring(
                        0, max_size - (int)current_text.length());
                    current_text += keyStr;
                    if (current_text.length() != (max_FM_size + 1) &&
                        current_text.length() != (max_FP_size + 1))'''
    if old_append in updated:
        updated=updated.replace(old_append,new_append)
    if updated!=original:
        source.write_text(updated,encoding="utf-8")

if __name__=="__main__":
    if len(sys.argv)!=2:
        sys.exit("usage: patch_keyboard_plaintext.py <kvx_root>")
    patch(Path(sys.argv[1]))
