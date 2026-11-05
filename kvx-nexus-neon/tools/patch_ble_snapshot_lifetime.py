#!/usr/bin/env python3
"""Make passive BLE scan snapshots owned handles, not dangling raw pointers.

Previously ScannerData::addDevice() and clear() deleted snapshotCache while a
menu could still read its raw pointer. shared_ptr keeps each UI snapshot alive.
BLE packets and active functions are not changed.
"""
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve()
header=root/"src/menu/ble/BLE_Suite.h"
h=header.read_text(encoding="utf-8")
def hchange(old,new):
    global h
    assert h.count(old)==1,("header anchor",old,h.count(old))
    h=h.replace(old,new,1)
hchange("#include <functional>\n#include <vector>","#include <functional>\n#include <memory>\n#include <vector>")
hchange("    DeviceSnapshot *snapshotCache;","    std::shared_ptr<DeviceSnapshot> snapshotCache;")
hchange("    DeviceSnapshot *getSnapshot();","    std::shared_ptr<DeviceSnapshot> getSnapshot();")
header.write_text(h,encoding="utf-8")

p=root/"src/menu/ble/BLE_Suite.cpp"
s=p.read_text(encoding="utf-8")
def change(old,new,count=1):
    global s
    assert s.count(old)==count,("cpp anchor",old[:100],s.count(old))
    s=s.replace(old,new,count)
change("    snapshotCache = nullptr;","    snapshotCache.reset();")
change("    if (snapshotCache) delete snapshotCache;","    snapshotCache.reset();")
# Both invalidation sites release only the cache's reference, not any in-flight UI handles.
change("""            if (snapshotCache) {
                delete snapshotCache;
                snapshotCache = nullptr;
            }""", """            snapshotCache.reset();""",1)
change("""        if (snapshotCache) {
            delete snapshotCache;
            snapshotCache = nullptr;
        }""", """        snapshotCache.reset();""",1)
start=s.index("DeviceSnapshot *ScannerData::getSnapshot() {")
end=s.index("bool ScannerData::getDeviceInfo(",start)
replacement="""std::shared_ptr<DeviceSnapshot> ScannerData::getSnapshot() {
    std::shared_ptr<DeviceSnapshot> result;
    // No unlocked read of snapshotCache: the BLE scan callback can invalidate it.
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        const uint32_t now = millis();
        if (snapshotCache && (now - cacheTimestamp) < 1000U) {
            result = snapshotCache;
        } else {
            auto fresh = std::make_shared<DeviceSnapshot>();
            fresh->version = dataVersion;
            fresh->count = deviceAddresses.size();
            fresh->timestamp = now;
            fresh->names = deviceNames;
            fresh->addresses = deviceAddresses;
            fresh->rssi = deviceRssi;
            fresh->fastPair = deviceFastPair;
            fresh->hfp = deviceHasHFP;
            fresh->types = deviceTypes;
            snapshotCache = fresh;
            cacheTimestamp = now;
            result = fresh;
        }
        xSemaphoreGive(mutex);
    }
    return result; // Empty handle if the short mutex wait timed out.
}

"""
s=s[:start]+replacement+s[end:]
change("DeviceSnapshot *snapshot = scannerData.getSnapshot();",
       "auto snapshot = scannerData.getSnapshot();",2)
p.write_text(s,encoding="utf-8")
print("Passive BLE: snapshot lifetime pinned with shared_ptr under mutex; two callers migrated.")
