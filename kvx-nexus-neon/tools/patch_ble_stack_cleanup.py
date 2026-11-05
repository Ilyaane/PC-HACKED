#!/usr/bin/env python3
"""Release BLE Suite and generic BLE radio resources symmetrically.

This patch only touches radio lifecycle, not exploit or interference logic.
"""
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve()
p=root/"src/menu/ble/ble_common.cpp"
s=p.read_text(encoding="utf-8")
start=s.index("    if (is_ble_inited) {\n#if !defined(LITE_VERSION)")
end=s.index("    pServer = nullptr;",start)
old=s[start:end]
assert old.count("BLEStateManager::deinitBLE(true);")==1
assert old.count("BLEDevice::deinit();")==1
s=s[:start]+"""#if !defined(LITE_VERSION)
    // A Suite-owned NimBLE stack must be torn down even if the generic
    // is_ble_inited flag was never set by ble_scan_setup().
    if (BLEStateManager::isBLEActive()) {
        BLEStateManager::deinitBLE(true);
    } else if (is_ble_inited &&
               (BLEDevice::getScan()!=nullptr || BLEDevice::getAdvertising()!=nullptr ||
                BLEDevice::getServer()!=nullptr || BLEConnected)) {
        BLEDevice::deinit();
    }
#else
    if (is_ble_inited &&
        (BLEDevice::getScan()!=nullptr || BLEDevice::getAdvertising()!=nullptr ||
         BLEDevice::getServer()!=nullptr || BLEConnected)) {
        BLEDevice::deinit();
    }
#endif

"""+s[end:]
a="""    pBLEScan = BLEDevice::getScan();
    if (!pBLEScan) {
        displayError("Failed to get scan object", true);
        return false;
    }"""
b="""    pBLEScan = BLEDevice::getScan();
    if (!pBLEScan) {
        BLEDevice::deinit();
        is_ble_inited = false;
        uiRamLeaveHeavy();
        displayError("Failed to get scan object", true);
        return false;
    }"""
assert s.count(a)==1, "BLE scan error cleanup anchor mismatch"
s=s.replace(a,b,1)
p.write_text(s,encoding="utf-8")
print("BLE Suite cleanup enabled even when generic BLE scan state is false.")
