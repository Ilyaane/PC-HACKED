#!/usr/bin/env python3
"""Guard BLE Suite initialization and avoid stealing WiFi's memory lease."""
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve()
suite=root/"src/menu/ble/BLE_Suite.cpp"
s=suite.read_text(encoding="utf-8")
a="""    if (bleInitialized) return true; // Do not acquire a second heavy-RAM lease."""
b="""    if (bleInitialized) {
        // Another KVX module may have deinitialised NimBLE in the meantime.
        // Do not report success for a stale stack handle.
        if (NimBLEDevice::isInitialized()) return true;
        bleInitialized = false;
        currentDeviceName = "";
        uiRamLeaveHeavy();
    }"""
assert s.count(a)==1
s=s.replace(a,b,1)
a="""    std::string nameStr = name.c_str();
    NimBLEDevice::init(nameStr);
    NimBLEDevice::setPower((esp_power_level_t)powerLevel);

    currentDeviceName = name;"""
b="""    std::string nameStr = name.c_str();
    if (!NimBLEDevice::init(nameStr)) {
        uiRamLeaveHeavy();
        displayError("BLE initialization failed", true);
        return false;
    }
    NimBLEDevice::setPower((esp_power_level_t)powerLevel);

    currentDeviceName = name;"""
assert s.count(a)==1
s=s.replace(a,b,1)
suite.write_text(s,encoding="utf-8")

common=root/"src/menu/ble/ble_common.cpp"
s=common.read_text(encoding="utf-8")
a="""void stopBLEStack() {
    if (pBLEScan) {"""
b="""void stopBLEStack() {
    // The generic scan and BLE Suite each own a separate UI memory lease.
    // Suite deinit already releases its own. An unconditional second release
    // could otherwise drop a currently active WiFi lease by mistake.
    const bool genericBleHeldRam = is_ble_inited;
    if (pBLEScan) {"""
assert s.count(a)==1
s=s.replace(a,b,1)
start=s.index("void stopBLEStack() {")
end=s.index("bool ble_scan_setup()",start)
old=s[start:end]
assert old.count("    uiRamLeaveHeavy();")==1
old=old.replace("    uiRamLeaveHeavy();","    if (genericBleHeldRam) uiRamLeaveHeavy();",1)
s=s[:start]+old+s[end:]
common.write_text(s,encoding="utf-8")
print("BLE: stale-stack check, failed init handling, balanced radio memory release.")
