#!/usr/bin/env python3
"""Noninvasive Wi-Fi reliability fixes on a disposable KVXputer checkout.

User-facing changes:
* safe FreeRTOS termination for an already-connected auto-connect task;
* no accidental reconnect to a different SSID after a successful auto-connect;
* do not disable the Wi-Fi interface before attempting to scan known networks;
* release completed scan buffers and show a clear failure on scan errors;
* add an explicit scan retry to the normal connection menu.
All original radio/privacy/network toolkit features remain untouched.
"""
from pathlib import Path
import sys

path=Path(sys.argv[1]) / "src/root/net/wifi_common.cpp"
text=path.read_text(encoding="utf-8")

def replace_one(a,b):
    global text
    if b in text:
        return
    assert text.count(a)==1,(path, a[:100],text.count(a))
    text=text.replace(a,b,1)

replace_one(
    "void wifiConnectTask(void *pvParameters) {\n    if (WiFi.isConnected()) return;",
    "void wifiConnectTask(void *pvParameters) {\n    if (WiFi.isConnected()) { vTaskDelete(NULL); return; }"
)
replace_one(
    "    for (int i = 0; i < nets; i++) {\n        ssid = WiFi.SSID(i);",
    "    for (int i = 0; i < nets; i++) {\n        if (WiFi.isConnected()) break;\n        ssid = WiFi.SSID(i);"
)
replace_one(
    "    WiFi.disconnect(true, true);\n    vTaskDelay(10 / portTICK_PERIOD_MS);\n    nets = WiFi.scanNetworks();",
    "    // Preserve the STA driver and saved credentials while scanning.\n"
    "    WiFi.disconnect(false, false);\n"
    "    WiFi.mode(WIFI_MODE_STA);\n"
    "    vTaskDelay(50 / portTICK_PERIOD_MS);\n"
    "    nets = WiFi.scanNetworks();\n"
    "    if (nets < 0) {\n"
    "        displayError(\"WiFi scan failed; retry\", true);\n"
    "        wifiDisconnect();\n"
    "        return false;\n"
    "    }"
)
replace_one(
    "    return result;\n}\n\nvoid updateTimezoneTask",
    "    WiFi.scanDelete(); // Release scan results in success and failure paths.\n"
    "    if (!result) wifiDisconnect();\n"
    "    return result;\n}\n\nvoid updateTimezoneTask"
)
replace_one(
    "                nets = WiFi.scanNetworks();\n\n                String selSsid = \"\";",
    "                nets = WiFi.scanNetworks();\n"
    "                if (nets < 0) {\n"
    "                    WiFi.scanDelete();\n"
    "                    displayError(\"WiFi scan failed; retry\", true);\n"
    "                    wifiDisconnect();\n"
    "                    return false;\n"
    "                }\n\n                String selSsid = \"\";"
)
replace_one(
    "                options.push_back({\"Hidden SSID\", [&selHidden]() { selHidden = true; }});",
    "                options.push_back({\"Rescan\", [&refresh_scan]() { refresh_scan = true; }});\n"
    "                options.push_back({\"Hidden SSID\", [&selHidden]() { selHidden = true; }});"
)
replace_one(
    "                } else if (check(EscPress)) {\n                    refresh_scan = true;\n                } else {\n                    refresh_scan = false;\n                }",
    "                } else if (refresh_scan) {\n"
    "                    // Explicit Rescan menu choice: keep the requested retry.\n"
    "                } else if (check(EscPress)) {\n"
    "                    refresh_scan = true;\n"
    "                } else {\n"
    "                    refresh_scan = false;\n"
    "                }"
)

path.write_text(text,encoding="utf-8")
print("Applied verified Wi-Fi task, known-network scan and rescan corrections.")
