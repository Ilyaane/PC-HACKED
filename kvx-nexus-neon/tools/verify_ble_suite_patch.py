#!/usr/bin/env python3
"""Fail CI if BLE Suite stabilization silently regressed or changed menu options."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
ble = (root / "src/menu/ble/BLE_Suite.cpp").read_text(encoding="utf-8")
common = (root / "src/menu/ble/ble_common.cpp").read_text(encoding="utf-8")
ble_menu = (root / "src/menu/ble/ble_menu.cpp").read_text(encoding="utf-8")

init = ble.split("bool BLEStateManager::initBLE(", 1)[1].split(
    "void BLEStateManager::deinitBLE(", 1
)[0]
deinit = ble.split("void BLEStateManager::deinitBLE(", 1)[1].split(
    "void BLEStateManager::registerClient(", 1
)[0]
menu = ble.split("void BleSuiteMenu()", 1)[1].split(
    "const char *getScanTitle(", 1
)[0]
scan_init = common.split("bool ble_scan_setup()", 1)[1].split(
    "namespace {", 1
)[0]
expected_labels = (
    "Quick Vulnerability Scan",
    "Deep Device Profiling",
    "FastPair Attack Suite",
    "HFP (Hands-Free) Suite",
    "Audio Suite",
    "HID Attack Suite",
    "Memory Corruption Suite",
    "DoS Attacks",
    "Payload Delivery",
    "Testing Tools",
    "Universal Attack Chain",
    "BLE Sniffer",
)

assert init.count("uiRamEnterHeavy();") == 1
assert init.count("bleInitialized && NimBLEDevice::isInitialized()") == 1
assert "if (!NimBLEDevice::isInitialized())" in init
assert deinit.count("uiRamLeaveHeavy();") == 1
assert "g_pBLEScan = nullptr;" in deinit
assert "g_bleScanActive = false;" in deinit
assert 'if (!BLEStateManager::initBLE("Bruce-BLESuite", ESP_PWR_LVL_P9))' in menu
assert "const int rowHeight = 12;" in menu
assert "const int firstRowY = 31;" in menu
assert "tft.setTextSize(1);" in menu
assert "int maxVisible = (tftHeight - 80) / 25" not in menu
welcome = ble.split("void showWelcomeScreen()", 1)[1].split("void BleSuiteMenu()", 1)[0]
assert "delay(2000);" not in welcome
assert "delay(120);" in welcome
assert "stopBLEStack(); // Deinit + release the UI RAM reservation." in scan_init
assert "if (is_ble_inited || BLEConnected)" in common
assert "stopBLEStack();" in ble_menu
assert "BLEDevice::deinit();" not in ble_menu.split("void BleMenu::optionsMenu()", 1)[1].split("void BleMenu::drawIcon", 1)[0]
clear = ble.split("void ScannerData::clear()", 1)[1].split("size_t ScannerData::size()", 1)[0]
assert clear.count(".shrink_to_fit();") == 6, "Scanner arrays still retain allocated capacity"
assert menu.count("if (check(EscPress))") >= 1
for label in expected_labels:
    assert menu.count(f'"{label}"') == 1, f"Missing/duplicated BLE Suite menu entry: {label}"
assert "executeAttackWithTargetScan(selected)" in menu
assert "BLE_Sniffer()" in menu
print(
    "BLE Suite checks PASS: 12 original options, 1x compact navigation, "
    "failed-init exit, balanced RAM ownership, failed-scan cleanup"
)
