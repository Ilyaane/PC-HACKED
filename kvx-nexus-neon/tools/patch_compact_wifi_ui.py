#!/usr/bin/env python3
"""Compact shared Wi-Fi scan and connection feedback used by Nexus and KVX.

Preserves existing SSID scanning, credentials, radio manager and connection logic.
Adds Esc cancellation to avoid a non-interruptible ~10-second connect screen.
"""
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve()
path=root/"src/root/net/wifi_common.cpp"
s=path.read_text(encoding="utf-8")

def change(a,b):
    global s
    assert s.count(a)==1, f"Wifi source anchor mismatch {a[:95]!r} ({s.count(a)})"
    s=s.replace(a,b,1)

anchor="bool _wifiConnect(const String &ssid, int encryption) {"
helper="""// Compact 1x Wi-Fi status screen, shared by Nexus and KVXputer.
// The screen is cleared on a meaningful state transition, never on a timer.
static void wifiCompactNotice(const String &title, const String &detail) {
#if defined(HAS_SCREEN)
    tft.fillScreen(kvxConfig.bgColor);
    tft.setTextSize(1);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString(title, 8, 9, 1);
    tft.drawFastHLine(8, 22, tftWidth - 16, kvxConfig.secColor);
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawString(detail.substring(0, 36), 8, 39, 1);
    tft.drawString("ESC : annuler", 8, 118, 1);
#else
    Serial.println(title + ": " + detail);
#endif
}

"""
change(anchor,helper+anchor)

old="""    RAM_LOG("wifi pre-mode"); // Wi-Fi is already up from the menu scan by this point
    drawMainBorderWithTitle("WiFi Connect");
    padprintln("");
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    padprintln("Connecting to:");
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    padprint(ssid);
    tft.print(".");
    WiFi.mode(WIFI_MODE_STA);"""
new="""    RAM_LOG("wifi pre-mode"); // Wi-Fi is already up from the menu scan by this point
    wifiCompactNotice("WI-FI / Connexion", "Reseau : " + ssid);
    WiFi.mode(WIFI_MODE_STA);"""
change(old,new)

a=s.index("    int i = 1;\n    while (!WiFi.isConnected()) {",s.index("bool _connectToWifiNetwork("))
b=s.index("    return WiFi.isConnected();",a)
s=s[:a]+"""    int i = 1;
    while (!WiFi.isConnected()) {
        // The network handshake is bounded and the user can cancel it.
        if (check(EscPress)) {
            WiFi.disconnect(false, false);
            return false;
        }
        if (i > 20) {
            wifiCompactNotice("WI-FI", "Delai depasse : connexion echouee");
            break;
        }
#if defined(HAS_SCREEN)
        // Update only the progress row. No growing giant dots / full repaint.
        if (i == 1 || (i & 1) == 0) {
            tft.fillRect(7, 60, tftWidth - 14, 16, kvxConfig.bgColor);
            tft.setTextSize(1);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawString("Connexion : " + String(i / 2) + " s", 8, 64, 1);
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(500));
        ++i;
    }

"""+s[b:]

# Scan feedback must not open the oversized adaptive text dialog.
change('                displayTextLine("Scanning..");',
       '                wifiCompactNotice("WI-FI / Recherche", "Detection des reseaux...");')
change('    displayTextLine("Scanning Networks..");',
       '    wifiCompactNotice("WI-FI / Recherche", "Reseaux enregistres...");')
path.write_text(s,encoding="utf-8")
print("Compact Wi-Fi search/connect UI and cancellable association, radio/credentials preserved.")
