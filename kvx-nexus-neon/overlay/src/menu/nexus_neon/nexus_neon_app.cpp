// Native Nexus Neon text chat for KVXputer / Cardputer ADV.
// Adapted from the user's Nexus Neon text-only protocol (HTTP endpoints,
// authorization, recipient roles and client_id deduplication).
// Nexus V10: no firmware switch, no mic, no Morse, no token in NVS.
#include "nexus_neon_app.h"
#include "nexus_neon_secrets.h"
#include "ca_roots.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/hal/radio_mem.h"
#include "root/app/utils.h"
#include <globals.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <nvs.h>
#include <atomic>
#include <new>
#include <time.h>

namespace {
constexpr int kMaxMessages = 24;
constexpr size_t kMaxText = 220;
constexpr unsigned long kPollMs = 12000; // background network only, no full-screen timer
constexpr uint32_t kClockThreshold = 1700000000UL;
constexpr uint16_t kNeonCyan = 0x07FF;
constexpr uint16_t kNeonPurple = 0xB09F;
constexpr uint16_t kNeonGreen = 0x07E0;
constexpr uint16_t kDark = 0x080F;
constexpr uint16_t kMuted = 0xA514;
constexpr int kWorkerStack = 12288;

enum class View : uint8_t { HOME, MESSAGES, DETAIL, CONFIRM, STATUS, CONTACT, WIFI_STATUS };
enum class Op : uint8_t { FETCH, SEND, SESSION };
struct Message {
    uint32_t id = 0;
    String author;
    String body;
    bool mine = false;
};
struct NetReply;
struct NetRequest {
    uint32_t generation = 0;
    Op op = Op::FETCH;
    String recipient;
    String payload;
    uint32_t after = 0;
    NetReply *response = nullptr; // Preallocated on UI core; worker cannot deadlock on OOM.
};
struct NetReply {
    uint32_t generation = 0;
    Op op = Op::FETCH;
    int code = -1;
    int tlsError = 0;
    String payload;
};

QueueHandle_t sReplyQueue = nullptr;
std::atomic<bool> sBusy{false};
uint32_t sGeneration = 0;
Message sItems[kMaxMessages];
int sCount = 0, sSelected = 0;
uint32_t sCursor = 0;
View sView = View::HOME;
int sHome = 0;
String sRecipient = "bob";
String sActiveRecipient = "bob";  // Role whose inbox and cursor are currently displayed.
String sDraft;
String sClientId;
// Keep unsent drafts and retry IDs separate for Alice and Bob.
String sDraftAlice, sDraftBob, sIdAlice, sIdBob;
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
    NetReply *rep = req->response;
    // Reply is reserved before the task launches, avoiding a permanent
    // "busy" state when the network core has no heap left to allocate it.
    rep->generation = req->generation;
    rep->op = req->op;
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    Serial.printf("[Nexus HTTPS] debut libre=%u bloc=%u\n",
                  (unsigned)heap_caps_get_free_size(internal),
                  (unsigned)heap_caps_get_largest_free_block(internal));
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
            client.setHandshakeTimeout(6);
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
                if (rep->code >= 200 && rep->code < 300) {
                    if (http.getSize() > 16384) {
                        // Explicit size guard: do not report a large body as HTTP 200.
                        rep->code = -6;
                    } else {
                        rep->payload = http.getString();
                        if (rep->payload.length() > 16384) {
                            rep->payload = "";
                            rep->code = -6;
                        }
                    }
                }
                if (rep->code < 0) {
                    char tlsMessage[96] = {};
                    rep->tlsError = client.lastError(tlsMessage, sizeof(tlsMessage));
                }
                http.end();
            } else {
                rep->code = -5;
            }
        }
    }
    Serial.printf("[Nexus HTTPS] fin HTTP=%d TLS=%d libre=%u bloc=%u pile=%u\n",
                  rep->code, rep->tlsError, (unsigned)heap_caps_get_free_size(internal),
                  (unsigned)heap_caps_get_largest_free_block(internal),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    delete req;
    if (!sReplyQueue || xQueueSend(sReplyQueue, &rep, pdMS_TO_TICKS(20)) != pdTRUE) {
        delete rep;
        sBusy.store(false); // Never strand the module waiting for a missing reply.
    }
    vTaskDelete(nullptr);
}

