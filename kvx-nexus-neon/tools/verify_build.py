#!/usr/bin/env python3
from pathlib import Path
import struct
import hashlib
import sys

root = Path(sys.argv[1])
fw = root / ".pio/build/m5stack-cardputer/firmware.bin"
assert fw.is_file(), "No compiled KVXputer firmware.bin"
data = fw.read_bytes()
assert len(data) > 250000, "Unexpectedly small firmware"
assert data[0] == 0xE9, "Invalid ESP32 application image magic"
app_slots = []
with (root / "tools/build/partitions/custom_8Mb.csv").open() as file:
    for raw in file:
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in raw.split(",")]
        if len(parts) > 4 and parts[1] == "app":
            size = int(parts[4], 0)
            app_slots.append((parts[0], size))
assert app_slots, "No app partition found in KVXputer partition table"
limit = min(size for name,size in app_slots)
assert len(data) <= limit, f"Image {len(data)} > app partition {limit}"
print(f"ESP32 application image header valid; size {len(data)} bytes; app partition {limit} bytes")
print("SHA-256 app:", hashlib.sha256(data).hexdigest())
print("REMINDER: no physical tests have been performed")
