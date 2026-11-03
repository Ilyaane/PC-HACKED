#!/usr/bin/env python3
"""Pin KVXputer's FastLED build dependency in a disposable checkout.

The upstream constraint ^3.10.3 unexpectedly upgrades to 3.10.9, which
introduces an ESP32-S3 consoleWrite link relocation failure under LTO.
Keep the upstream-approved minimum 3.10.3 but prevent auto-upgrading.
"""
from pathlib import Path
import sys
path=Path(sys.argv[1])
s=path.read_text(encoding="utf-8")
old="fastled/FastLED @^3.10.3"
new="fastled/FastLED @3.10.3"
if new not in s:
    assert s.count(old)==1, "Unexpected upstream FastLED library declaration"
    path.write_text(s.replace(old,new),encoding="utf-8")
print("FastLED pinned to exact 3.10.3 for reproducibility.")
