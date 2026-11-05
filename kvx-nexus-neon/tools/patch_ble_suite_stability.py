#!/usr/bin/env python3
"""Fix passive BLE Suite lifecycle and compact list UI on Cardputer ADV.

Only changes scanner initialization, shared BLE memory cleanup, and menu drawing.
No changes to attack, spoofing, injection, DoS or interference routines.
"""
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve()
source=root/"src/menu/ble/BLE_Suite.cpp"
s=source.read_text(encoding="utf-8")

def change(a,b):
    global s
    assert s.count(a)==1, f"BLE Suite anchor changed: {a[:110]!r} ({s.count(a)})"
    s=s.replace(a,b,1)

# Nested initialisations previously incremented the RAM-heavy lease repeatedly.
change(
    "bool BLEStateManager::initBLE(const String &name, int powerLevel) {\n",
    "bool BLEStateManager::initBLE(const String &name, int powerLevel) {\n"
    "    if (bleInitialized) return true; // Do not acquire a second heavy-RAM lease.\n"
)
# Ensure canvas suppression is always released when this suite owns the stack.
change(
    '    g_pBLEScan = nullptr;\n    g_bleScanActive = false;\n}\n\nvoid BLEStateManager::registerClient',
    '    g_pBLEScan = nullptr;\n    g_bleScanActive = false;\n'
    '    uiRamLeaveHeavy(); // Restore regular KVX UI after BLE Suite exits.\n'
    '}\n\nvoid BLEStateManager::registerClient'
)
# Don't continue with a half-initialised radio when memory is tight.
change(
    '    BLEStateManager::initBLE("Bruce-BLESuite", ESP_PWR_LVL_P9);\n\n    // Clear data',
    '    if (!BLEStateManager::initBLE("Kvx-BLESuite", ESP_PWR_LVL_P9)) {\n'
    '        displayError("BLE indisponible : memoire / radio", true);\n'
    '        return;\n'
    '    }\n\n    // Clear data'
)
change(
    '    BLEStateManager::initBLE("BruceSniffer", ESP_PWR_LVL_P9);\n    NimBLEScan *pScan',
    '    if (!BLEStateManager::initBLE("Kvx-Sniffer", ESP_PWR_LVL_P9)) {\n'
    '        displayError("BLE indisponible : memoire / radio", true);\n'
    '        return;\n'
    '    }\n    NimBLEScan *pScan'
)
# Keep splash compact; remove long blocking splash animation.
change(
    '    tft.setTextSize(4);\n    tft.setTextColor(TFT_PURPLE, kvxConfig.bgColor);',
    '    tft.setTextSize(1);\n    tft.setTextColor(TFT_PURPLE, kvxConfig.bgColor);'
)
change(
    '    tft.setTextSize(2);\n    tft.setCursor((tftWidth - tft.textWidth("BLE SUITE")) / 2, 80);',
    '    tft.setTextSize(1);\n    tft.setCursor((tftWidth - tft.textWidth("BLE SUITE")) / 2, 80);'
)
change("    tft.setTextSize(1.5);\n", "    tft.setTextSize(1);\n")
change("    delay(2000);\n\n    welcomeShown = true;", "    delay(220);\n\n    welcomeShown = true;")

start=s.index("void BleSuiteMenu() {")
end=s.index("//=============================================================================\n// Attack Execution with Target Selection",start)
frag=s[start:end]
old="    int maxVisible = (tftHeight - 80) / 25;"
assert frag.count(old)==1
frag=frag.replace(old,'''    // 135px display: six readable 1x rows instead of two 25px rows.
    const int menuTop = 32;
    const int rowH = 13;
    const int maxVisible = max(1, (tftHeight - menuTop - 24) / rowH);''')
