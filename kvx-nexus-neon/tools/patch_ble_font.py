#!/usr/bin/env python3
"""Patch KVXputer source in a disposable checkout, never upstream."""
from pathlib import Path
import sys
import re
root = Path(sys.argv[1]).resolve()
ui = root / "src/root/ui/kvx_ui.cpp"
display = root / "src/root/ui/display.cpp"
p = ui.read_text(encoding="utf-8")
anchor = "static void drawKvxSubmenuRow("
assert p.count(anchor) == 1
if "sKvxSubmenuFontSize" not in p:
    p = p.replace(anchor, "static int sKvxSubmenuFontSize = FM;\n\n" + anchor, 1)
    before = """    tft.setTextSize(FM);
    if (!options[i].enabled)"""
    assert p.count(before)==1
    p=p.replace(before, """    tft.setTextSize(sKvxSubmenuFontSize);
    if (!options[i].enabled)""",1)
    old = """    const int lineH = FM * LH + 4;
    const int startY = KVX_TOPBAR_H + 4;
    const int visible = max(1, (tftHeight - startY - 6) / lineH);
    int scroll = 0;
    if (index >= visible) scroll = index - visible + 1;
    int nchars = max(1, (tftWidth - 12) / (FM * LW));"""
    assert p.count(old)==1
    new = """    // Bluetooth menus need smaller text on the Cardputer's 240x135 screen.
    // Keep unrelated KVXputer menus at their original font size.
    const String requestedTitle = title ? String(title) : String();
    sKvxSubmenuFontSize =
        (requestedTitle == "Bluetooth" || requestedTitle.startsWith("BLE ")) ? 1 : FM;
    const int lineH = sKvxSubmenuFontSize * LH + 4;
    const int startY = KVX_TOPBAR_H + 4;
    const int visible = max(1, (tftHeight - startY - 6) / lineH);
    int scroll = 0;
    if (index >= visible) scroll = index - visible + 1;
    int nchars = max(1, (tftWidth - 12) / (sKvxSubmenuFontSize * LW));"""
    p=p.replace(old,new,1)
    ui.write_text(p,encoding="utf-8")

p=display.read_text(encoding="utf-8")
old = "            const int nchars = max(1, (tftWidth - 12) / (FM * LW));"
assert p.count(old) == 1
new = """            const int bleFont = (kvxActiveTitle == "Bluetooth" ||
                                 kvxActiveTitle.startsWith("BLE ")) ? 1 : FM;
            const int nchars = max(1, (tftWidth - 12) / (bleFont * LW));"""
p=p.replace(old,new,1)
display.write_text(p,encoding="utf-8")
print("BLE submenu font set to compact 1x; all non-BLE UI remains unchanged.")
