#!/usr/bin/env python3
"""Close abandoned WiFi memory leases on failed auto-connect.

Operates only on the pinned temporary KVX source. Preserves network credentials,
search results and successful connections. Does not change transmit features.
"""
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()/"src/root/net/wifi_common.cpp"
s=p.read_text(encoding="utf-8")
a="""    WiFi.scanDelete();

    vTaskDelete(NULL);
    return;
}

String checkMAC()"""
b="""    WiFi.scanDelete();

    // Failed background association still held the UI heavy-RAM lease.
    // Tear it down only when this task did not establish a connection.
    if (!WiFi.isConnected()) {
        wifiDisconnect();
    }
    vTaskDelete(NULL);
    return;
}

String checkMAC()"""
assert s.count(a)==1, f"wifi background task footer changed ({s.count(a)})"
s=s.replace(a,b,1)
a="""    if (returnToMenu) {
        wifiDisconnect(); // Forced turning off the wifi module if exiting back to the menu
        return false;
    }
    return wifiConnected;
}"""
b="""    if (returnToMenu) {
        wifiDisconnect(); // Forced turning off the wifi module if exiting back to the menu
        return false;
    }
    // A cancelled/failed interactive scan must not retain the memory lease.
    if (!wifiConnected && !WiFi.isConnected()) {
        wifiDisconnect();
    }
    return wifiConnected;
}"""
assert s.count(a)==1, f"wifi interactive exit changed ({s.count(a)})"
s=s.replace(a,b,1)
p.write_text(s,encoding="utf-8")
print("WiFi: failed foreground and background sessions now release the RAM lease.")