frag=frag.replace('            tft.setTextSize(2);\n', '            tft.setTextSize(1);\n',1)
frag=frag.replace('            tft.setTextSize(uiDenseFont()) /* BLE Suite custom HUD density */;', '            tft.setTextSize(1);')
frag=frag.replace('                int yPos = 60 + (i * 25);','                int yPos = menuTop + (i * rowH);')
frag=frag.replace('tft.fillRect(20, yPos, tftWidth - 40, 20,', 'tft.fillRect(10, yPos, tftWidth - 20, rowH - 1,')
frag=frag.replace('tft.setCursor(25, yPos + 5);','tft.setCursor(14, yPos + 2);')
frag=frag.replace('tft.setCursor(tftWidth - 25, 65);','tft.setCursor(tftWidth - 13, menuTop);')
frag=frag.replace('tft.setCursor(tftWidth - 25, 65 + (maxVisible * 25) - 10);','tft.setCursor(tftWidth - 13, menuTop + (maxVisible - 1) * rowH);')
frag=frag.replace('tft.setCursor(20, tftHeight - 30);','tft.setCursor(9, tftHeight - 22);')
frag=frag.replace('tft.setCursor(20, tftHeight - 20);','tft.setCursor(9, tftHeight - 12);')
frag=frag.replace('tft.print("SEL: Select  PREV/NEXT: Navigate");','tft.print("ENTREE ouvrir  Fleches naviguer");')
frag=frag.replace('tft.print("ESC: Back");','tft.print("ESC: retour KVX");')
assert "int yPos = 60 + (i * 25)" not in frag
s=s[:start]+frag+s[end:]

start=s.index("int showSubMenu(const char *title, const char *options[], int optionCount) {")
end=s.index("//=============================================================================\n// Attack Submenus",start)
frag=s[start:end]
frag=frag.replace('    tft.setTextSize(2);','    tft.setTextSize(1);',1)
frag=frag.replace('    tft.setTextSize(uiDenseFont()) /* BLE Suite custom HUD density */;', '    tft.setTextSize(1);')
old='    int maxVisible = (tftHeight - 80) / 25;'
assert frag.count(old)==1
frag=frag.replace(old,'''    const int menuTop = 32;
    const int rowH = 13;
    const int maxVisible = max(1, (tftHeight - menuTop - 24) / rowH);''')
frag=frag.replace('tft.fillRect(20, 60, tftWidth - 40, tftHeight - 115, kvxConfig.bgColor);',
                  'tft.fillRect(10, menuTop, tftWidth - 20, maxVisible * rowH, kvxConfig.bgColor);')
frag=frag.replace('                int yPos = 60 + (i * 25);','                int yPos = menuTop + (i * rowH);')
frag=frag.replace('tft.fillRect(20, yPos, tftWidth - 40, 20,','tft.fillRect(10, yPos, tftWidth - 20, rowH - 1,')
frag=frag.replace('tft.setCursor(25, yPos + 5);','tft.setCursor(14, yPos + 2);')
frag=frag.replace('tft.setCursor(tftWidth - 25, 65);','tft.setCursor(tftWidth - 13, menuTop);')
frag=frag.replace('tft.setCursor(tftWidth - 25, 65 + (maxVisible * 25) - 10);',
                  'tft.setCursor(tftWidth - 13, menuTop + (maxVisible - 1) * rowH);')
frag=frag.replace('tft.setCursor(20, tftHeight - 30);','tft.setCursor(9, tftHeight - 22);')
frag=frag.replace('tft.setCursor(20, tftHeight - 20);','tft.setCursor(9, tftHeight - 12);')
frag=frag.replace('tft.print("SEL: Select  PREV/NEXT: Navigate");','tft.print("ENTREE ouvrir  Fleches naviguer");')
frag=frag.replace('tft.print("ESC: Back");','tft.print("ESC: retour");')
s=s[:start]+frag+s[end:]
source.write_text(s,encoding="utf-8")
print("BLE Suite: startup guard, RAM lease cleanup, 1x layout, ~6 visible rows.")