bool beginOperation(Op op, const String &payload = "") {
    if (sBusy) return false;
    tftReleaseFrameCanvas();
    if (!sReplyQueue) {
        sReplyQueue = xQueueCreate(3, sizeof(NetReply *));
        if (!sReplyQueue) { sStatus = "Memoire insuffisante (file)"; return false; }
    }
    // A 43 KB total-free-heap threshold rejected perfectly usable KVX builds.
    // What matters for the worker is a large contiguous INTERNAL block.
    // Reserve the reply first, so a failed allocation never leaves sBusy stuck.
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (largest < (size_t)kWorkerStack + 2048U) {
        if (op != Op::FETCH) {
            sStatus = "RAM contigue faible (" + String(largest / 1024U) + " Ko)";
            sDirty = true;
        }
        return false;
    }
    NetRequest *req = new (std::nothrow) NetRequest();
    if (!req) {
        if (op != Op::FETCH) { sStatus = "Memoire de requete indisponible"; sDirty = true; }
        return false;
    }
    req->response = new (std::nothrow) NetReply();
    if (!req->response) {
        delete req;
        if (op != Op::FETCH) { sStatus = "Memoire de reponse indisponible"; sDirty = true; }
        return false;
    }
    req->generation = sGeneration;
    req->op = op;
    req->recipient = sRecipient;
    req->payload = payload;
    req->after = sCursor;
    sBusy = true;
    if (xTaskCreatePinnedToCore(doHttp, "nexus_kvx_tls", kWorkerStack,
                                req, 1, nullptr, 0) != pdPASS) {
        delete req->response;
        delete req;
        sBusy = false;
        sStatus = "Impossible de lancer TLS (" + String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024U) + " Ko bloc libre)";
        sDirty = true;
        return false;
    }
    if(op != Op::FETCH) {
        sStatus = op == Op::SEND ? "Envoi HTTPS..." : "Verification du serveur...";
        sDirty = true;
    }
    return true;
}

void acceptReply() {
    if (!sReplyQueue) return;
    NetReply *rep = nullptr;
    while (xQueueReceive(sReplyQueue, &rep, 0) == pdTRUE) {
        sBusy = false;
        if (!rep) continue;
        const int previousCount = sCount;
        const String previousStatus = sStatus;
        if (rep->generation != sGeneration) {
            delete rep;
            continue;
        }
        if (rep->code < 200 || rep->code >= 300) {
            sStatus = rep->code == -2 ? "Connecte d'abord le Wi-Fi" :
                      rep->code == -3 ? "Jeton compile absent" :
                      rep->code == -4 ? "Heure TLS indisponible" :
                      rep->code == -6 ? "Reponse trop volumineuse" :
                      rep->code == -1 && rep->tlsError != 0 ?
                        "Erreur TLS " + String(rep->tlsError) :
                      rep->code == -1 ? "HTTPS -1: DNS / TLS / serveur" :
                      "HTTP serveur " + String(rep->code);
        } else if (rep->op == Op::FETCH) {
            bool fullPage = false;
            JsonDocument doc;
            if (deserializeJson(doc, rep->payload)) {
                sStatus = "Reponse JSON invalide";
            } else {
                JsonArray incoming = doc["messages"].as<JsonArray>();
                if (incoming.isNull()) {
                    sStatus = "Format des messages inconnu";
                } else {
                    int added = 0;
                    // The Worker returns at most ten messages per page. Drain
                    // older pending pages promptly instead of one every 12 s.
                    fullPage = incoming.size() >= 10;
                    for (JsonObject item : incoming) {
                        uint32_t id = item["id"] | 0u;
                        if (id == 0 || id <= sCursor) continue;
                        String kind = item["kind"] | "";
                        const String author = item["sender"] | "?";
                        if (kind == "text") {
                            addHistory(id, author, printable(item["text"].as<String>()), false);
                            added++;
                        } // Ignore les vocaux dans ce module texte.
                        // Cursor covers text and voice to avoid pagination stalls.
                        sCursor = id;
                    }
                    sStatus = added ? String(added) + " nouveau(x) message(s)" : "Messages a jour";
                }
            }
            sPollAt = millis() + (fullPage ? 400UL : kPollMs);
        } else if (rep->op == Op::SEND) {
            // HTTP 2xx alone does not prove the server saved the message.
            // A malformed/empty confirmation must preserve the draft and
            // idempotency key so a retry cannot silently lose user text.
            JsonDocument receipt;
            const bool decoded = !deserializeJson(receipt, rep->payload);
            const uint32_t receiptId = decoded ? (receipt["id"] | 0u) : 0u;
            if (receiptId == 0) {
                sStatus = "Envoi non confirme : brouillon conserve";
                sView = View::STATUS;
            } else {
                addHistory(receiptId, ownRole(sRecipient), printable(sDraft), true);
                sDraft = "";
                sClientId = "";
                if(sRecipient=="bob"){sDraftBob="";sIdBob="";}
                else{sDraftAlice="";sIdAlice="";}
                sStatus = "Message envoye et confirme";
                sView = View::MESSAGES;
                sPollAt = millis() + 1000;
            }
        } else if (rep->op == Op::SESSION) {
            sStatus = "Serveur disponible";
        }
        // Successful idle polls do not repaint the screen every 12 seconds.
        // Repaint on actual new messages or a user-requested operation.
        if(rep->op != Op::FETCH || previousCount != sCount ||
           (sView == View::STATUS && sStatus != previousStatus))
            sDirty = true;
        delete rep;
    }
}

