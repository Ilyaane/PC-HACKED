// Native Nexus Neon text chat for KVXputer / Cardputer ADV.
// Adapted from the user's Nexus Neon text-only protocol (HTTP endpoints,
// authorization, recipient roles and client_id deduplication).
// No firmware switch, no mic, no Morse, no MQTT, no token in NVS.
#include "nexus_neon_app.h"
#include "nexus_neon_secrets.h"
#include "ca_roots.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/app/utils.h"
#include <globals.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <time.h>

namespace {
constexpr int kMaxMessages = 24;
constexpr size_t kMaxText = 220;
constexpr unsigned long kPollMs = 12000;
constexpr uint32_t kClockThreshold = 1700000000UL;
constexpr uint16_t kNeonCyan = 0x07FF;
constexpr uint16_t kNeonPurple = 0xB09F;
constexpr uint16_t kNeonGreen = 0x07E0;
constexpr uint16_t kDark = 0x080F;
constexpr uint16_t kMuted = 0xA514;
constexpr int kWorkerStack = 12288;

enum class View : uint8_t { HOME, MESSAGES, DETAIL, CONFIRM, STATUS, CONTACT };
enum class Op : uint8_t { FETCH, SEND, SESSION };
struct Message {
    uint32_t id = 0;
    String author;
    String body;
    bool mine = false;
};
struct NetRequest {
    uint32_t generation = 0;
    Op op = Op::FETCH;
    String recipient;
    String payload;
    uint32_t after = 0;
};
struct NetReply {
    uint32_t generation = 0;
    Op op = Op::FETCH;
    int code = -1;
    String payload;
};

QueueHandle_t sReplyQueue = nullptr;
bool sBusy = false;
uint32_t sGeneration = 0;
Message sItems[kMaxMessages];
int sCount = 0, sSelected = 0;
uint32_t sCursor = 0;
View sView = View::HOME;
int sHome = 0;
String sRecipient = "bob";
String sDraft;
String sClientId;
String sStatus = "Pret";
bool sDirty = true;
unsigned long sPollAt = 0;

bool isConfigured() {
    return String(NEXUS_NEON_SERVER_URL).startsWith("https://") &&
           strlen(NEXUS_NEON_SHARED_TOKEN) >= 10;
}

void addHistory(uint32_t id, const String &author, const String &text, bool mine) {
    if (text.length() == 0) return;
    if (id != 0) {
        for (int i = 0; i < sCount; ++i)
            if (sItems[i].id == id && sItems[i].mine == mine) return;
    }
    if (sCount == kMaxMessages) {
        for (int i = 1; i < kMaxMessages; ++i) sItems[i - 1] = sItems[i];
        sCount--;
    }
    sItems[sCount].id = id;
    sItems[sCount].author = author;
    sItems[sCount].body = text;
    sItems[sCount].mine = mine;
    sCount++;
    sSelected = sCount - 1;
    sDirty = true;
}

String ownRole(const String &to) { return to == "alice" ? "bob" : "alice"; }
String printable(String in) {
    in.replace("\r", " ");
    in.replace("\n", " ");
    in.replace("\t", " ");
    return in;
}
String cut(const String &in, int maxChars) {
    return in.length() > (unsigned)maxChars ? in.substring(0, maxChars - 1) + "~" : in;
}

void doHttp(void *raw) {
    NetRequest *req = static_cast<NetRequest *>(raw);
    NetReply *rep = new (std::nothrow) NetReply();
    if (!rep) { delete req; vTaskDelete(nullptr); return; }
    rep->generation = req->generation;
    rep->op = req->op;
    if (WiFi.status() != WL_CONNECTED) {
        rep->code = -2;
    } else if (!isConfigured()) {
        rep->code = -3;
    } else {
        if (time(nullptr) < kClockThreshold) {
            configTime(0, 0, "pool.ntp.org", "time.google.com");
            for (int i = 0; i < 36 && time(nullptr) < kClockThreshold; ++i)
                vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (time(nullptr) < kClockThreshold) {
            rep->code = -4;  // No unverified TLS date.
        } else {
            WiFiClientSecure client;
            HTTPClient http;
            client.setCACert(NEXUS_TRUSTED_ROOTS);
            client.setTimeout(5500);
            http.setConnectTimeout(4500);
            http.setTimeout(5500);
            String path = req->op == Op::FETCH
                              ? "/messages?after=" + String(req->after)
                              : req->op == Op::SEND ? "/messages" : "/session";
            if (http.begin(client, String(NEXUS_NEON_SERVER_URL) + path)) {
                http.addHeader("Authorization", String("Bearer ") + NEXUS_NEON_SHARED_TOKEN);
                http.addHeader("X-Nexus-User", ownRole(req->recipient));
                if (req->op == Op::SEND) {
                    http.addHeader("Content-Type", "application/json");
                    rep->code = http.POST(req->payload);
                } else {
                    rep->code = http.GET();
                }
                // Limit parsing memory, and never trust server-controlled large bodies.
                if (rep->code >= 200 && rep->code < 300 &&
                    http.getSize() <= 16384) {
                    rep->payload = http.getString();
                    if (rep->payload.length() > 16384) {
                        rep->payload = "";
                        rep->code = -6;
                    }
                }
                http.end();
            } else {
                rep->code = -5;
            }
        }
    }
    delete req;
    if (!sReplyQueue || xQueueSend(sReplyQueue, &rep, 0) != pdTRUE) delete rep;
    vTaskDelete(nullptr);
}

bool beginOperation(Op op, const String &payload = "") {
    if (sBusy) return false;
    if (!sReplyQueue) {
        sReplyQueue = xQueueCreate(3, sizeof(NetReply *));
        if (!sReplyQueue) { sStatus = "Memoire insuffisante (file)"; return false; }
    }
    if (ESP.getFreeHeap() < 43000) {
        sStatus = "RAM insuffisante, action refusee";
        sDirty = true;
        return false;
    }
    NetRequest *req = new (std::nothrow) NetRequest();
    if (!req) { sStatus = "Memoire indisponible"; sDirty = true; return false; }
    req->generation = sGeneration;
    req->op = op;
    req->recipient = sRecipient;
    req->payload = payload;
    req->after = sCursor;
    sBusy = true;
    if (xTaskCreatePinnedToCore(doHttp, "nexus_kvx_tls", kWorkerStack,
                                req, 1, nullptr, 0) != pdPASS) {
        delete req;
        sBusy = false;
        sStatus = "Impossible de lancer le reseau";
        sDirty = true;
        return false;
    }
    sStatus = op == Op::SEND ? "Envoi HTTPS..." : "Synchronisation HTTPS...";
    sDirty = true;
    return true;
}

void acceptReply() {
    if (!sReplyQueue) return;
    NetReply *rep = nullptr;
    while (xQueueReceive(sReplyQueue, &rep, 0) == pdTRUE) {
        sBusy = false;
        if (!rep) continue;
        if (rep->generation != sGeneration) {
            delete rep;
            continue;
        }
        if (rep->code < 200 || rep->code >= 300) {
            sStatus = rep->code == -2 ? "Connecte d'abord le Wi-Fi" :
                      rep->code == -3 ? "Jeton compile absent" :
                      rep->code == -4 ? "Heure TLS indisponible" :
                      rep->code == -6 ? "Reponse trop volumineuse" :
                      "Reseau / serveur: " + String(rep->code);
        } else if (rep->op == Op::FETCH) {
            JsonDocument doc;
            if (deserializeJson(doc, rep->payload)) {
                sStatus = "Reponse JSON invalide";
            } else {
                JsonArray incoming = doc["messages"].as<JsonArray>();
                if (incoming.isNull()) {
                    sStatus = "Format des messages inconnu";
                } else {
                    int added = 0;
                    for (JsonObject item : incoming) {
                        uint32_t id = item["id"] | 0u;
                        if (id == 0 || id <= sCursor) continue;
                        String kind = item["kind"] | "";
                        // This version is intentionally text-only.
                        if (kind == "text") {
                            String author = item["sender"] | "?";
                            String body = printable(item["text"].as<String>());
                            addHistory(id, author, body, false);
                            added++;
                        }
                        // Advance past all kinds; audio isn't supported here.
                        sCursor = id;
                    }
                    sStatus = added ? String(added) + " nouveau(x) message(s)" : "Messages a jour";
                }
            }
            sPollAt = millis() + kPollMs;
        } else if (rep->op == Op::SEND) {
            // Preserve the draft and idempotency key on uncertain errors.
            addHistory(0, ownRole(sRecipient), printable(sDraft), true);
            sDraft = "";
            sClientId = "";
            sStatus = "Message envoye";
            sView = View::MESSAGES;
            sPollAt = millis() + 1000;
        } else if (rep->op == Op::SESSION) {
            sStatus = "Serveur disponible";
        }
        sDirty = true;
        delete rep;
    }
}

void loadContact() {
    Preferences prefs;
    if (!prefs.begin("nexusneon", true)) return;
    const String saved = prefs.getString("contact", "bob");
    prefs.end();
    sRecipient = saved == "alice" ? "alice" : "bob";
}

void saveContact() {
    Preferences prefs;
    if (!prefs.begin("nexusneon", false)) {
        sStatus = "NVS indisponible : contact non memorise";
        return;
    }
    size_t n = prefs.putString("contact", sRecipient);
    prefs.end();
    sStatus = n ? "Contact memorise" : "Echec sauvegarde contact";
    // No token, server, or message data written to NVS.
}

void paintFrame(const char *title) {
    tft.fillScreen(kDark);
    tft.drawRoundRect(2, 2, tftWidth - 4, tftHeight - 4, 7, kNeonPurple);
    tft.fillRoundRect(4, 4, tftWidth - 8, 20, 5, 0x181B);
    tft.drawLine(7, 24, tftWidth - 8, 24, kNeonCyan);
    tft.setTextSize(1);
    tft.setTextColor(kNeonCyan, 0x181B);
    tft.drawString(cut(String(title), 21), 9, 11, 1);
    String net = WiFi.status() == WL_CONNECTED ? "W" : "-";
    tft.setTextColor(kNeonGreen, 0x181B);
    tft.drawRightString(net, tftWidth - 10, 11, 1);
    tft.fillRoundRect(5, tftHeight - 22, tftWidth - 10, 17, 4, 0x101C);
    tft.setTextColor(kNeonPurple, 0x101C);
    tft.drawString(cut(sStatus, 34), 9, tftHeight - 18, 1);
}

void drawHome() {
    paintFrame("NEXUS  NEON / TEXTE");
    const char *labels[4] = {"MESSAGES", "ECRIRE", "WI-FI", "CONTACT"};
    const uint16_t colors[4] = {kNeonCyan, kNeonPurple, kNeonGreen, kNeonCyan};
    const int x0 = 9, y0 = 30, w = (tftWidth - 26) / 2, h = 35;
    for (int i = 0; i < 4; ++i) {
        const int col = i % 2, row = i / 2;
        const int x = x0 + col * (w + 8);
        const int y = y0 + row * 40;
        uint16_t fill = i == sHome ? 0x3039 : 0x181D;
        tft.fillRoundRect(x, y, w, h, 6, fill);
        tft.drawRoundRect(x, y, w, h, 6, i == sHome ? kNeonGreen : colors[i]);
        tft.setTextColor(i == sHome ? kNeonGreen : colors[i], fill);
        tft.setTextSize(1);
        tft.drawCentreString(labels[i], x + w/2, y + 12, 1);
    }
}

void drawMessages() {
    paintFrame(sRecipient == "bob" ? "ALICE > BOB" : "BOB > ALICE");
    tft.setTextSize(1);
    if (sCount == 0) {
        tft.setTextColor(kMuted, kDark);
        tft.drawCentreString("Aucun message texte", tftWidth / 2, 53, 1);
        tft.drawCentreString("Droite : composer", tftWidth / 2, 67, 1);
        return;
    }
    const int visibleRows = max(1, (tftHeight - 57) / 16);
    int first = max(0, sSelected - visibleRows + 1);
    for (int slot = 0; slot < visibleRows && first + slot < sCount; ++slot) {
        const int i = first + slot, y = 30 + slot * 16;
        uint16_t fill = i == sSelected ? 0x2030 : kDark;
        tft.fillRoundRect(8, y, tftWidth - 16, 15, 3, fill);
        const uint16_t c = sItems[i].mine ? kNeonPurple : kNeonCyan;
        tft.setTextColor(c, fill);
        tft.drawString(sItems[i].mine ? ">" : "<", 11, y + 4, 1);
        tft.drawString(cut(sItems[i].body, 30), 25, y + 4, 1);
    }
}

void drawDetail() {
    paintFrame("DETAIL DU MESSAGE");
    if (sCount == 0) return;
    const Message &m = sItems[sSelected];
    tft.setTextSize(1);
    tft.setTextColor(m.mine ? kNeonPurple : kNeonCyan, kDark);
    tft.drawString(cut(m.mine ? "Envoye" : "Recu de " + m.author, 35), 9, 31, 1);
    String txt = printable(m.body);
    const int width = max(15, (tftWidth - 22) / 6);
    const int lines = min(6, (int)((txt.length() + width - 1) / width));
    tft.setTextColor(0xFFFF, kDark);
    for (int i = 0; i < lines; ++i) {
        tft.drawString(txt.substring(i * width, (i + 1) * width), 9, 47 + i * 11, 1);
    }
}

void drawConfirm() {
    paintFrame("CONFIRMER ENVOI");
    tft.setTextSize(1);
    tft.setTextColor(kNeonGreen, kDark);
    tft.drawString("Pour : " + sRecipient, 10, 33, 1);
    tft.setTextColor(0xFFFF, kDark);
    String txt = printable(sDraft);
    for (int i = 0; i < 4 && i * 35 < (int)txt.length(); ++i)
        tft.drawString(txt.substring(i*35, (i+1)*35), 10, 49 + i*12, 1);
    tft.setTextColor(kNeonCyan, kDark);
    tft.drawString("Entree: envoyer   Esc: retour", 10, tftHeight - 37, 1);
}

void drawContact() {
    paintFrame("CHOIX DU CONTACT");
    tft.setTextColor(kNeonCyan, kDark);
    tft.setTextSize(1);
    tft.drawCentreString("Destinataire", tftWidth/2, 41, 1);
    tft.setTextSize(2);
    tft.drawCentreString(sRecipient == "bob" ? "BOB" : "ALICE", tftWidth/2, 61, 1);
    tft.setTextSize(1);
    tft.drawCentreString("Gauche / Droite : changer", tftWidth/2, 89, 1);
    tft.drawCentreString("Entree : sauvegarder", tftWidth/2, 102, 1);
}

void paint() {
    switch (sView) {
    case View::HOME: drawHome(); break;
    case View::MESSAGES: drawMessages(); break;
    case View::DETAIL: drawDetail(); break;
    case View::CONFIRM: drawConfirm(); break;
    case View::CONTACT: drawContact(); break;
    case View::STATUS: paintFrame("ETAT NEXUS"); break;
    }
    sDirty = false;
}

void newDraft() {
    String typed = keyboard(sDraft, (int)kMaxText, "Message Nexus (Esc annule)");
    // KVXputer returns the escape byte on cancellation.
    if (typed == "\x1B") {
        sStatus = "Brouillon conserve";
        sDirty = true;
        return;
    }
    typed.trim();
    if (typed.isEmpty()) { sStatus = "Message vide : annule"; sDirty = true; return; }
    if (typed.length() > kMaxText) typed = typed.substring(0, kMaxText);
    sDraft = typed;
    // Keep the idempotency key for retries, but refresh it for new content.
    sClientId = "";
    sView = View::CONFIRM;
    sDirty = true;
}

void sendDraft() {
    if (sDraft.isEmpty()) { sStatus = "Message vide"; sDirty = true; return; }
    if (WiFi.status() != WL_CONNECTED) {
        sStatus = "Wi-Fi absent : texte conserve";
        sDirty = true;
        return;
    }
    if (sClientId.isEmpty()) {
        sClientId = String("kvx-") + String((uint32_t)esp_random(), HEX) +
                    String((uint32_t)esp_random(), HEX);
    }
    JsonDocument data;
    data["to"] = sRecipient;
    data["text"] = sDraft;
    data["client_id"] = sClientId;
    String body;
    serializeJson(data, body);
    if (beginOperation(Op::SEND, body)) sView = View::STATUS;
}

bool pressed(volatile bool &flag) { return check(flag); }

void handleHomeSelect() {
    if (sHome == 0) {
        sView = View::MESSAGES;
        if (WiFi.status() == WL_CONNECTED) beginOperation(Op::FETCH);
    } else if (sHome == 1) {
        newDraft();
    } else if (sHome == 2) {
        if (WiFi.status() == WL_CONNECTED) {
            sStatus = "Wi-Fi deja connecte";
        } else {
            // Reuse KVXputer's own Wi-Fi UI and saved network manager.
            wifiConnectMenu();
            sStatus = WiFi.status() == WL_CONNECTED ? "Wi-Fi connecte" : "Wi-Fi non connecte";
        }
    } else {
        sView = View::CONTACT;
    }
    sDirty = true;
}

void handleKeys(bool &exitApp) {
    // Standard KVXputer directional flags: ; , . / without Fn.
    if (pressed(EscPress)) {
        if (sView == View::HOME) { exitApp = true; return; }
        if (sView == View::DETAIL) sView = View::MESSAGES;
        else sView = View::HOME;
        sDirty = true;
        return;
    }
    if (pressed(UpPress)) {
        if (sView == View::HOME) sHome = (sHome + 2) % 4;
        else if (sView == View::MESSAGES && sCount) sSelected = max(0, sSelected - 1);
        sDirty = true;
    }
    if (pressed(DownPress)) {
        if (sView == View::HOME) sHome = (sHome + 2) % 4;
        else if (sView == View::MESSAGES && sCount) sSelected = min(sCount - 1, sSelected + 1);
        sDirty = true;
    }
    if (pressed(PrevPress)) {
        if (sView == View::HOME) sHome = (sHome / 2) * 2;
        else if (sView == View::MESSAGES && !sBusy) beginOperation(Op::FETCH);
        else if (sView == View::CONTACT) sRecipient = sRecipient == "bob" ? "alice" : "bob";
        else if (sView == View::DETAIL && sCount) sSelected = max(0, sSelected - 1);
        sDirty = true;
    }
    if (pressed(NextPress)) {
        if (sView == View::HOME) sHome = (sHome / 2) * 2 + 1;
        else if (sView == View::MESSAGES && !sBusy) newDraft();
        else if (sView == View::CONTACT) sRecipient = sRecipient == "bob" ? "alice" : "bob";
        else if (sView == View::DETAIL && sCount) sSelected = min(sCount - 1, sSelected + 1);
        sDirty = true;
    }
    if (pressed(SelPress)) {
        if (sView == View::HOME) handleHomeSelect();
        else if (sView == View::MESSAGES && sCount) sView = View::DETAIL;
        else if (sView == View::CONFIRM) sendDraft();
        else if (sView == View::CONTACT) { saveContact(); sView = View::HOME; }
        else if (sView == View::STATUS && !sBusy) sView = View::HOME;
        sDirty = true;
    }
}
}  // namespace

void nexusNeonApp() {
#if !defined(HAS_KEYBOARD)
    displayInfo("Nexus Neon: clavier requis", true);
    return;
#else
    loadContact();
    sGeneration++;
    sCursor = 0; sCount = 0; sSelected = 0;
    sView = View::HOME; sHome = 0; sDraft = ""; sClientId = "";
    sStatus = isConfigured() ? "Entree: ouvrir, Esc: quitter" : "Compilation sans jeton";
    sDirty = true;
    sPollAt = millis() + kPollMs;
    // Do not reset sBusy: a previously launched HTTPS request may still be running.
    EscPress = false; SelPress = false; NextPress = false; PrevPress = false;
    UpPress = false; DownPress = false;
    bool exitApp = false;
    while (!exitApp && !returnToMenu && !forceHome) {
        checkReboot();
        acceptReply();
        if (sView == View::MESSAGES && !sBusy &&
            WiFi.status() == WL_CONNECTED && (int32_t)(millis() - sPollAt) >= 0) {
            beginOperation(Op::FETCH);
            sPollAt = millis() + kPollMs;
        }
        if (sDirty) paint();
        handleKeys(exitApp);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    // Let in-flight HTTPS finish; replies are consumed/dropped by generation on re-entry.
    tft.fillScreen(kvxConfig.bgColor);
#endif
}
