#!/usr/bin/env python3
"""Persist a manually set or NTP-synchronised software clock immediately.

Original KVX saved the clock only from a task that first slept 60 seconds.
A user who rebooted before that delay lost the new time. All other clock
behavior and periodic write throttling are unchanged.
"""
from pathlib import Path
import sys
root=Path(sys.argv[1]).resolve()
header=root/"src/root/app/utils.h"
h=header.read_text(encoding="utf-8")
a="""#if !defined(HAS_RTC)
void restorePersistedClock();
#endif"""
b="""#if !defined(HAS_RTC)
void restorePersistedClock();
void persistClockNow();
#endif"""
assert h.count(a)==1
h=h.replace(a,b,1)
header.write_text(h,encoding="utf-8")

path=root/"src/root/app/utils.cpp"
s=path.read_text(encoding="utf-8")
a="""    rtc.setTime(localTime);
    updateTimeStr(rtc.getTimeStruct());
    clock_set = true;
#endif"""
b="""    rtc.setTime(localTime);
    updateTimeStr(rtc.getTimeStruct());
    clock_set = true;
    persistClockNow(); // NTP time should survive an immediate user reboot.
#endif"""
assert s.count(a)==1
s=s.replace(a,b,1)
anchor="#define CLOCK_PERSIST_INTERVAL_MS 300000 // 5 min: bounds NVS wear, <=5 min drift after power loss"
assert s.count(anchor)==1
s=s.replace(anchor,anchor+"""

// Explicit user/NTP saves must not depend on the one-minute background task.
void persistClockNow() {
    if (!clock_set) return;
    uint32_t epoch = (uint32_t)rtc.getEpoch();
    if (epoch <= 1735689600UL) return;
    Preferences prefs;
    if (prefs.begin(CLOCK_PERSIST_NS, false)) {
        prefs.putULong(CLOCK_PERSIST_KEY, epoch);
        prefs.end();
    }
}
""",1)
path.write_text(s,encoding="utf-8")
settings=root/"src/root/ui/settings.cpp"
s=settings.read_text(encoding="utf-8")
a="""        clock_set = true;
    }
}

void runClockLoop(bool showMenuHint) {"""
b="""        clock_set = true;
#if !defined(HAS_RTC)
        persistClockNow(); // Save manually adjusted time before the user can reboot.
#endif
    }
}

void runClockLoop(bool showMenuHint) {"""
assert s.count(a)==1
s=s.replace(a,b,1)
settings.write_text(s,encoding="utf-8")
print("RTC-less clock now persists manual and NTP changes immediately.")
