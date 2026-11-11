#!/usr/bin/env python3
"""Compact KVXputer's shared option menus to the same 1x text as BLE.

Only changes shared menu-list renderers, file lists and regular option dialogs.
The BLE Spam screens use the same compact typography. Their controls and radio behavior are unchanged.
Operate on a disposable pinned upstream checkout, not the user's originals.
"""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
ui_path = root / "src/root/ui/kvx_ui.cpp"
display_path = root / "src/root/ui/display.cpp"

ui = ui_path.read_text(encoding="utf-8")
needle = "static void drawKvxSubmenuRow("
assert ui.count(needle) == 1, "Unexpected KVX submenu drawing code"
ui = ui.replace(needle, "static constexpr int kKvxMenuFont = 1;\n\n" + needle, 1)
old = "    tft.setTextSize(FM);\n    if (!options[i].enabled)"
assert ui.count(old) == 1, "Expected row text-size anchor missing"
ui = ui.replace(old, "    tft.setTextSize(kKvxMenuFont);\n    if (!options[i].enabled)", 1)
old = """    const int lineH = FM * LH + 4;
    const int startY = KVX_TOPBAR_H + 4;
    const int visible = max(1, (tftHeight - startY - 6) / lineH);
    int scroll = 0;
    if (index >= visible) scroll = index - visible + 1;
    int nchars = max(1, (tftWidth - 12) / (FM * LW));"""
new = """    // Same dense 1x font already approved in the Bluetooth submenu.
    const int lineH = kKvxMenuFont * LH + 4;
    const int startY = KVX_TOPBAR_H + 4;
    const int visible = max(1, (tftHeight - startY - 6) / lineH);
    int scroll = 0;
    if (index >= visible) scroll = index - visible + 1;
    int nchars = max(1, (tftWidth - 12) / (kKvxMenuFont * LW));"""
assert ui.count(old) == 1, "Unexpected KVX submenu layout"
ui = ui.replace(old, new, 1)
ui_path.write_text(ui, encoding="utf-8")

display = display_path.read_text(encoding="utf-8")
# The regular option dialog and the file list are also navigation menus;
# leave title-bars, text editors, and custom application canvases untouched.
old = "#define MAX_MENU_SIZE max(1, (int)(tftHeight / uiRowH(FM)))"
assert display.count(old) == 1, "Regular options capacity anchor missing"
display = display.replace(old,
    "#define MAX_MENU_SIZE max(1, (int)(tftHeight / uiRowH(1)))", 1)
for old in (
    "const int nchars = max(1, (tftWidth - 12) / (FM * LW));",
    "    int nchars = max(1, (tftWidth - 12) / (FM * LW));",
):
    assert display.count(old) == 1, "Expected unique compact menu width anchor"
    display = display.replace(old, old.replace("FM * LW", "1 * LW"), 1)
beg = display.index("Opt_Coord drawOptions(")
end = display.index("void drawSubmenu(", beg)
segment = display[beg:end]
assert "tft.setTextSize(FM)" in segment
segment = re.sub(r"\bFM\b", "1", segment)
display = display[:beg] + segment + display[end:]
beg = display.index("Opt_Coord listFiles(")
end = display.index("void drawSdSmall(", beg)
segment = display[beg:end]
assert "tft.setTextSize(FM)" in segment
segment = re.sub(r"\bFM\b", "1", segment)
display = display[:beg] + segment + display[end:]
display_path.write_text(display, encoding="utf-8")
print("KVX shared submenus, file lists and regular option dialogs: compact 1x font; custom screens unchanged.")

# The BLE Spam selector, configuration, running view and popups draw their
# own text outside the shared list renderers. Keep the title and row geometry
# in sync with the fixed 1x font on these views.
spam_path = root / "src/menu/ble/ble_spam.cpp"
spam = spam_path.read_text(encoding="utf-8")
start = spam.index("static BleSpamListMetrics bleSpamGetListMetrics")
prefix, screens = spam[:start], spam[start:]
assert screens.count("drawMainBorderWithTitle(bleSpamMakeTitle(title));") == 5
assert screens.count("tft.setTextSize(FP);") == 7
screens = re.sub(r"\b(?:FP|FM)\b", "kBleSpamFont", screens)
screens = screens.replace(
    "static String bleSpamMakeTitle(const String &title) {",
    "static void drawBleSpamTitle(const String &title) {\n"
    "    drawMainBorder();\n"
    "    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);",
)
screens = screens.replace(
    "return bleSpamTruncateText(title, maxWidth);",
    "tft.drawCentreString(bleSpamTruncateText(title, maxWidth), tftWidth / 2, BORDER_PAD_Y, 1);",
)
screens = screens.replace(
    "drawMainBorderWithTitle(bleSpamMakeTitle(title));", "drawBleSpamTitle(title);"
)
spam_path.write_text(prefix + "static constexpr int kBleSpamFont = 1;\n\n" + screens, encoding="utf-8")
