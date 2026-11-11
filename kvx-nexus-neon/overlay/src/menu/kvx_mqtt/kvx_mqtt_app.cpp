// NEXUS MQTT 0.2.3 - Physical arrow keys, stable partial UI, background TLS/MQTT; Wi-Fi no NVS.
// Text-only first milestone. No LoRa radio, no audio, no external credentials built in.
#include "root/ui/display.h"
#include "root/input/mykeyboard.h"
#include "root/storage/sd_functions.h"
#include "root/net/wifi_common.h"
#include "root/app/utils.h"
#include <globals.h>
#include "kvx_mqtt_private.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace KvxMqtt {
std::atomic<bool> stopWorker{false}, workerFinished{true};
static const char *CONFIG_FILE="/mqtt/config.ini";
static const char *CA_FILE="/mqtt/ca.pem";
static const char *HISTORY_FILE="/mqtt/history.jsonl";
static const char *OUTBOX_FILE="/mqtt/outbox.jsonl";
static const char *OUTBOX_TMP="/mqtt/outbox.tmp";
static const char *OUTBOX_BAK="/mqtt/outbox.bak";
static const uint32_t HISTORY_LIMIT=262144;
static const size_t MAX_TEXT=160;

struct Config {
  String wifiSsid, wifiPass, host, user, pass, id, peer;
  uint16_t port=8883;
} cfg;
Config beforeSettings; // Snapshot, so cancelling cannot corrupt live settings.
struct Message { String id, from, text; uint32_t timestamp=0; };

String caPem, draft, statusText="Initialisation...", editBuf;
std::vector<Message> recent;
bool sdOK=false, inSettings=false, configured=false, pendingReconfigure=false;
enum class Page : uint8_t { Home, Chat, History, Detail, Network, Settings };
Page currentPage=Page::Home;
uint8_t selectedTile=0;
int selectedHistory=0;
size_t settingsTextOffset=0; // Fn+left/right scrolls unmasked long fields.
const uint16_t C_BG=0x0843, C_BAR=0x1087, C_PANEL=0x194A;
const uint16_t C_CYAN=0x3E7F, C_GREEN=0x562D, C_PURPLE=0xA2FA;
const uint16_t C_ORANGE=0xFD46, C_WHITE=0xFFFF, C_MUTED=0x9C9A;
const uint16_t C_RED=0xF284;
uint8_t settingsIdx=0;
// Network I/O lives on core 0. UI and all microSD access stay on the Arduino loop.
// FreeRTOS queue elements are POD, so no Arduino String crosses the task boundary.
struct NetPacket { char payload[640]; };
struct NetAck { bool ok; };
struct NetNotice { char text[64]; char ip[24]; };
struct NetConfig { String wifiSsid,wifiPass,host,user,pass,id,peer; uint16_t port; } netCfg;
QueueHandle_t netTxQueue=nullptr,netAckQueue=nullptr,netRxQueue=nullptr,netNoticeQueue=nullptr;
std::atomic<bool> netWifiUp{false}, netMqttUp{false};
std::atomic<unsigned> droppedPackets{0};
bool txWaiting=false;
String netIp="--";
uint32_t lastSendAttempt=0,lastDraw=0;
uint64_t lastPhysicalKeyMask=0;
Page lastRenderedPage=Page::Home;
bool uiInitialized=false;
int scrollPos=0;

const char* settingNames[]={"WiFi SSID","WiFi mot de passe","Broker DNS","Port TLS","Compte MQTT","Mot de passe MQTT","Cet appareil a/b","Destinataire a/b"};
static constexpr uint8_t SETTINGS_COUNT=8;

