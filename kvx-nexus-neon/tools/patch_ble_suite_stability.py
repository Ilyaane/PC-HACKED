#!/usr/bin/env python3
"""Non-invasive BLE Suite and generic BLE scan stability fixes.

Applied only to the disposable pinned KVXputer checkout during CI.
No radio packet formats, exploit implementations, targets or attack methods are altered.
"""
from __future__ import annotations
from pathlib import Path
import sys

def replace_once(src: str, old: str, new: str, context: str) -> str:
    if src.count(old) != 1:
        raise AssertionError(
            f"{context}: unexpected upstream source, anchor count={src.count(old)}"
        )
    return src.replace(old, new, 1)

def patch(root: Path) -> None:
    suite_path = root / "src/menu/ble/BLE_Suite.cpp"
    common_path = root / "src/menu/ble/ble_common.cpp"
    ble_menu_path = root / "src/menu/ble/ble_menu.cpp"
    suite = suite_path.read_text(encoding="utf-8")
    common = common_path.read_text(encoding="utf-8")
    ble_menu = ble_menu_path.read_text(encoding="utf-8")

    # Calling initBLE() repeatedly must not consume one extra UI memory
    # suppression reference each time, nor tear down a working Wi-Fi link.
    suite = replace_once(
        suite,
        'bool BLEStateManager::initBLE(const String &name, int powerLevel) {\n'
        '    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {',
        'bool BLEStateManager::initBLE(const String &name, int powerLevel) {\n'
        '    if (bleInitialized && NimBLEDevice::isInitialized()) {\n'
        '        // Already active: keep exactly one radio/UI RAM reservation.\n'
        '        return true;\n'
        '    }\n'
        '    if (bleInitialized) {\n'
        '        // NimBLE was shut down outside this manager. Repair stale state.\n'
        '        bleInitialized = false;\n'
        '        uiRamLeaveHeavy();\n'
        '    }\n'
        '    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {',
        "BLE Suite repeated-init guard"
    )
    suite = replace_once(
        suite,
        '    NimBLEDevice::init(nameStr);\n'
        '    NimBLEDevice::setPower((esp_power_level_t)powerLevel);',
        '    NimBLEDevice::init(nameStr);\n'
        '    if (!NimBLEDevice::isInitialized()) {\n'
        '        // Release only our own reservation after failed initialization.\n'
        '        uiRamLeaveHeavy();\n'
        '        return false;\n'
        '    }\n'
        '    NimBLEDevice::setPower((esp_power_level_t)powerLevel);',
        "BLE initialization validation"
    )
    suite = replace_once(
        suite,
        '    g_pBLEScan = nullptr;\n'
        '    g_bleScanActive = false;\n'
        '}\n\n'
        'void BLEStateManager::registerClient(',
        '    g_pBLEScan = nullptr;\n'
        '    g_bleScanActive = false;\n'
        '    // Pairs with the only successful uiRamEnterHeavy() in initBLE.\n'
        '    uiRamLeaveHeavy();\n'
        '}\n\n'
        'void BLEStateManager::registerClient(',
        "BLE exit memory cleanup"
    )

    # "clear()" leaves vector capacities allocated in internal RAM. Give
    # this memory back when leaving the suite so Nexus/MQTT aren't starved.
    suite = replace_once(
        suite,
        '        deviceNames.clear();\n'
        '        deviceAddresses.clear();\n'
        '        deviceRssi.clear();\n'
        '        deviceFastPair.clear();\n'
        '        deviceHasHFP.clear();\n'
        '        deviceTypes.clear();\n'
        '        foundCount = 0;',
        '        deviceNames.clear(); deviceNames.shrink_to_fit();\n'
        '        deviceAddresses.clear(); deviceAddresses.shrink_to_fit();\n'
        '        deviceRssi.clear(); deviceRssi.shrink_to_fit();\n'
        '        deviceFastPair.clear(); deviceFastPair.shrink_to_fit();\n'
        '        deviceHasHFP.clear(); deviceHasHFP.shrink_to_fit();\n'
        '        deviceTypes.clear(); deviceTypes.shrink_to_fit();\n'
        '        foundCount = 0;',
        "BLE scanner cache releases RAM when cleared"
    )

    # A welcome animation is not a reason to freeze keyboard input for two
    # seconds, especially on the tiny Cardputer screen.
    suite = replace_once(
        suite,
        '    delay(2000);\n\n    welcomeShown = true;',
        '    delay(120);\n\n    welcomeShown = true;',
        "BLE Suite non-blocking welcome"
    )

    # Do not enter the suite after an unsuccessful BLE stack initialization.
    suite = replace_once(
        suite,
        'void BleSuiteMenu() {\n'
        '    // FIX: Init BLE stack ONCE when entering the suite\n'
        '    BLEStateManager::initBLE("Bruce-BLESuite", ESP_PWR_LVL_P9);',
        'void BleSuiteMenu() {\n'
        '    // A failed stack allocation must return to KVX, not leave a dead menu.\n'
        '    if (!BLEStateManager::initBLE("Bruce-BLESuite", ESP_PWR_LVL_P9)) {\n'
        '        return;\n'
        '    }',
        "BLE Suite failed-init exit"
    )

    # Match only the main BLE Suite grid, never other scan/diagnostic views.
    menu_start = suite.index("void BleSuiteMenu()")
    menu_end = suite.index("const char *getScanTitle(", menu_start)
    menu = suite[menu_start:menu_end]
    # The original 25px row height shows only TWO items on a 135px display.
    # A compact 12px row with a 1x font shows six items while keeping footer.
    menu = replace_once(
        menu,
        '    int maxVisible = (tftHeight - 80) / 25;',
        '    const int rowHeight = 12;\n'
        '    const int firstRowY = 31;\n'
        '    const int footerTopY = tftHeight - 23;\n'
        '    const int maxVisible = max(1, (footerTopY - firstRowY) / rowHeight);',
        "BLE Suite compact viewport"
    )
    menu = replace_once(
        menu,
        '            tft.setTextSize(2);\n'
        '            tft.setCursor((tftWidth - tft.textWidth("BLE SUITE")) / 2, 15);\n'
        '            tft.print("BLE SUITE");\n'
        '            tft.setTextSize(uiDenseFont()) /* BLE Suite custom HUD density */;',
        '            tft.setTextSize(1);\n'
        '            tft.setCursor((tftWidth - tft.textWidth("BLE SUITE")) / 2, 13);\n'
        '            tft.print("BLE SUITE");\n'
        '            tft.setTextSize(1);',
        "BLE Suite title and list font"
    )
    menu = replace_once(
        menu,
        '                int yPos = 60 + (i * 25);',
        '                int yPos = firstRowY + (i * rowHeight);',
        "BLE Suite row positioning"
    )
    menu = replace_once(
        menu,
        'tft.fillRect(20, yPos, tftWidth - 40, 20, TFT_WHITE);',
        'tft.fillRect(13, yPos, tftWidth - 26, rowHeight - 1, TFT_WHITE);',
        "BLE Suite selected row background"
    )
    menu = replace_once(
        menu,
        'tft.fillRect(20, yPos, tftWidth - 40, 20, kvxConfig.bgColor);',
        'tft.fillRect(13, yPos, tftWidth - 26, rowHeight - 1, kvxConfig.bgColor);',
        "BLE Suite non-selected row background"
    )
    cursor_anchor = 'tft.setCursor(25, yPos + 5);'
    assert menu.count(cursor_anchor) == 2, "BLE Suite row cursor count changed"
    menu = menu.replace(cursor_anchor, 'tft.setCursor(17, yPos + 2);')
    menu = replace_once(
        menu,
        '                tft.print(String(idx + 1) + ". " + menuItems[idx]);',
        '                String label = String(idx + 1) + ". " + menuItems[idx];\n'
        '                const int maxChars = max(1, (tftWidth - 35) / 6);\n'
        '                if ((int)label.length() > maxChars) label = label.substring(0, maxChars - 1) + "~";\n'
        '                tft.print(label);',
        "BLE Suite label clipping"
    )
    menu = replace_once(
        menu,
        '                tft.setCursor(tftWidth - 25, 65);\n'
        '                if (scrollOffset > 0) tft.print("^");\n'
        '                tft.setCursor(tftWidth - 25, 65 + (maxVisible * 25) - 10);',
        '                tft.setCursor(tftWidth - 19, firstRowY + 2);\n'
        '                if (scrollOffset > 0) tft.print("^");\n'
        '                tft.setCursor(tftWidth - 19, firstRowY + maxVisible * rowHeight - 9);',
        "BLE Suite scroll indicators"
    )
    menu = replace_once(
        menu,
        '            tft.setCursor(20, tftHeight - 30);\n'
        '            tft.print("SEL: Select  PREV/NEXT: Navigate");\n'
        '            tft.setCursor(20, tftHeight - 20);\n'
        '            tft.print("ESC: Back");',
        '            tft.setCursor(13, tftHeight - 20);\n'
        '            tft.print("FLECHES: naviguer  ENTREE: ouvrir");\n'
        '            tft.setCursor(13, tftHeight - 11);\n'
        '            tft.print("ESC: retour KVX");',
        "BLE Suite compact footer"
    )

    suite = suite[:menu_start] + menu + suite[menu_end:]

    # BLE scan initialization has to undo its RAM reservation when the
    # scan object cannot be obtained (without changing scanning behavior).
    common = replace_once(
        common,
        '    if (!pBLEScan) {\n'
        '        displayError("Failed to get scan object", true);\n'
        '        return false;\n'
        '    }',
        '    if (!pBLEScan) {\n'
        '        displayError("Failed to get scan object", true);\n'
        '        stopBLEStack(); // Deinit + release the UI RAM reservation.\n'
        '        return false;\n'
        '    }',
        "BLE scan failed allocation cleanup"
    )
    # The root Bluetooth menu used a separate ad-hoc deinit path.
    # Route its Disconnect action through the one shared cleanup function.
    ble_menu = replace_once(
        ble_menu,
        '                               BLEDevice::deinit();\n'
        '                               BLEConnected = false;\n'
        '                               delete hid_ble;\n'
        '                               hid_ble = nullptr;',
        '                               stopBLEStack();',
        "Bluetooth menu shared teardown"
    )
    # A BLE HID session can mark BLEConnected without setting the legacy
    # scan-only is_ble_inited flag, so Disconnect must still deinit its stack.
    common = replace_once(
        common,
        '    if (is_ble_inited) {',
        '    if (is_ble_inited || BLEConnected) {',
        "Bluetooth disconnect catches non-scan BLE sessions"
    )
    ble_menu_path.write_text(ble_menu, encoding="utf-8")
    suite_path.write_text(suite, encoding="utf-8")
    common_path.write_text(common, encoding="utf-8")
    print("BLE Suite: compact six-row menu and matched init/deinit memory reservations")
    print("BLE Scan: failed scan-object allocation now releases the radio and UI RAM")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_ble_suite_stability.py /path/to/kvxputer")
    patch(Path(sys.argv[1]).resolve())