void loadContact() {
    sRecipient = "bob";  // Deterministic fallback if Preferences cannot be opened.
    Preferences prefs;
    if (prefs.begin("nexusneon", true)) {
        const String saved = prefs.getString("contact", "bob");
        prefs.end();
        sRecipient = saved == "alice" ? "alice" : "bob";
    }
    sActiveRecipient = sRecipient;
    // On re-entry, recover the correct per-contact draft instead of showing
    // a draft belonging to the other correspondent.
    sDraft=sRecipient=="bob"?sDraftBob:sDraftAlice;
    sClientId=sRecipient=="bob"?sIdBob:sIdAlice;
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
    // Native port of the V10 Neon horizontal carousel, drawn only on input.
    static const char *labels[4] = {"MESSAGES","ECRIRE","WI-FI","CONTACT"};
    static const char *captions[4] = {
        "Conversations texte","Nouveau message",
        "Etat et choix reseau","Alice / Bob"
    };
    static const uint16_t dark[4] = {0x080C,0x180C,0x0026,0x0805};
    static const uint16_t middle[4] = {0x11C8,0x4209,0x1169,0x1287};
    static const uint16_t accent[4] = {0x05FF,0xC31F,0x07FF,0x3FEA};
    uint16_t d=dark[sHome],m=middle[sHome],a=accent[sHome];
    tft.fillScreen(0x080F);
    for(int y=22;y<114;y+=3) {
        // Gradient stripe painting is limited to actual page or selection changes.
        const uint8_t k=(uint8_t)((y-22)*170/92);
        const uint16_t r=((m>>11&31)*(255-k)+(d>>11&31)*k)/255;
        const uint16_t g=((m>>5&63)*(255-k)+(d>>5&63)*k)/255;
        const uint16_t b=((m&31)*(255-k)+(d&31)*k)/255;
        tft.fillRect(0,y,tftWidth,3,(r<<11)|(g<<5)|b);
    }
    tft.fillRect(0,0,tftWidth,22,0x080F);
    tft.fillRoundRect(5,3,17,15,4,a);
    tft.setTextSize(1);
    tft.setTextColor(0x0000,a);
    tft.drawString("N",10,6,1);
    tft.setTextColor(0xFFFF,0x080F);
    tft.drawString("NEXUS V10 / 10NOV",28,7,1);
    tft.fillRect(0,20,tftWidth,2,a);
    const int x=tftWidth/2, y=62;
    tft.fillRoundRect(x-82,26,164,76,11,d);
    tft.drawRoundRect(x-82,26,164,76,11,a);
    tft.fillTriangle(10,65,30,52,30,78,a);
    tft.fillTriangle(230,65,210,52,210,78,a);
    tft.fillTriangle(15,65,26,58,26,72,0xFFFF);
    tft.fillTriangle(225,65,214,58,214,72,0xFFFF);
    switch(sHome){
    case 0: // envelope
        tft.fillRoundRect(x-28,y-19,56,38,5,a);
        tft.fillRoundRect(x-25,y-16,50,31,3,d);
        tft.drawLine(x-25,y-15,x,y+5,a);
        tft.drawLine(x+25,y-15,x,y+5,a);
        break;
    case 1: // pencil and compose
        tft.drawLine(x-30,y+21,x+25,y-25,a);
        tft.drawLine(x-25,y+26,x+30,y-20,0xFFFF);
        tft.fillTriangle(x-30,y+21,x-25,y+26,x-37,y+29,a);
        break;
    case 2: // network icon
        tft.fillCircle(x,y+20,6,a);
        tft.drawLine(x-34,y-10,x,y-29,a);
        tft.drawLine(x,y-29,x+34,y-10,a);
        tft.drawLine(x-25,y+1,x,y-13,0xFFFF);
        tft.drawLine(x,y-13,x+25,y+1,0xFFFF);
        break;
    case 3: // two contacts
        tft.fillCircle(x-19,y-9,10,a);
        tft.fillCircle(x+19,y-9,10,0xFFFF);
        tft.fillRoundRect(x-38,y+5,36,25,9,a);
        tft.fillRoundRect(x+2,y+5,36,25,9,0xFFFF);
        break;
    }
    tft.fillRoundRect(x-62,87,124,13,3,a);
    tft.setTextColor(0x0000,a);
    tft.drawCentreString(labels[sHome],x,89,1);
    tft.setTextColor(0xFFFF,d);
    tft.drawCentreString(sHome==1 ? (String("Vers ")+sRecipient) :
                         String(captions[sHome]), x,104,1);
    tft.fillRect(0,114,tftWidth,21,0x080F);
    tft.fillRect(0,114,tftWidth,2,a);
    tft.setTextColor(0xD6BA,0x080F);
    tft.drawString("< / > Choisir  ENTREE Ouvrir",6,120,1);
}
void drawMessages() {
    paintFrame(sRecipient == "bob" ? "ALICE > BOB" : "BOB > ALICE");
    tft.setTextSize(1);
    if (sCount == 0) {
        tft.setTextColor(kMuted, kDark);
        tft.drawCentreString("Aucun message", tftWidth / 2, 53, 1);
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

void drawStatus() {
    paintFrame("ETAT NEXUS");
    tft.setTextSize(1);
    tft.setTextColor(0xFFFF,kDark);
    tft.drawString(cut(sStatus,36),8,37,1);
    tft.setTextColor(kNeonCyan,kDark);
    tft.drawString(WiFi.status()==WL_CONNECTED?"Wi-Fi: connecte":"Wi-Fi: absent",8,58,1);
    const size_t freeBlock=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    const size_t freeTotal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    tft.drawString("RAM interne : "+String(freeTotal/1024)+" Ko libres",8,73,1);
    tft.drawString("Plus grand bloc : "+String(freeBlock/1024)+" Ko",8,85,1);
    nvs_stats_t nvs{};
    if(nvs_get_stats(nullptr,&nvs)==ESP_OK && nvs.total_entries){
        const unsigned used=(unsigned)(100*nvs.used_entries/nvs.total_entries);
        tft.drawString("NVS : "+String(used)+" % des entrees",8,96,1);
    }
    tft.setTextColor(kNeonGreen,kDark);
    tft.drawString("ENTREE retour | ESC accueil",8,108,1);
}
void drawWifiStatus() {
    paintFrame("RESEAU KVX PARTAGE");
    tft.setTextSize(1);
    if(WiFi.status()==WL_CONNECTED) {
        tft.setTextColor(kNeonGreen,kDark);
        tft.drawString("Reseau: "+cut(WiFi.SSID(),27),8,35,1);
        tft.drawString("IP: "+WiFi.localIP().toString(),8,53,1);
        tft.setTextColor(0xFFFF,kDark);
        tft.drawString("ENTREE: changer le reseau",8,77,1);
        tft.drawString("(coupe la connexion actuelle)",8,92,1);
    } else {
        tft.setTextColor(kMuted,kDark);
        tft.drawString("Wi-Fi non connecte",8,42,1);
        tft.setTextColor(kNeonCyan,kDark);
        tft.drawString("ENTREE: choisir reseau KVX",8,75,1);
    }
}
void openKvxWifiMenu() {
    // The upstream chooser intentionally refuses when already connected.
    // Disconnect only on an explicit action from the network status screen.
    const bool wasExiting=returnToMenu;
    if(WiFi.status()==WL_CONNECTED) wifiDisconnect();
    const bool ok=wifiConnectMenu(WIFI_STA);
    if(!wasExiting) returnToMenu=false;
    EscPress=false;SelPress=false;PrevPress=false;NextPress=false;
    UpPress=false;DownPress=false;
    sStatus=(ok||WiFi.status()==WL_CONNECTED)?
      "Wi-Fi KVX connecte":"Wi-Fi annule/connexion echouee";
    sDirty=true;
}
void paint() {
    switch (sView) {
    case View::HOME: drawHome(); break;
    case View::MESSAGES: drawMessages(); break;
    case View::DETAIL: drawDetail(); break;
    case View::CONFIRM: drawConfirm(); break;
    case View::CONTACT: drawContact(); break;
    case View::STATUS: drawStatus(); break;
    case View::WIFI_STATUS: drawWifiStatus(); break;
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
    // Reopening the same draft must keep its retry ID after an ambiguous send.
    if(typed!=sDraft)sClientId="";
    sDraft = typed;
    if(sRecipient=="bob"){sDraftBob=sDraft;sIdBob=sClientId;}
    else{sDraftAlice=sDraft;sIdAlice=sClientId;}
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
        if(sRecipient=="bob")sIdBob=sClientId;
        else sIdAlice=sClientId;
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
        sView=View::WIFI_STATUS;
        sStatus=WiFi.status()==WL_CONNECTED?"Wi-Fi KVX partage actif":"Wi-Fi non connecte";
    } else if (sHome==3) {
        sView=View::CONTACT;
    }
    sDirty=true;
}

void handleKeys(bool &exitApp) {
    // FN+ESC is the *only* direct exit; a simple ESC goes back in Nexus.
    // ADV KeyStroke captures FN status at the moment of the key event.
    keyStroke key = _getKeyPress();
    bool esc = pressed(EscPress);
    if (esc && (key.fn || isFnKeyHeld())) {
        EscPress = false;
        exitApp = true;
        return;
    }
    if (esc && !(key.pressed && key.enter)) {
        if (sView == View::HOME) return;
        if (sView == View::DETAIL) sView = View::MESSAGES;
        else {
            if (sView == View::CONTACT) sRecipient = sActiveRecipient;
            sView = View::HOME;
        }
        sDirty = true;
        return;
    }
    if (pressed(UpPress)) {
        if (sView == View::HOME) sHome = (sHome + 3) % 4;
        else if (sView == View::MESSAGES && sCount) sSelected = max(0, sSelected - 1);
        sDirty = true;
    }
    if (pressed(DownPress)) {
        if (sView == View::HOME) sHome = (sHome + 1) % 4;
        else if (sView == View::MESSAGES && sCount) sSelected = min(sCount - 1, sSelected + 1);
        sDirty = true;
    }
    if (pressed(PrevPress)) {
        if (sView == View::HOME) sHome = (sHome + 3) % 4;
        else if (sView == View::MESSAGES && !sBusy) beginOperation(Op::FETCH);
        else if (sView == View::CONTACT) sRecipient = sRecipient == "bob" ? "alice" : "bob";
        else if (sView == View::DETAIL && sCount) sSelected = max(0, sSelected - 1);
        sDirty = true;
    }
    if (pressed(NextPress)) {
        if (sView == View::HOME) sHome = (sHome + 1) % 4;
        else if (sView == View::MESSAGES && !sBusy) newDraft();
        else if (sView == View::CONTACT) sRecipient = sRecipient == "bob" ? "alice" : "bob";
        else if (sView == View::DETAIL && sCount) sSelected = min(sCount - 1, sSelected + 1);
        sDirty = true;
    }
    const bool selectFlag=pressed(SelPress);
    if (selectFlag || (key.pressed && !key.fn && key.enter)) {
        if (sView == View::HOME) handleHomeSelect();
        else if (sView == View::MESSAGES && sCount) sView = View::DETAIL;
        else if (sView == View::CONFIRM) sendDraft();
        else if (sView == View::WIFI_STATUS) openKvxWifiMenu();
        else if (sView == View::CONTACT) {
            if (sActiveRecipient != sRecipient) {
                // Do not show Bob's messages under Alice's identity (or vice versa).
                // Pending responses from the previous role are discarded by generation.
                ++sGeneration;
                sCount = 0;
                sSelected = 0;
                sCursor = 0;
                if (sActiveRecipient=="bob") {
                    sDraftBob=sDraft;
                    sIdBob=sClientId;
                } else {
                    sDraftAlice=sDraft;
                    sIdAlice=sClientId;
                }
                sDraft=sRecipient=="bob"?sDraftBob:sDraftAlice;
                sClientId=sRecipient=="bob"?sIdBob:sIdAlice;
                sPollAt = millis();
                sActiveRecipient = sRecipient;
            }
            saveContact();
            // Standalone Nexus UX: choose Alice/Bob and type immediately.
            // The icon-pencil compose tile and existing colored carousel stay.
            sView = View::HOME;
            sHome = 1;
            newDraft();
        }
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
    sView = View::HOME; sHome = 0; // Preserve unsent draft on return from KVX.
    sStatus = isConfigured() ? "FN+ESC: retour KVX" : "Compilation sans jeton";
    sDirty = true;
    sPollAt = millis() + kPollMs;
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
    if(sBusy.load()){
        tft.fillScreen(kDark);
        tft.setTextSize(1);
        tft.drawCentreString("Nexus : fermeture HTTPS",tftWidth/2,tftHeight/2,1);
    }
    while(sBusy.load()){
        acceptReply();
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    if(sReplyQueue){vQueueDelete(sReplyQueue);sReplyQueue=nullptr;}
    for(auto &message : sItems)message={};
    sCount=0;
    tft.fillScreen(kvxConfig.bgColor);
#endif
}
