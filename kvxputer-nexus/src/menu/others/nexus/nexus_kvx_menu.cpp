/*
 * NEXUS/KVXputer integration phase 1, AGPL-3.0-or-later.
 * Read-only NVS settings + bounded offline text journal. No duplicate Morse.
 * No network messaging in this phase: no untested blocking HTTPS calls.
 */
#include "nexus_kvx_menu.h"
#include "root/ui/display.h"
#include "root/net/wifi_common.h"
#include <globals.h>
#include <Preferences.h>
#include <SD.h>
#include <WiFi.h>
#include <vector>

namespace {
struct NexusSettings {
    String server;
    String peer;
    bool configured = false;
};

NexusSettings loadNexusSettings() {
    NexusSettings result;
    Preferences prefs;
    if (!prefs.begin("nexusadv", true)) return result;
    result.server = prefs.getString("server", "");
    result.peer = prefs.getString("peer", "bob");
    const String token = prefs.getString("token", "");
    result.configured = result.server.startsWith("https://") && token.length() >= 10;
    prefs.end();
    return result;
}

void showNexusStatus() {
    const NexusSettings settings = loadNexusSettings();
    String line = "NEXUS / KVX\n";
    line += "Wi-Fi: ";
    line += WiFi.status() == WL_CONNECTED ? "connecte" : "hors ligne";
    line += "\nConfiguration: ";
    line += settings.configured ? "presente" : "a completer";
    line += "\nCorrespondant: " + settings.peer;
    line += "\nMicroSD: ";
    line += sdcardMounted ? "disponible" : "absente";
    displayInfo(line, true);
}

void showOfflineInbox() {
    if (!sdcardMounted) {
        displayInfo("NEXUS: microSD non montee", true);
        return;
    }
    File f = SD.open("/nexus_history.txt", FILE_READ);
    if (!f) {
        displayInfo("NEXUS: aucun historique local", true);
        return;
    }
    // Only inspect a small tail of a potentially very large journal.
    const size_t bytes = f.size();
    if (bytes > 4096) {
        f.seek(bytes - 4096);
        f.readStringUntil('\n');  // Discard partial first line.
    }
    String recent[12];
    size_t count = 0;
    while (f.available()) {
        String record = f.readStringUntil('\n');
        if (record.length() > 450) record = record.substring(0, 450);
        const int first = record.indexOf('|');
        const int second = first >= 0 ? record.indexOf('|', first+1) : -1;
        const int third = second >= 0 ? record.indexOf('|', second+1) : -1;
        if (third < 0) continue;
        const String sender = record.substring(first+1, second);
        const String kind = record.substring(second+1, third);
        String body = record.substring(third+1);
        if (kind == "voice") body = "[Vocal archive]";
        if (body.length() > 65) body = body.substring(0, 62) + "...";
        const String entry = sender + ": " + body;
        if (count == 12) {
            for (size_t i=1; i<12; i++) recent[i-1] = recent[i];
            --count;
        }
        recent[count++] = entry;
    }
    f.close();
    if (!count) {
        displayInfo("NEXUS: aucun message en cache", true);
        return;
    }
    for (size_t i=0; i<count; i++) {
        displayInfo("NEXUS " + String(i+1) + "/" + String(count) + "\n" + recent[i], true);
        if (forceHome || returnToMenu) return;
    }
}
} // namespace

void nexusKvxMenu() {
    std::vector<Option> nexusItems = {
        {"Etat NEXUS", showNexusStatus},
        {"Recus (microSD)", showOfflineInbox},
        {"Wi-Fi KVXputer", []() { wifiConnectMenu(WIFI_STA); }},
        {"Retour", []() {}}
    };
    loopOptions(nexusItems, MENU_TYPE_SUBMENU, "NEXUS");
}
