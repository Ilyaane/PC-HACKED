#!/usr/bin/env python3
"""Static integration contract checks against patched disposable KVX checkout."""
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve()
def content(p): return (root/p).read_text(encoding="utf-8")

ble=content("src/menu/ble/BLE_Suite.cpp")
generic=content("src/menu/ble/ble_common.cpp")
wifi=content("src/root/net/wifi_common.cpp")
ui=content("src/root/ui/kvx_ui.cpp")
display=content("src/root/ui/display.cpp")
nexus=content("src/menu/nexus_neon/nexus_neon_app.cpp")
mqtt=content("src/menu/kvx_mqtt/kvx_mqtt_app.cpp")

def require(condition,message):
    assert condition,message

require('if (!BLEStateManager::initBLE("Kvx-BLESuite", ESP_PWR_LVL_P9))' in ble,"BLE Suite init guard")
require("uiRamLeaveHeavy(); // Restore regular KVX UI" in ble,"BLE Suite release")
require("const int rowH = 13;" in ble,"BLE Suite compact rows")
require("const int menuTop = 32;" in ble,"BLE Suite compact top")
require("deviceAddresses.size() >= 64" in ble,"BLE scanner bounded")
require(ble.count("size_t deviceCount = snapshot->count;") >= 2,"BLE selection size matches snapshot")
require("std::vector<String>().swap(deviceNames)" in ble,"BLE clear returns capacity")
require("BLEStateManager::isBLEActive()" in generic,"generic stop BLE ownership")
require("BLEDevice::deinit();" in generic,"generic stack cleanup")
require("wifiCompactNotice(" in wifi,"Wi-Fi status compact")
require("static int wifiScanWithCancel()" in wifi,"Wi-Fi asynchronous scan")
require("WiFi.scanNetworks(true)" in wifi,"Wi-Fi nonblocking scan start")
require("vTaskDelay(pdMS_TO_TICKS(30))" in wifi,"Wi-Fi scan yields")
require("if (nets == -3)" in wifi,"Wi-Fi scan Esc cancellation")
require('tft.setTextSize(kKvxMenuFont)' in ui,"shared KVX compact 1x")
require("uiRowH(1)" in display,"regular option density")
require("heap_caps_get_largest_free_block" in nexus,"Nexus contiguous heap guard")
require("sdOK=setupSdCard() && sdcardMounted;" in mqtt,"MQTT lazy SD init")
require('String typed=keyboard(editBuf,110,prompt,settingsIdx==1||settingsIdx==5);' in mqtt,"MQTT native editor with masked secrets")
require("bool revealPassword=false;" in mqtt,"MQTT settings hide secrets by default")
require("if(f.size()>8192){f.close();return false;}" in mqtt,"MQTT rejects oversized configuration")
require("cfg.port=(p>0&&p<65536)?(uint16_t)p:0;" in mqtt,"MQTT rejects invalid TLS ports")
require('wifiDisconnect();\n    }\n    vTaskDelete(NULL);' in wifi,"WiFi auto task closes failed sessions")
require('if (!wifiConnected && !WiFi.isConnected())' in wifi,"WiFi foreground session cleanup")
require('if (!NimBLEDevice::init(nameStr))' in ble,"BLE initialized status checked")
require('if (NimBLEDevice::isInitialized()) return true;' in ble,"BLE stale state checked")
require('const bool genericBleHeldRam = is_ble_inited;' in generic,"BLE generic lease ownership")
require('if (genericBleHeldRam) uiRamLeaveHeavy();' in generic,"BLE generic lease release guarded")
require('std::shared_ptr<DeviceSnapshot> ScannerData::getSnapshot()' in ble,"BLE owned scan snapshots")
require('auto snapshot = scannerData.getSnapshot();' in ble,"BLE snapshot callers own references")
require('std::shared_ptr<DeviceSnapshot> snapshotCache;' in content("src/menu/ble/BLE_Suite.h"),"BLE cache has owned lifetime")
require('bool connected=wifiConnectMenu();' in mqtt,"MQTT user can reconnect KVX WiFi")
require('void persistClockNow();' in content("src/root/app/utils.h"),"Clock immediate save exported")
require('void persistClockNow()' in content("src/root/app/utils.cpp"),"Clock immediate NVS save")
require('persistClockNow(); // Save manually adjusted time' in content("src/root/ui/settings.cpp"),"Manual clock write")
require('persistClockNow(); // NTP time should survive' in content("src/root/app/utils.cpp"),"NTP clock write")



# This quality gate verifies structure, not the physical radio or network.
print("PASS: BLE scan lifecycle, memory bounds, compact menus, Wi-Fi cancel, MQTT/Nexus integration")
print("NOTE: no physical RF, TLS or Wi-Fi association tested here")