bool validNode(const String &s){return s=="a"||s=="b";}
String topicFor(const String &id){return String("nexusmqtt/v1/")+id+"/in";}
String strValue(const String &key){
  if(key=="wifi_ssid") return cfg.wifiSsid;
  if(key=="wifi_password") return cfg.wifiPass;
  if(key=="host") return cfg.host;
  if(key=="username") return cfg.user;
  if(key=="password") return cfg.pass;
  if(key=="device_id") return cfg.id;
  if(key=="peer_id") return cfg.peer;
  if(key=="port") return String(cfg.port);
  return "";
}
void setValue(const String &key, const String &val){
  if(key=="wifi_ssid")cfg.wifiSsid=val;
  else if(key=="wifi_password")cfg.wifiPass=val;
  else if(key=="host")cfg.host=val;
  else if(key=="username")cfg.user=val;
  else if(key=="password")cfg.pass=val;
  else if(key=="device_id")cfg.id=val;
  else if(key=="peer_id")cfg.peer=val;
  else if(key=="port") {int p=val.toInt();cfg.port=(p>0&&p<65536)?(uint16_t)p:0;}
}
const char *settingKey(uint8_t n){
  static const char* keys[]={"wifi_ssid","wifi_password","host","port","username","password","device_id","peer_id"};
  return keys[n];
}
bool configIsValid(){
  return cfg.host.length() && cfg.user.length() &&
         cfg.pass.length() && cfg.port>0 && validNode(cfg.id) && validNode(cfg.peer) && cfg.id!=cfg.peer;
}
bool loadConfig(){
  // Fallback values are private build configuration. If an existing
  // /mqtt/config.ini is present, its entries take precedence.
  cfg.wifiSsid=""; cfg.wifiPass="";
  cfg.host=KVX_MQTT_HOST; cfg.user=KVX_MQTT_USER; cfg.pass=KVX_MQTT_PASSWORD;
  cfg.id=KVX_MQTT_DEVICE; cfg.peer=KVX_MQTT_PEER; cfg.port=KVX_MQTT_PORT;
  if (!sdOK) return configIsValid();
  File f=SD.open(CONFIG_FILE,FILE_READ);
  if(!f)return configIsValid();
  // Refuse an oversized or corrupted INI before readStringUntil allocates RAM.
  if(f.size()>8192){f.close();return false;}
  while(f.available()){
    String line=f.readStringUntil('\n');line.trim();
    if(!line.length()||line.startsWith("#"))continue;
    int pos=line.indexOf('=');if(pos<=0)continue;
    String key=line.substring(0,pos),value=line.substring(pos+1);
    key.trim();
    // The standalone editor permits passwords containing apostrophes, '=',
    // and deliberate leading/trailing spaces. Only strip the CR in CRLF.
    if(value.endsWith("\r"))value.remove(value.length()-1);
    setValue(key,value);
  }
  f.close();return configIsValid();
}
bool saveConfig(){
  if(!sdOK || !configIsValid())return false;
  SD.remove("/mqtt/config.tmp");
  File f=SD.open("/mqtt/config.tmp",FILE_WRITE);
  if(!f)return false;
  bool written=true;
  for(uint8_t i=0;i<SETTINGS_COUNT;i++){
    const String line=String(settingKey(i))+"="+strValue(settingKey(i))+"\n";
    written = (f.print(line) == line.length()) && written;
  }
  f.flush();
  f.close();
  if(!written){SD.remove("/mqtt/config.tmp");return false;}
  File verify=SD.open("/mqtt/config.tmp",FILE_READ);
  const bool good=verify && verify.size()>=40;
  if(verify)verify.close();
  if(!good){SD.remove("/mqtt/config.tmp");return false;}
  SD.remove("/mqtt/config.bak");
  if(SD.exists(CONFIG_FILE) && !SD.rename(CONFIG_FILE,"/mqtt/config.bak"))return false;
  if(!SD.rename("/mqtt/config.tmp",CONFIG_FILE)){
    if(SD.exists("/mqtt/config.bak"))SD.rename("/mqtt/config.bak",CONFIG_FILE);
    return false;
  }
  SD.remove("/mqtt/config.bak");return true;
}
String readCA(){
  if(!sdOK)return "";
  File f=SD.open(CA_FILE,FILE_READ);
  if(!f)return "";
  if(f.size()<100||f.size()>8192){f.close();return "";}
  String cert=f.readString();f.close();
  if(cert.indexOf("-----BEGIN CERTIFICATE-----")<0 ||
     cert.indexOf("-----END CERTIFICATE-----")<0)return "";
  return cert;
}
void setStatus(const String &s){
  if(statusText==s)return;
  statusText=s;
  // Network and settings display status; other pages keep stable frames.
  if(currentPage==Page::Network || currentPage==Page::Settings)lastDraw=0;
}
void keepRecent(const Message &m){
  recent.push_back(m);
  if(recent.size()>35) recent.erase(recent.begin());
  scrollPos=0;
}
bool alreadySeen(const String &id){
  for(const Message &m:recent)if(m.id==id)return true;
  return false;
}
void logHistory(const Message &m){
  if(!sdOK)return;
  if(SD.exists(HISTORY_FILE)){
    File chk=SD.open(HISTORY_FILE,FILE_READ);
    size_t sz=chk?chk.size():0;if(chk)chk.close();
    if(sz>HISTORY_LIMIT){
      SD.remove("/mqtt/history.old.jsonl");
      SD.rename(HISTORY_FILE,"/mqtt/history.old.jsonl");
    }
  }
  File f=SD.open(HISTORY_FILE,FILE_APPEND);
  if(!f)return;
  JsonDocument doc;doc["id"]=m.id;doc["from"]=m.from;
  doc["text"]=m.text;doc["ts"]=m.timestamp;
  serializeJson(doc,f);f.println();f.close();
}
void loadHistory(){
  if(!sdOK)return;
  File f=SD.open(HISTORY_FILE,FILE_READ);if(!f)return;
  if(f.size()>16384){
    f.seek(f.size()-16384);
    f.readStringUntil('\n'); // skip truncated first record
  }
  while(f.available()){
    String line=f.readStringUntil('\n');
    if(line.length()>600)continue;
    JsonDocument d;if(deserializeJson(d,line))continue;
    Message m;
    m.id=d["id"].as<String>();m.from=d["from"].as<String>();
    m.text=d["text"].as<String>();m.timestamp=d["ts"]|0u;
    if(m.id.length() && m.text.length()<=MAX_TEXT)keepRecent(m);
  }
  f.close();
}
bool queueOutbox(const String &text){
  if(!sdOK||text.isEmpty()||text.length()>MAX_TEXT)return false;
  if(SD.exists(OUTBOX_FILE)){
    File existing=SD.open(OUTBOX_FILE,FILE_READ);
    size_t sz=existing?existing.size():0;if(existing)existing.close();
    if(sz>131072){setStatus("File d'attente pleine");return false;}
  }
  Message m;
  m.id=cfg.id+"-"+String((uint32_t)time(nullptr))+"-"+String(esp_random(),HEX);
  m.from=cfg.id;m.text=text;m.timestamp=(uint32_t)time(nullptr);
  JsonDocument d;d["id"]=m.id;d["from"]=m.from;d["text"]=m.text;d["ts"]=m.timestamp;
  File f=SD.open(OUTBOX_FILE,FILE_APPEND);if(!f)return false;
  if(serializeJson(d,f)==0){f.close();return false;}
  f.println();f.close();logHistory(m);keepRecent(m);return true;
}
void removeFirstOutboxLine(){
  File src=SD.open(OUTBOX_FILE,FILE_READ);if(!src)return;
  SD.remove(OUTBOX_TMP);
  File dst=SD.open(OUTBOX_TMP,FILE_WRITE);if(!dst){src.close();return;}
  src.readStringUntil('\n');
  while(src.available()){
    uint8_t buf[256];size_t n=src.read(buf,sizeof(buf));
    if(n && dst.write(buf,n)!=n){src.close();dst.close();SD.remove(OUTBOX_TMP);return;}
  }
  src.close();dst.close();
  // Two-phase SD update: on restart, if the active file is missing,
  // restore the original .bak and resend; QoS1 may duplicate, IDs deduplicate.
  SD.remove(OUTBOX_BAK);
  if(!SD.rename(OUTBOX_FILE,OUTBOX_BAK)){
    SD.remove(OUTBOX_TMP);setStatus("Erreur rotation file");return;
  }
  if(!SD.rename(OUTBOX_TMP,OUTBOX_FILE)){
    SD.rename(OUTBOX_BAK,OUTBOX_FILE);
    setStatus("Erreur sauvegarde file");return;
  }
  SD.remove(OUTBOX_BAK);
}
// SD and outbox operations only happen in the UI task, never concurrently with TLS.
void sendQueued(){
  if(txWaiting||!netMqttUp.load()||!sdOK||millis()-lastSendAttempt<850)return;
  lastSendAttempt=millis();
  File f=SD.open(OUTBOX_FILE,FILE_READ);if(!f)return;
  String line=f.readStringUntil('\n');f.close();
  if(!line.length())return;
  if(line.length()>600){setStatus("File corrompue : conserver SD");return;}
  JsonDocument d;if(deserializeJson(d,line)){setStatus("File invalide : conserver SD");return;}
  String payload;serializeJson(d,payload);
  if(payload.length()>=sizeof(NetPacket::payload)){setStatus("Message trop long");return;}
  NetPacket p{};memcpy(p.payload,payload.c_str(),payload.length()+1);
  if(xQueueSend(netTxQueue,&p,0)==pdPASS)txWaiting=true;
}
// Callback runs only in networkTask: do not draw or touch the microSD here.
void onIncoming(String &topic, String &payload){
  if(topic!=topicFor(netCfg.id)||payload.isEmpty()||payload.length()>600)return;
  if(!netRxQueue)return;
  NetPacket p{};memcpy(p.payload,payload.c_str(),payload.length()+1);
  // Do not block the MQTT parser when the UI queue fills.
  if(xQueueSend(netRxQueue,&p,0)!=pdPASS)++droppedPackets;
}
void processIncoming(const String &payload){
  JsonDocument d;if(deserializeJson(d,payload))return;
  String id=d["id"].as<String>(),from=d["from"].as<String>(),text=d["text"].as<String>();
  if(!id.length()||id.length()>80||from!=cfg.peer||text.isEmpty()||text.length()>MAX_TEXT)return;
  if(alreadySeen(id))return;
  Message m; m.id=id;m.from=from;m.text=text;m.timestamp=d["ts"]|0u;
  logHistory(m);keepRecent(m);setStatus("Nouveau message !");lastDraw=0;
}
void startSettings(){
  beforeSettings=cfg; // All eight entries form one transaction.
  inSettings=true;currentPage=Page::Settings;
  // Expose the broker first when already configured: this is useful when
  // the MQTT TLS connection fails and the user needs to inspect host/port.
  // All eight fields remain reachable using the arrow keys.
  settingsIdx=configIsValid()?2:0;
  editBuf=strValue(settingKey(settingsIdx));
  settingsTextOffset=0;
  statusText="Taper directement, ENTREE pour sauvegarder";lastDraw=0;
}
void finishSetting(){
  // Save the selected field directly, preserving the original settings
  // transaction and the previously usable configuration on any failure.
  editBuf.replace("\n","");editBuf.replace("\r","");
  const String key=settingKey(settingsIdx);
  const Config staged=cfg;
  const bool wasValid=configIsValid();
  if(settingsIdx==3){
    bool onlyDigits=!editBuf.isEmpty();
    for(size_t i=0;i<editBuf.length();++i){
      if(editBuf[i]<'0'||editBuf[i]>'9')onlyDigits=false;
    }
    const long port=editBuf.toInt();
    if(!onlyDigits||port<1||port>65535){
      setStatus("Port TLS invalide (1-65535)");
      editBuf=strValue(key);lastDraw=0;return;
    }
  }
  setValue(key,editBuf);
  if(!configIsValid()){
    // On first installation, collect remaining values without saving partial
    // credentials. On an already configured device, roll back bad entries.
    if(!wasValid && settingsIdx+1<SETTINGS_COUNT){
      ++settingsIdx;
      editBuf=strValue(settingKey(settingsIdx));
      statusText="Completer les champs MQTT";
    }else{
      cfg=staged;
      editBuf=strValue(key);
      setStatus("MQTT: hote, compte ou a/b invalide");
    }
    settingsTextOffset=0;lastDraw=0;return;
  }
  if(!saveConfig()){
    cfg=beforeSettings; // Existing saved credentials remain authoritative.
    editBuf=strValue(key);
    setStatus("Sauvegarde microSD echouee; ancienne config conservee");
    settingsTextOffset=0;lastDraw=0;return;
  }
  beforeSettings=cfg;
  pendingReconfigure=true;
  stopWorker.store(true);
  inSettings=false;
  currentPage=Page::Network;
  setStatus("Champ sauve; reconnexion MQTT...");
  settingsTextOffset=0;lastDraw=0;
}
void navigateHistory(int delta){
  if(recent.empty())return;
  selectedHistory+=delta;
  if(selectedHistory<0)selectedHistory=0;
  if(selectedHistory>=(int)recent.size())selectedHistory=recent.size()-1;
  scrollPos=(int)recent.size()-1-selectedHistory;
}
void openTile(){
  if(selectedTile==0)currentPage=Page::Chat;
  if(selectedTile==1){currentPage=Page::History;selectedHistory=recent.empty()?0:recent.size()-1;scrollPos=0;}
  if(selectedTile==2)currentPage=Page::Network;
  if(selectedTile==3)startSettings();
  lastDraw=0;
}
// Read each newly pressed physical key, not Keyboard.isChange(): the latter
// only compares key COUNT and misses equal-count swaps between arrow keys.
// Keys painted with arrows are ; (UP), , (LEFT), . (DOWN), / (RIGHT).
// On menus they navigate directly, without Fn. In text input the punctuation
// stays available, and Shift + punctuation remains available in all modes.
void onKey(uint8_t code,char typed,bool fn,bool shift){
  // KEY_FN and other nonprinting modifiers are ignored as standalone keys.
  const bool arrow=code==';'||code==','||code=='.'||code=='/';
  const bool back=code=='`' && (currentPage!=Page::Settings || fn);
  const bool enter=code==13;
  const bool tab=code==9;
  const bool del=code==8;
  const bool directArrow=arrow && !shift;
  const Page oldPage=currentPage;
  const uint8_t oldTile=selectedTile;
  const int oldHistory=selectedHistory, oldScroll=scrollPos;
  const String oldDraft=draft,oldEdit=editBuf;
  bool changed=false;
  if(currentPage==Page::Settings){
    if(code==96 && !fn) {
      // ESC cancels the whole form, not just the visible keyboard widget.
      cfg=beforeSettings;
      inSettings=false;
      currentPage=Page::Home;
      statusText="Reglages annules : valeurs restaurees";
      changed=true;
    }
    // Raw ported keyboard characters must not be inserted into configuration.
    // Use KVXputer's native editor from keyPress() instead.
  }else if(currentPage==Page::Home){
    if(directArrow){
      if(code==',' && selectedTile%2==1)selectedTile--;
      if(code=='/' && selectedTile%2==0)selectedTile++;
      if(code==';' && selectedTile>=2)selectedTile-=2;
      if(code=='.' && selectedTile<2)selectedTile+=2;
    }
    if(enter){openTile();changed=true;}
    if(fn && code=='1'){currentPage=Page::Chat;changed=true;}
  }else if(currentPage==Page::Chat){
    if(tab){startSettings();changed=true;}
    else if(back){if(draft.length())draft="";else currentPage=Page::Home;changed=true;}
    else if(enter){
      draft.trim();
      if(draft.length() && queueOutbox(draft)){draft="";setStatus("Message sauvegarde");}
      else if(draft.length())setStatus("Erreur SD / longueur");
      changed=true;
    }else if(del && draft.length()){draft.remove(draft.length()-1);changed=true;}
    else if(directArrow && (draft.isEmpty() || fn)){
      // Arrow keys scroll while not composing; while composing, punctuation
      // is typed normally rather than losing '.', ',' and '/'.
      if(code==';' && scrollPos<30)scrollPos++;
      if(code=='.' && scrollPos>0)scrollPos--;
    }else if(!fn && typed>=32&&typed<=126&&draft.length()<MAX_TEXT){draft+=typed;changed=true;}
  }else if(currentPage==Page::History){
    if(back){currentPage=Page::Home;scrollPos=0;changed=true;}
    else if(directArrow){if(code==';'||code==',')navigateHistory(-1);
                             else navigateHistory(1);}
    else if(enter&&!recent.empty()){currentPage=Page::Detail;changed=true;}
  }else if(currentPage==Page::Detail){
    if(back||enter){currentPage=Page::History;changed=true;}
  }else if(currentPage==Page::Network){
    if(back){currentPage=Page::Home;changed=true;}
    else if(enter){
      // The integrated app shares KVX's radio and credentials. The standalone
      // MQTT editor's SSID/password fields do not own Wi-Fi association here.
      if(WiFi.status()!=WL_CONNECTED){
        const bool wasExiting=returnToMenu;
        bool connected=wifiConnectMenu();
        if(!wasExiting) returnToMenu=false; // ESC cancels Wi-Fi, not the MQTT app.
        setStatus(connected||WiFi.status()==WL_CONNECTED?
                  "Wi-Fi partage connecte":"Wi-Fi absent : connexion annulee");
      }else{
        setStatus("Wi-Fi KVX deja connecte");
      }
      changed=true;
    }
  }
  if(currentPage!=oldPage || selectedTile!=oldTile ||
     selectedHistory!=oldHistory||scrollPos!=oldScroll||
     draft!=oldDraft||editBuf!=oldEdit||changed) lastDraw=0;
}
void keyPress(){
  // ADV keyboard is read by the KVXputer input handler. Never start another
  // keyboard scanner, second task, or M5Cardputer instance.
  keyStroke k = _getKeyPress();
  bool esc=check(EscPress);
  if ((esc && isFnKeyHeld()) || (k.pressed && k.fn && k.exit_key)) {
    EscPress=false; returnToMenu=true; return;
  }
  if(esc || (k.pressed && k.exit_key)) { onKey(96, char(96), false, false); return; }
  bool up=check(UpPress), down=check(DownPress), left=check(PrevPress), right=check(NextPress);
  if(currentPage==Page::Settings) {
    // Native compact inline editor: no oversized full-screen keyboard dialog,
    // no loss of Wi-Fi/MQTT values when entering an individual field.
    // Physical arrows select fields, Fn+arrows scroll long values, Enter saves.
    if(k.pressed && k.fn && k.del){
      editBuf="";settingsTextOffset=0;
      setStatus("Champ vide, saisissez la nouvelle valeur");lastDraw=0;
    }else if(k.pressed && k.fn && (left || right)){
      if(left)settingsTextOffset=settingsTextOffset>=35?settingsTextOffset-35:0;
      if(right)settingsTextOffset=std::min(editBuf.length(),settingsTextOffset+35);
      lastDraw=0;
    }else if(up || left){
      if(settingsIdx>0)--settingsIdx;
      editBuf=strValue(settingKey(settingsIdx));
      settingsTextOffset=0;lastDraw=0;
    }else if(down || right){
      if(settingsIdx+1<SETTINGS_COUNT)++settingsIdx;
      editBuf=strValue(settingKey(settingsIdx));
      settingsTextOffset=0;lastDraw=0;
    }else if(check(SelPress) || (k.pressed && k.enter)){
      finishSetting();
      lastDraw=0;
    }else if(k.pressed && k.del){
      if(editBuf.length())editBuf.remove(editBuf.length()-1);
      settingsTextOffset=editBuf.length()>70?editBuf.length()-70:0;
      lastDraw=0;
    }else if(k.pressed && !k.fn){
      for(char ch:k.word){
        if((unsigned char)ch>=32 && (unsigned char)ch<=126 &&
           editBuf.length()<110) editBuf+=ch;
      }
      settingsTextOffset=editBuf.length()>70?editBuf.length()-70:0;
      lastDraw=0;
    }
    return; // One physical key event must not enter another screen.
  }
  if (up) onKey(';',';',false,false);
  if (down) onKey('.','.',false,false);
  if (left) onKey(',',',',false,false);
  if (right) onKey('/','/',false,false);
  if (check(SelPress) || (k.pressed && k.enter)) onKey(13,'\r',false,false);
  if (k.pressed && k.del) onKey(8,'\b',false,false);
  if (!k.pressed || k.fn) return;
  for (char ch : k.word) {
    if (ch == '\t') { onKey(9,'\t',false,false);continue; }
    if ((up && ch==';') || (down && ch=='.') || 
        (left && ch==',') || (right && ch=='/')) continue;
    if ((unsigned char)ch>=32 && (unsigned char)ch<=126)
      onKey((uint8_t)ch,ch,false,false);
  }
}
String trimText(const String &s, int limit){
  if((int)s.length()<=limit)return s;
  if(limit<=2)return s.substring(0,limit);
  return s.substring(0,limit-1)+"~";
}
void drawText(const String &s,int x,int y,uint16_t fg,uint16_t bg=C_BG){
  auto &d=tft;
  d.setTextSize(1);d.setTextColor(fg,bg);d.setCursor(x,y);d.print(s);
}
void drawTopBar(const String &title){
  auto &d=tft;
  d.fillRect(0,0,240,22,C_BAR);
  d.fillRoundRect(6,4,14,14,4,C_CYAN);
  drawText("N",10,7,C_BG,C_CYAN);
  drawText(trimText(title,18),26,8,C_WHITE,C_BAR);
  d.fillCircle(180,11,3,netWifiUp.load()?C_GREEN:C_RED);
  d.fillCircle(201,11,3,netMqttUp.load()?C_GREEN:C_ORANGE);
  d.fillCircle(222,11,3,sdOK?C_GREEN:C_RED);
  d.drawFastHLine(0,22,240,C_CYAN);
}
void drawFooter(const String &hint){
  auto &d=tft;
  d.fillRect(0,122,240,13,C_BAR);
  drawText(trimText(hint,39),5,125,C_MUTED,C_BAR);
}
void drawGlyph(uint8_t type,int x,int y,uint16_t color){
  auto &d=tft;
  switch(type){
    case 0:
      d.drawRoundRect(x,y,20,13,3,color);
      d.drawLine(x+4,y+13,x+2,y+17,color);
      d.drawFastHLine(x+4,y+17,7,color);
      d.drawFastHLine(x+4,y+5,12,color);
      d.drawFastHLine(x+4,y+9,8,color);
      break;
    case 1:
      d.drawCircle(x+10,y+9,9,color);
      d.drawLine(x+10,y+9,x+10,y+3,color);
      d.drawLine(x+10,y+9,x+16,y+10,color);
      d.fillCircle(x+10,y+9,2,color);
      break;
    case 2:
      d.fillCircle(x+10,y+14,2,color);
      d.drawLine(x+4,y+9,x+10,y+3,color);
      d.drawLine(x+10,y+3,x+16,y+9,color);
      d.drawLine(x+1,y+5,x+10,y-2,color);
      d.drawLine(x+10,y-2,x+19,y+5,color);
      break;
    default:
      d.drawCircle(x+10,y+9,8,color);
      d.drawCircle(x+10,y+9,3,color);
      for(int k=0;k<4;k++){
        if(k%2==0)d.drawFastHLine(x+1,y+9+(k-1)*4,18,color);
        else d.drawFastVLine(x+10+(k-2)*4,y,18,color);
      }
  }
}
void drawHome(){
  auto &d=tft;
  const uint16_t accent[]={C_CYAN,C_PURPLE,C_GREEN,C_ORANGE};
  const char *titles[]={"MESSAGES","HISTORIQUE","RESEAU","REGLAGES"};
  const char *subs[]={"Discuter","microSD","Wi-Fi / MQTT","Connexion"};
  for(int i=0;i<4;i++){
    int x=6+(i%2)*118,y=28+(i/2)*45;
    bool selected=selectedTile==i;
    d.fillRoundRect(x,y,110,40,7,selected?C_PANEL:C_BAR);
    d.drawRoundRect(x,y,110,40,7,selected?accent[i]:C_PANEL);
    d.fillRoundRect(x+5,y+7,28,26,5,C_BG);
    drawGlyph(i,x+9,y+12,accent[i]);
    drawText(titles[i],x+37,y+11,selected?accent[i]:C_WHITE,selected?C_PANEL:C_BAR);
    drawText(subs[i],x+37,y+23,C_MUTED,selected?C_PANEL:C_BAR);
  }
  drawFooter("FLECHES: choix | ENTER ouvrir | FN+1 chat");
}
void drawChat(){
  auto &d=tft;
  int end=(int)recent.size()-1-scrollPos;
  for(int r=0;r<2;r++){
    int idx=end-(1-r);
    if(idx<0||idx>=(int)recent.size())continue;
    const Message &m=recent[idx];
    bool own=m.from==cfg.id;
    int x=own?30:5,y=27+r*31;
    uint16_t col=own?0x11AF:0x292C;
    d.fillRoundRect(x,y,205,29,6,col);
    d.fillRoundRect(x+5,y+5,23,9,3,own?C_CYAN:C_PURPLE);
    drawText(own?"MOI":"AMI",x+8,y+6,C_BG,own?C_CYAN:C_PURPLE);
    drawText(trimText(m.text,30),x+32,y+8,C_WHITE,col);
    if(m.text.length()>30)drawText(trimText(m.text.substring(30),32),x+6,y+20,C_MUTED,col);
  }
  d.fillRoundRect(5,92,230,27,6,C_PANEL);
  d.drawRoundRect(5,92,230,27,6,C_CYAN);
  drawText("ECRIRE >",11,96,C_CYAN,C_PANEL);
  drawText(trimText(draft,35),11,107,C_WHITE,C_PANEL);
  drawFooter("ENTER envoyer | ` menu | TAB config");
}
void drawHistory(){
  auto &d=tft;
  if(recent.empty())drawText("Aucun message enregistre",8,57,C_MUTED);
  else{
    int start=selectedHistory-1;
    if(start<0)start=0;
    if(start+3>(int)recent.size())start=recent.size()>3?recent.size()-3:0;
    for(int r=0;r<3;r++){
      int idx=start+r;if(idx>=(int)recent.size())break;
      const Message &m=recent[idx];int y=27+r*31;
      uint16_t bg=idx==selectedHistory?C_PANEL:C_BAR;
      uint16_t accent=m.from==cfg.id?C_CYAN:C_PURPLE;
      d.fillRoundRect(5,y,230,27,5,bg);
      if(idx==selectedHistory)d.drawRoundRect(5,y,230,27,5,accent);
      drawText(m.from==cfg.id?"MOI":"AMI",10,y+4,accent,bg);
      drawText(trimText(m.text,34),10,y+15,C_WHITE,bg);
    }
  }
  drawFooter("FLECHES lire | ENTER ouvrir | ` menu");
}
void drawDetail(){
  auto &d=tft;
  if(selectedHistory<0||selectedHistory>=(int)recent.size())return;
  const Message &m=recent[selectedHistory];
  drawText(m.from==cfg.id?"MESSAGE ENVOYE":"MESSAGE RECU",8,29,C_CYAN);
  int pos=0;
  for(int row=0;row<6&&pos<(int)m.text.length();row++){
    String chunk=m.text.substring(pos,pos+37);
    drawText(chunk,8,43+row*12,C_WHITE);
    pos+=chunk.length();
  }
  drawFooter("` / ENTER : historique");
}
void drawNetwork(){
  auto &d=tft;
  d.fillRoundRect(5,29,230,87,6,C_PANEL);
  drawText("Wi-Fi",11,36,C_CYAN,C_PANEL);
  drawText(netWifiUp.load()?"Connecte":"Hors ligne",105,36,netWifiUp.load()?C_GREEN:C_ORANGE,C_PANEL);
  drawText("MQTT",11,51,C_CYAN,C_PANEL);
  drawText(netMqttUp.load()?"Connecte":"Non connecte",105,51,netMqttUp.load()?C_GREEN:C_ORANGE,C_PANEL);
  drawText("microSD",11,66,C_CYAN,C_PANEL);
  drawText(sdOK?"OK":"Absente",62,66,sdOK?C_GREEN:C_RED,C_PANEL);
  const size_t largestHeap=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  drawText("Bloc RAM",116,66,C_CYAN,C_PANEL);
  drawText(String(largestHeap/1024)+" Ko",181,66,largestHeap>=16384?C_GREEN:C_ORANGE,C_PANEL);
  drawText("IP",11,81,C_CYAN,C_PANEL);
  drawText(netWifiUp.load()?trimText(netIp,21):"--",55,81,C_WHITE,C_PANEL);
  drawText(trimText(statusText,34),11,101,C_MUTED,C_PANEL);
  drawFooter("ENTREE connecter Wi-Fi  |  ESC retour");
}
void drawSettings(){
  auto &d=tft;
  drawText("CONFIGURATION  "+String(settingsIdx+1)+" / "+String(SETTINGS_COUNT),7,30,C_CYAN);
  drawText(trimText(settingNames[settingsIdx],34),7,47,C_WHITE);
  d.fillRoundRect(5,65,230,40,5,C_PANEL);
  d.drawRoundRect(5,65,230,40,5,C_ORANGE);
  // Never replace characters with asterisks. Show up to 70 characters
  // in two readable rows; Fn+left/right pages through longer values.
  const size_t start=std::min(settingsTextOffset,editBuf.length());
  drawText(editBuf.substring(start,start+35),10,73,C_WHITE,C_PANEL);
  drawText(editBuf.substring(start+35,start+70),10,88,C_WHITE,C_PANEL);
  drawText(trimText(statusText,38),7,109,C_MUTED);
  drawFooter("ENTREE sauver | FN+DEL vider | ESC retour");
}
void drawConnectionDots(){
  auto &d=tft;

  d.fillCircle(180,11,3,netWifiUp.load()?C_GREEN:C_RED);
  d.fillCircle(201,11,3,netMqttUp.load()?C_GREEN:C_ORANGE);
  d.fillCircle(222,11,3,sdOK?C_GREEN:C_RED);

}
void drawUi(){
  auto &d=tft;
  const bool newPage=!uiInitialized || lastRenderedPage!=currentPage;

  if(newPage){
    d.fillScreen(C_BG);
    switch(currentPage){
      case Page::Home:drawTopBar("MQTT KVX / 10NOV");break;
      case Page::Chat:drawTopBar("MESSAGES");break;
      case Page::History:drawTopBar("HISTORIQUE");break;
      case Page::Detail:drawTopBar("LIRE MESSAGE");break;
      case Page::Network:drawTopBar("ETAT RESEAU");break;
      case Page::Settings:drawTopBar("REGLAGES");break;
    }
  }else if(currentPage!=Page::Home){
    // Small-area clear, never blank the entire display on text/status updates.
    d.fillRect(0,23,240,99,C_BG);
  }
  switch(currentPage){
    case Page::Home:drawHome();break;
    case Page::Chat:drawChat();break;
    case Page::History:drawHistory();break;
    case Page::Detail:drawDetail();break;
    case Page::Network:drawNetwork();break;
    case Page::Settings:drawSettings();break;
  }

  uiInitialized=true;
  lastRenderedPage=currentPage;
}
void notifyUi(const char* message,const char* ip=""){
  if(!netNoticeQueue)return;
  NetNotice n{};
  strncpy(n.text,message,sizeof(n.text)-1);
  // Include the current IP in every event so an overwritten event cannot hide it.
  const String currentIp=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String();
  strncpy(n.ip,ip[0]?ip:currentIp.c_str(),sizeof(n.ip)-1);
  // Only newest status matters; never stall TLS for a status indicator.
  xQueueOverwrite(netNoticeQueue,&n);
}
// All synchronous Wi-Fi / TLS / MQTT calls run here, away from the keyboard.
void runNetworkSession(){
  WiFiClientSecure tls;
  MQTTClient mqtt(768,768);
  tls.setHandshakeTimeout(6);
  uint32_t lastWifiAttempt=millis()-10001,lastMqttAttempt=millis()-8001;
  bool ntpStarted=false;
  uint32_t lastNtpAttempt=0;
  bool wasWifiUp=false;
  tls.setCACert(caPem.c_str());
  mqtt.begin(netCfg.host.c_str(),netCfg.port,tls);
  mqtt.onMessage(onIncoming);
  mqtt.setOptions(30,false,4000);
  while(!stopWorker.load()){
    const uint32_t now=millis();
    const bool wifiOk=WiFi.status()==WL_CONNECTED && static_cast<uint32_t>(WiFi.localIP())!=0;
    if(!wifiOk){
      netWifiUp.store(false);netMqttUp.store(false);
      if(wasWifiUp){
        wasWifiUp=false;ntpStarted=false;
        mqtt.disconnect(); // Tear down stale TLS session before next reconnection.
        tls.stop();
        notifyUi("Wi-Fi deconnecte");
      }
      if(now-lastWifiAttempt>10000){
        lastWifiAttempt=now;
        // Shared KVX Wi-Fi manager owns association / reconnect.
        // Never start a second Wi-Fi connection from MQTT.
        notifyUi("Wi-Fi KVX indisponible");
      }
      vTaskDelay(pdMS_TO_TICKS(60));continue;
    }
    netWifiUp.store(true);
    if(!wasWifiUp){
      wasWifiUp=true;
      const String ip=WiFi.localIP().toString();
      notifyUi("Wi-Fi connecte",ip.c_str());
    }
    if(time(nullptr)<1700000000){
      if(!ntpStarted||now-lastNtpAttempt>20000){
        configTime(0,0,"pool.ntp.org","time.google.com");
        ntpStarted=true;lastNtpAttempt=now;
        notifyUi("Horloge NTP: attente/reessai");
      }
      vTaskDelay(pdMS_TO_TICKS(100));continue;
    }
    if(!mqtt.connected()){
      netMqttUp.store(false);
      if(now-lastMqttAttempt>8000){
        lastMqttAttempt=now;
        notifyUi("Connexion MQTT TLS...");
        constexpr uint32_t internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
        Serial.printf("[MQTT TLS] debut libre=%u bloc=%u pile=%u\n",
                      (unsigned)heap_caps_get_free_size(internal),
                      (unsigned)heap_caps_get_largest_free_block(internal),
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        const String clientId=String("nmqtt-")+netCfg.id+"-"+String((uint32_t)ESP.getEfuseMac(),HEX);
        IPAddress brokerIp;
        if(!WiFi.hostByName(netCfg.host.c_str(),brokerIp)){
          notifyUi("MQTT: DNS broker introuvable");
        }
        else if(mqtt.connect(clientId.c_str(),netCfg.user.c_str(),netCfg.pass.c_str())){
          if(mqtt.subscribe(topicFor(netCfg.id),1)){
            netMqttUp.store(true);notifyUi("MQTT connecte");
          }else{
            notifyUi("Abonnement MQTT refuse");mqtt.disconnect();
          }
        }else {
          const int rc=int(mqtt.returnCode());
          const int err=int(mqtt.lastError());
          char tlsInfo[100]={};
          const int tlsCode=tls.lastError(tlsInfo,sizeof(tlsInfo));
          Serial.printf("[MQTT TLS] echec erreur=%d code=%d TLS=%d libre=%u bloc=%u\n",
                        err,rc,tlsCode,(unsigned)heap_caps_get_free_size(internal),
                        (unsigned)heap_caps_get_largest_free_block(internal));
          String diag=rc==4?"MQTT: identifiants refuses":
                      rc==5?"MQTT: autorisation refusee":
                      err==-3 && tlsCode!=0?
                        "MQTT TLS err="+String(tlsCode):
                      err==-3?"MQTT TCP/TLS: hote, port, CA":
                      String("MQTT e=")+String(err)+" rc="+String(rc);
          notifyUi(diag.c_str());
          // Reclaim TLS buffers after a failed handshake before the next retry.
          mqtt.disconnect();tls.stop();
        }
      }
      vTaskDelay(pdMS_TO_TICKS(50));continue;
    }
    netMqttUp.store(true);
    mqtt.loop();
    if(!mqtt.connected()){netMqttUp.store(false);notifyUi("Connexion MQTT perdue");}
    else{
      NetPacket outgoing{};
      if(xQueueReceive(netTxQueue,&outgoing,0)==pdPASS){
        const bool ok=mqtt.publish(topicFor(netCfg.peer),String(outgoing.payload),false,1);
        NetAck ack{ok};
        xQueueSend(netAckQueue,&ack,pdMS_TO_TICKS(200));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  mqtt.disconnect();tls.stop();
}
void networkTask(void*){
  runNetworkSession();
  netMqttUp.store(false);netWifiUp.store(false);
  // Session objects have been destroyed before the UI reclaims the queues.
  workerFinished.store(true);
  vTaskDelete(nullptr);
}
void serviceNetworkEvents(bool shuttingDown=false){
  if(netNoticeQueue){
    NetNotice notice{};
    if(xQueueReceive(netNoticeQueue,&notice,0)==pdPASS){
      if(notice.ip[0])netIp=notice.ip;
      setStatus(notice.text);
    }
  }
  if(netRxQueue){
    NetPacket incoming{};
    // Drain up to four incoming messages per UI frame to avoid keyboard starvation.
    for(int i=0;i<4&&xQueueReceive(netRxQueue,&incoming,0)==pdPASS;i++)
      processIncoming(String(incoming.payload));
  }
  if(netAckQueue){
    NetAck ack{};
    if(xQueueReceive(netAckQueue,&ack,0)==pdPASS){
      txWaiting=false;
      if(ack.ok){removeFirstOutboxLine();setStatus("Publication MQTT acquittee");}
      else setStatus("Envoi en attente");
    }
  }
  static bool oldWifi=false,oldMqtt=false;
  const bool wifi=netWifiUp.load(),mqttOk=netMqttUp.load();
  if(wifi!=oldWifi||mqttOk!=oldMqtt){
    oldWifi=wifi;oldMqtt=mqttOk;
    if(uiInitialized)drawConnectionDots();
    if(currentPage==Page::Network)lastDraw=0;
  }
  const unsigned lost=droppedPackets.exchange(0);
  if(lost)setStatus("Reception : "+String(lost)+" msg perdus (file pleine)");
  if(!shuttingDown)sendQueued();
}
void cleanupQueues(){
  if(!workerFinished.load()) return;
  if(netTxQueue){vQueueDelete(netTxQueue);netTxQueue=nullptr;}
  if(netAckQueue){vQueueDelete(netAckQueue);netAckQueue=nullptr;}
  if(netRxQueue){vQueueDelete(netRxQueue);netRxQueue=nullptr;}
  if(netNoticeQueue){vQueueDelete(netNoticeQueue);netNoticeQueue=nullptr;}
}
void appStart(){
  // Single KVX setup(), Wi-Fi service, display, keyboard and SD mount.
  cleanupQueues();
  currentPage=Page::Home;selectedTile=0;selectedHistory=0;scrollPos=0;
  recent.clear();txWaiting=false;statusText="Initialisation..."; // Keep unsent draft on reentry.
  uiInitialized=false;lastDraw=0;lastPhysicalKeyMask=0;
  // KVX mounts microSD lazily; without this call config.ini and ca.pem
  // silently fail even when the card is inserted.
  sdOK=setupSdCard() && sdcardMounted;
  if(sdOK){
    SD.mkdir("/mqtt");
    if(!SD.exists(CONFIG_FILE) && SD.exists("/mqtt/config.bak"))
      SD.rename("/mqtt/config.bak",CONFIG_FILE);
    if(!SD.exists(OUTBOX_FILE) && SD.exists(OUTBOX_BAK))
      SD.rename(OUTBOX_BAK,OUTBOX_FILE);
    if(SD.exists(OUTBOX_FILE)){SD.remove(OUTBOX_BAK);SD.remove(OUTBOX_TMP);}
    loadHistory();
  }
  configured=loadConfig();caPem=readCA();
  stopWorker.store(false);workerFinished.store(true);
  if(!sdOK)setStatus("MicroSD absente ou impossible a monter");
  else if(!configured){startSettings();setStatus("Configurer MQTT, ENTREE pour modifier");}
  else if(caPem.isEmpty())setStatus("Installer /mqtt/ca.pem (TLS)");
  else{
    tftReleaseFrameCanvas();
    netCfg={cfg.wifiSsid,cfg.wifiPass,cfg.host,cfg.user,cfg.pass,cfg.id,cfg.peer,cfg.port};
    netTxQueue=xQueueCreate(1,sizeof(NetPacket));
    netAckQueue=xQueueCreate(2,sizeof(NetAck));
    netRxQueue=xQueueCreate(4,sizeof(NetPacket));
    netNoticeQueue=xQueueCreate(1,sizeof(NetNotice));
    if(!netTxQueue||!netAckQueue||!netRxQueue||!netNoticeQueue)
      setStatus("Memoire insuffisante pour MQTT");
    else{
      workerFinished.store(false);
      BaseType_t ok=xTaskCreatePinnedToCore(networkTask,"kvx-mqtt-net",12288,nullptr,1,nullptr,0);
      if(ok!=pdPASS){workerFinished.store(true);setStatus("Echec creation tache MQTT");}
    }
  }
  drawUi();lastDraw=millis();
}
void appService(){
  // Reconfiguration is asynchronous so the interface never waits for TLS.
  if(pendingReconfigure && workerFinished.load()){
    pendingReconfigure=false;
    // Do not lose an unfinished chat draft or jump back to the main tiles
    // after saving the final configuration field.
    const String preservedDraft=draft;
    appStart();
    draft=preservedDraft;
    if(configured) {
      currentPage=Page::Network;
      setStatus("Reglages sauves. Etat de reconnexion ci-dessus");
    }
    lastDraw=0;
    return;
  }
  keyPress();serviceNetworkEvents();
  if(lastDraw==0){drawUi();lastDraw=millis();}
  vTaskDelay(pdMS_TO_TICKS(12));
}
void appStop(){
  stopWorker.store(true);
  if(!workerFinished.load()){
    tft.fillScreen(kvxConfig.bgColor);
    tft.setTextSize(1);
    tft.drawCentreString("MQTT : fermeture de la connexion",tftWidth/2,tftHeight/2,1);
  }
  while(!workerFinished.load()){
    serviceNetworkEvents(true);
    vTaskDelay(pdMS_TO_TICKS(15));
  }
  serviceNetworkEvents(true);
  cleanupQueues();
  netCfg={};caPem=String();recent.clear();recent.shrink_to_fit();
  pendingReconfigure=false;
}
} // namespace KvxMqtt

void kvxMqttApp(){
#if defined(HAS_KEYBOARD)
  EscPress=false;SelPress=false;NextPress=false;PrevPress=false;
  UpPress=false;DownPress=false;
  if(!KvxMqtt::workerFinished.load()){
    // On a rapid re-entry, leave KVX responsive while the old task exits.
    tft.fillScreen(kvxConfig.bgColor);
    tft.drawCentreString("MQTT: nettoyage en cours",tftWidth/2,tftHeight/2,1);
    return;
  }
  KvxMqtt::appStart();
  while(!returnToMenu && !forceHome){
    checkReboot();KvxMqtt::appService();
  }
  KvxMqtt::appStop();
  tft.fillScreen(kvxConfig.bgColor);
#else
  displayInfo("MQTT: clavier requis",true);
#endif
}
