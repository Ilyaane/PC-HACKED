#!/usr/bin/env python3
"""Bound passive BLE discovery memory and prevent snapshot/vector index mismatch.

No changes to BLE transmissions, connection logic or any attack functionality.
"""
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve()
p=root/"src/menu/ble/BLE_Suite.cpp"
s=p.read_text(encoding="utf-8")
def sub(a,b,n=1):
    global s
    assert s.count(a)==n,(a[:100],s.count(a),n)
    s=s.replace(a,b,n)

# Limit unbounded vector growth on 8MB-flash / no-PSRAM Cardputer ADV.
a="""        if (!isDuplicate) {
            deviceNames.push_back(name);"""
b="""        if (!isDuplicate) {
            // Keep discovery bounded on the small internal heap of Cardputer ADV.
            // Existing entries remain available; extra advertisements are ignored.
            if (deviceAddresses.size() >= 64) {
                xSemaphoreGive(mutex);
                return;
            }
            deviceNames.push_back(name);"""
sub(a,b)

# Clear capacity instead of retaining all previously discovered strings in RAM.
a="""        deviceNames.clear();
        deviceAddresses.clear();
        deviceRssi.clear();
        deviceFastPair.clear();
        deviceHasHFP.clear();
        deviceTypes.clear();
        foundCount = 0;"""
b="""        std::vector<String>().swap(deviceNames);
        std::vector<String>().swap(deviceAddresses);
        std::vector<int>().swap(deviceRssi);
        std::vector<bool>().swap(deviceFastPair);
        std::vector<bool>().swap(deviceHasHFP);
        std::vector<uint8_t>().swap(deviceTypes);
        foundCount = 0;"""
sub(a,b)

# Do not pair live vector size with an older snapshot's vectors while indexing.
sub("size_t deviceCount = scannerData.deviceAddresses.size();",
    "size_t deviceCount = snapshot->count;",2)
p.write_text(s,encoding="utf-8")
print("BLE discovery capped at 64 devices; capacity released on clear; snapshot indexing consistent.")
