#!/usr/bin/env python3
"""Fix BLE Suite <-> WiFi transitions and symmetric memory cleanup.

Do not change BLE payloads, protocol operations or RF transmission capabilities.
Only lifecycle guards and missing cleanup paths.
"""
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve()
p=root/"src/menu/ble/ble_common.cpp"
s=p.read_text(encoding="utf-8")
a="""    if (is_ble_inited) {
#if !defined(LITE_VERSION)
        if (BLEStateManager::isBLEActive() || BLEStateManager::getActiveClientCount() > 0) {"""
b="""#if !defined(LITE_VERSION)
    if (is_ble_inited || BLEStateManager::isBLEActive() ||
        BLEStateManager::getActiveClientCount() > 0) {
        if (BLEStateManager::isBLEActive() || BLEStateManager::getActiveClientCount() > 0) {"""
assert s.count(a)==1, "BLE teardown guard changed"
s=s.replace(a,b,1)
a="""#endif
            if (BLEDevice::getScan() != nullptr || BLEDevice::getAdvertising() != nullptr ||
                BLEDevice::getServer() != nullptr || BLEConnected) {"""
b="""#endif
            if (BLEDevice::getScan() != nullptr || BLEDevice::getAdvertising() != nullptr ||
                BLEDevice::getServer() != nullptr || BLEConnected) {"""
assert s.count(a)==1
# Complete the matching outer #if block in the new guard.
anchor="""        }
    }

    pServer = nullptr;"""
assert s.count(anchor)==1
s=s.replace(anchor,"""        }
#else
    if (is_ble_inited) {
        if (BLEDevice::getScan() || BLEDevice::getAdvertising() ||
            BLEDevice::getServer() || BLEConnected) BLEDevice::deinit();
#endif
    }

    pServer = nullptr;""",1)
# Leak prevention when BLE scan initialisation failed after reserving RAM.
a="""    pBLEScan = BLEDevice::getScan();
    if (!pBLEScan) {
        displayError("Failed to get scan object", true);
        return false;
    }"""
b="""    pBLEScan = BLEDevice::getScan();
    if (!pBLEScan) {
        is_ble_inited = false;
        uiRamLeaveHeavy();
        displayError("Failed to get scan object", true);
        return false;
    }"""
assert s.count(a)==1
s=s.replace(a,b,1)
p.write_text(s,encoding="utf-8")
print("BLE generic teardown now checks Suite ownership; failed scan releases UI RAM.")
