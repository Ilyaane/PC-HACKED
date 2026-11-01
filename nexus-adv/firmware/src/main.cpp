/* NEXUS ADV v0.5.0 SOURCE - Cardputer ADV ONLY.
 * Requires M5Cardputer >=1.1.1, microSD for audio; zero PSRAM allocation.
 * Wi-Fi credentials and device token are configured on the keyboard and saved in NVS.
 * Not hardware-certified: see VERIFICATION.md before flashing.
 */
#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include "ca_roots.h"

namespace {
constexpr uint16_t COL_BG=0x0000, COL_PANEL=0x18C3, COL_ACCENT=0x07FF, COL_TEXT=0xFFFF, COL_MUTED=0xAD55, COL_WARN=0xFBE0;
constexpr int SD_SCK=40, SD_MISO=39, SD_MOSI=14, SD_CS=12;
constexpr uint32_t AUDIO_RATE=16000;
constexpr size_t AUDIO_FRAME=1024, AUDIO_FRAMES=48; // 3.072 seconds, 98,348-byte WAV
constexpr size_t MAX_VOICE=120000;
constexpr char TX_AUDIO[]="/nexus_tx.wav", RX_AUDIO[]="/nexus_rx.wav", TMP_RX[]="/nexus_rx.tmp", RX_BACKUP[]="/nexus_rx.bak";
constexpr char HISTORY[]="/nexus_history.txt";

Preferences prefs;
String wifiSsid, wifiPass, serverUrl, deviceToken, recipient="bob", editor, statusLine="Initialisation";
String pendingTextId, pendingVoiceId;
String lastSentInfo="";
uint32_t cursor=0, lastVoiceId=0, lastCheck=0, lastWifiAttempt=0;
bool sdReady=false, netReady=false, serverReady=false;
enum class View { HOME, WIFI_CONFIRM, WIFI_LIST, WIFI_PASSWORD, CONFIG, EDIT_URL, EDIT_TOKEN, EDIT_PEER, COMPOSE, HISTORY, SENT, DIAG, DELETE_CONFIRM, DISCARD_VOICE };
View view=View::HOME;
String input;
String scanNames[24];
bool scanSaved[24]={}, scanOpen[24]={};
uint8_t homeIndex=0, configIndex=0, wifiIndex=0, confirmIndex=0;
constexpr uint8_t HOME_COUNT=11, CONFIG_COUNT=4, WIFI_SLOTS=5;
uint8_t scanCount=0, scanPage=0, pageHistory=0;
uint8_t textPage=0;
struct History {uint32_t id=0; String sender,kind,body;};
History history[8];
uint8_t historySize=0;

String masked(size_t count){String s;for(size_t i=0;i<count && i<40;i++)s+='*';return s;}
String clipped(String s,size_t max){if(s.length()>max)return s.substring(0,max-2)+"..";return s;}
void persistDraft(){ prefs.putString("draft",editor); }
void setStatus(const String& msg){ statusLine=clipped(msg,60);Serial.println("NEXUS: "+statusLine); }
void drawLines(const String& title,const String& a="",const String& b="",const String& c="",const String& d="",const String& e=""){
  auto& l=M5Cardputer.Display;
  l.fillScreen(COL_BG);l.fillRect(0,0,240,21,COL_PANEL);
  l.setTextColor(COL_ACCENT,COL_PANEL);l.setTextSize(1);
  l.setCursor(7,7);l.print(clipped(title,27));
  l.setTextColor(WiFi.status()==WL_CONNECTED?COL_ACCENT:COL_WARN,COL_PANEL);
  l.setCursor(198,7);l.print(WiFi.status()==WL_CONNECTED?"WiFi":"OFF");
  l.setTextColor(COL_TEXT,COL_BG);
  const String lines[]={a,b,c,d,e};
  for(int i=0;i<5;++i){l.setCursor(7,26+i*17);l.print(clipped(lines[i],37));}
  l.drawFastHLine(0,114,240,COL_PANEL);
  l.setTextColor(COL_MUTED,COL_BG);l.setCursor(5,120);l.print(clipped(statusLine,37));
}
void menuFrame(const String& title){
  auto& l=M5Cardputer.Display;
  l.fillScreen(COL_BG);
  l.fillRoundRect(2,2,236,20,5,COL_PANEL);
  l.fillRoundRect(7,6,11,11,3,COL_ACCENT);
  l.setTextColor(COL_BG,COL_ACCENT);
  l.setTextSize(1);l.setCursor(9,8);l.print("N");
  l.setTextColor(COL_TEXT,COL_PANEL);
  l.setCursor(24,9);l.print(clipped(title,25));
  l.fillCircle(222,12,3,WiFi.status()==WL_CONNECTED?COL_ACCENT:COL_WARN);
}
void drawMenu(const String& title,const String opts[],int count,int selected,
              const String& hint="Fn+; ^  Fn+. v   Entree OK"){
  auto& l=M5Cardputer.Display; menuFrame(title);
  const int start=(selected/5)*5;
  for(int r=0;r<5;r++){
    const int idx=start+r;
    if(idx>=count)break;
    const int y=26+r*17;
    if(idx==selected){
      l.fillRoundRect(4,y-1,232,17,5,COL_ACCENT);
      l.fillTriangle(8,y+3,8,y+11,13,y+7,COL_BG);
      l.setTextColor(COL_BG,COL_ACCENT);
    }else{
      l.fillRoundRect(4,y-1,232,17,5,COL_PANEL);
      l.setTextColor(COL_TEXT,COL_PANEL);
    }
    l.setTextSize(1);l.setCursor(18,y+4);l.print(clipped(opts[idx],35));
  }
  l.drawFastHLine(3,114,234,COL_PANEL);
  l.fillRect(0,115,240,20,COL_BG);
  l.setTextColor(COL_MUTED,COL_BG);l.setCursor(5,119);l.print(clipped(statusLine,30));
  l.setCursor(202,119);l.print("^v OK");
}
void paint(){
 switch(view){
 case View::HOME: {
   const String opts[]={"Messages recus","Ecrire un message","Messages envoyes","Enregistrer un vocal",
     "Envoyer le vocal","Ecouter un vocal","Reseaux Wi-Fi","Parametres NEXUS",
     "Diagnostics","Detecter microSD","Effacer le vocal"};
   drawMenu("NEXUS   MESSAGERIE",opts,HOME_COUNT,homeIndex);break;}
 case View::WIFI_CONFIRM: {
   const String opts[]={"Connecter : "+clipped(wifiSsid,23),"Choisir un autre Wi-Fi","Oublier ce reseau"};
   drawMenu("WI-FI MEMORISE",opts,3,confirmIndex);break;}
 case View::WIFI_LIST: {
   if(scanCount==0){drawLines("RESEAUX WI-FI","Aucun reseau trouve","Fn+ESC : retour");break;}
   String opts[24];
   for(int i=0;i<scanCount;i++)
     opts[i]=String(scanSaved[i]?"[MEM] ":scanOpen[i]?"[LIB] ":"[WPA] ")+clipped(scanNames[i],28);
   drawMenu("RESEAUX  "+String(wifiIndex+1)+"/"+String(scanCount),opts,scanCount,wifiIndex,
      "MEM=sauve  Fn+haut/bas Entree");break;}
 case View::WIFI_PASSWORD: {
   const String shown=input.length()>62?input.substring(input.length()-62):input;
   drawLines("MOT DE PASSE WI-FI","Reseau: "+clipped(wifiSsid,23),
       shown.substring(0,31),shown.length()>31?shown.substring(31):"",
       "Entree : connecter et sauver","Fn+ESC : retour");break;}
 case View::CONFIG: {
   const String opts[]={"Adresse HTTPS du serveur","Jeton de cet appareil",
     "Correspondant : "+recipient,"Reseaux Wi-Fi"};
   drawMenu("REGLAGES NEXUS",opts,CONFIG_COUNT,configIndex);break;}
 case View::EDIT_URL: {
   const String shown=input.length()>68?input.substring(input.length()-68):input;
   drawLines("ADRESSE HTTPS",shown.substring(0,34),shown.length()>34?shown.substring(34):"",
      "Entree : enregistrer","Fn+ESC : annuler");break;}
 case View::EDIT_TOKEN:drawLines("JETON PERSONNEL","Minimum 12 caracteres",
   masked(input.length()),"Entree : enregistrer","Fn+ESC : annuler");break;
 case View::EDIT_PEER:drawLines("DESTINATAIRE","alice ou bob",input,
   "Entree : enregistrer","Fn+ESC : annuler");break;
 case View::COMPOSE: {
   String tail=editor.length()>66?editor.substring(editor.length()-66):editor;
   drawLines("ECRIRE UN MESSAGE", "Vers: "+recipient,clipped(tail,33),tail.length()>33?clipped(tail.substring(33),33):"", "ENTREE: envoyer", "ESC: menu principal");break;}
 case View::HISTORY: {
   if(!historySize){drawLines("RECEPTION", "Aucun message en memoire", "P: lire dernier vocal", "ESC: retour");break;}
   int i=(int)historySize-1-(int)pageHistory;
   if(i<0)i=0;
   if(history[i].kind=="voice") {
     drawLines("VOCAL "+String(i+1)+"/"+String(historySize), "De: "+history[i].sender,
       "Audio #"+String(history[i].id), "P: telecharger / ecouter", "Haut/Bas: autres messages", "ESC: accueil");
   } else {
     const String& body=history[i].body;
     const uint8_t pages=max(1,(int)((body.length()+92)/93));
     if(textPage>=pages)textPage=pages-1;
     const String part=body.substring(textPage*93,(textPage+1)*93);
     drawLines("TEXTE "+String(i+1)+"/"+String(historySize)+" p"+String(textPage+1)+"/"+String(pages),
       "De: "+history[i].sender,
       part.substring(0,31),part.length()>31?part.substring(31,62):"",
       part.length()>62?part.substring(62,93):"", "N: suite B: avant Haut/Bas: msg");
   }
   break;}
 case View::SENT:drawLines("DERNIER ENVOI",clipped(lastSentInfo,33),"ENTREE: actualiser", "ESC: retour");break;
 case View::DIAG: drawLines("DIAGNOSTIC", "Wi-Fi: "+String(WiFi.status()==WL_CONNECTED?"connecte":"hors ligne"),
    "Serveur: "+String(serverReady?"OK":"non verifie"),"IP: "+WiFi.localIP().toString(),
    "Heap: "+String(ESP.getFreeHeap())+" B",String(sdReady?"microSD: OK":"microSD: absente"));break;
 case View::DELETE_CONFIRM:drawLines("EFFACER WI-FI?", "ENTREE: oublier le reseau", "ESC: ne rien effacer");break;
 case View::DISCARD_VOICE:drawLines("JETER BROUILLON VOCAL?", "ENTREE: supprimer le WAV", "ESC: le conserver");break;
 }
}
void changeView(View v){view=v;paint();}
String wifiKey(int slot,char type){return String("w")+type+String(slot);}
int wifiSlot(const String& ssid){
  if(ssid.isEmpty())return -1;
  for(int i=0;i<WIFI_SLOTS;i++)
    if(prefs.getString(wifiKey(i,'s').c_str(),"")==ssid)return i;
  return -1;
}
void rememberNetwork(const String& ssid,const String& pass){
  if(ssid.isEmpty())return;
  int slot=wifiSlot(ssid);
  if(slot<0){
    slot=-1;
    for(int i=0;i<WIFI_SLOTS;i++)
      if(prefs.getString(wifiKey(i,'s').c_str(),"").isEmpty()){slot=i;break;}
    if(slot<0)slot=0; // bounded replacement when all five slots are used
  }
  prefs.putString(wifiKey(slot,'s').c_str(),ssid);
  prefs.putString(wifiKey(slot,'p').c_str(),pass);
}
void forgetNetwork(const String& ssid){
  const int slot=wifiSlot(ssid);
  if(slot>=0){prefs.remove(wifiKey(slot,'s').c_str());prefs.remove(wifiKey(slot,'p').c_str());}
}

String nonce(){char buf[25]; snprintf(buf,sizeof(buf),"%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());return String(buf);}
void loadSettings(){
  prefs.begin("nexusadv",false);
  wifiSsid=prefs.getString("ssid",""); wifiPass=prefs.getString("pwd","");
  if(!wifiSsid.isEmpty() && wifiSlot(wifiSsid)<0)rememberNetwork(wifiSsid,wifiPass); // migrate old NVS
  serverUrl=prefs.getString("server",""); deviceToken=prefs.getString("token","");
  recipient=prefs.getString("peer","bob"); editor=prefs.getString("draft","");
  pendingTextId=prefs.getString("txtid",""); pendingVoiceId=prefs.getString("vocid","");
  cursor=prefs.getUInt("cursor",0);lastVoiceId=prefs.getUInt("voice",0);
}
void loadHistory(){
  if(!sdReady)return;
  File f=SD.open(HISTORY,FILE_READ);if(!f)return;
  while(f.available()){
    String line=f.readStringUntil('\n');line.trim();if(line.isEmpty())continue;
    int a=line.indexOf('|'),b=line.indexOf('|',a+1),c=line.indexOf('|',b+1);
    if(a<=0||b<=a||c<=b)continue;
    if(historySize==8){for(int k=1;k<8;k++)history[k-1]=history[k];historySize--;}
    auto& h=history[historySize++];h.id=line.substring(0,a).toInt();h.sender=line.substring(a+1,b);h.kind=line.substring(b+1,c);h.body=line.substring(c+1);
  }f.close();
}
bool remember(uint32_t id,String sender,String kind,String text){
  text.replace("\n"," ");text.replace("\r"," ");text.replace("|","/");
  if(historySize==8){for(int i=1;i<8;i++)history[i-1]=history[i];historySize--;}
  History& h=history[historySize++];h.id=id;h.sender=sender;h.kind=kind;h.body=text;
  if(!sdReady)return true; // RAM-only session: do not persist a cursor on flash.
  File f=SD.open(HISTORY,FILE_APPEND);
  if(!f) return false;
  String line=String(id)+"|"+sender+"|"+kind+"|"+text+"\n";
  const size_t written=f.print(line);
  f.flush();f.close();
  return written==line.length();
}
void initSd(){
  SPI.begin(SD_SCK,SD_MISO,SD_MOSI,SD_CS);
  sdReady=SD.begin(SD_CS,SPI,20000000);if(sdReady)loadHistory();
}
bool timeReady(){
  if(time(nullptr)>1700000000)return true;
  configTime(0,0,"pool.ntp.org","time.google.com");
  for(int i=0;i<50&&time(nullptr)<1700000000;i++)delay(100);
  return time(nullptr)>1700000000;
}
bool connectSaved(){
  if(wifiSsid.isEmpty()) {setStatus("Aucun Wi-Fi memorise");netReady=false;return false;}
  WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());
  setStatus("Connexion Wi-Fi...");paint();
  for(int i=0;i<50&&WiFi.status()!=WL_CONNECTED;i++)delay(200);
  netReady=WiFi.status()==WL_CONNECTED;
  if(!netReady){serverReady=false;setStatus("Connexion echouee (W)");return false;}
  setStatus("Wi-Fi connecte / test TLS");
  timeReady();return true;
}
bool connectRememberedWiFi(){
  const String previousSsid=wifiSsid,previousPass=wifiPass;
  if(!wifiSsid.isEmpty() && connectSaved())return true;
  for(int slot=0;slot<WIFI_SLOTS;slot++){
    const String ssid=prefs.getString(wifiKey(slot,'s').c_str(),"");
    if(ssid.isEmpty()||ssid==previousSsid)continue;
    wifiSsid=ssid;wifiPass=prefs.getString(wifiKey(slot,'p').c_str(),"");
    if(connectSaved()){
      prefs.putString("ssid",wifiSsid);prefs.putString("pwd",wifiPass);
      return true;
    }
  }
  wifiSsid=previousSsid;wifiPass=previousPass;
  return false;
}
bool configured(){return serverUrl.startsWith("https://") && !serverUrl.endsWith("/") && deviceToken.length()>=12;}

bool openHttp(HTTPClient& http, WiFiClientSecure& client,const String& path){
  if(WiFi.status()!=WL_CONNECTED){setStatus("Wi-Fi deconnecte");return false;}
  if(!configured()){setStatus("Regler serveur et jeton");return false;}
  if(!timeReady()){setStatus("Horloge non synchronisee");return false;}
  client.setCACert(NEXUS_TRUSTED_ROOTS);
  client.setTimeout(10000);
  http.setTimeout(10000);http.setConnectTimeout(7500);
  if(!http.begin(client,serverUrl+path)){setStatus("Erreur ouverture HTTPS");return false;}
  http.addHeader("Authorization","Bearer "+deviceToken);
  return true;
}
bool serverTest(){
  if(!configured()) {serverReady=false;setStatus("Configurer serveur et jeton");return false;}
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/session")){serverReady=false;return false;}
  int code=http.GET();String response=code==200?http.getString():"";
  http.end();
  JsonDocument doc;
  serverReady=(code==200 && !deserializeJson(doc,response) && doc["ok"]==true);
  setStatus(serverReady?"Serveur NEXUS OK":("Serveur inaccessible: "+String(code)));
  return serverReady;
}
void scanWifi(){
  changeView(View::WIFI_LIST);setStatus("Recherche Wi-Fi...");paint();
  int n=WiFi.scanNetworks(false,true);scanCount=0;scanPage=0;wifiIndex=0;
  for(int i=0;i<n && scanCount<24;i++){
    String name=WiFi.SSID(i);if(name.isEmpty())continue;
    bool duplicate=false;for(int j=0;j<scanCount;j++)if(scanNames[j]==name)duplicate=true;
    if(!duplicate){scanNames[scanCount]=name;scanSaved[scanCount]=wifiSlot(name)>=0;
      scanOpen[scanCount]=(WiFi.encryptionType(i)==WIFI_AUTH_OPEN);scanCount++;}
  }
  WiFi.scanDelete();setStatus(scanCount?"Fleches : selection / Entree : OK":"Aucun reseau trouve");paint();
}
void wifiSetup(){
  if(wifiSsid.isEmpty())scanWifi();else{confirmIndex=0;changeView(View::WIFI_CONFIRM);}
}
void storeWiFiAfterTest(const String& newPass);
void chooseWiFi(int selected){
  if(selected<0 || selected>=scanCount)return;
  wifiSsid=scanNames[selected];const int saved=wifiSlot(wifiSsid);
  if(saved>=0){
    const String pass=prefs.getString(wifiKey(saved,'p').c_str(),"");
    storeWiFiAfterTest(pass);return;
  }
  if(scanOpen[selected]){storeWiFiAfterTest("");return;}
  input="";changeView(View::WIFI_PASSWORD);
}
void storeWiFiAfterTest(const String& newPass){
  String ssid=wifiSsid,pass=newPass;
  const String oldSsid=prefs.getString("ssid","");
  const String oldPass=prefs.getString("pwd","");
  wifiPass=pass;
  if(connectSaved()){
    prefs.putString("ssid",ssid);prefs.putString("pwd",pass);rememberNetwork(ssid,pass);
    const bool hasServer=serverTest();
    setStatus(hasServer ? "Wi-Fi et serveur OK" : "Wi-Fi OK / serveur indisponible");
    changeView(View::HOME);
  }else{
    wifiSsid=oldSsid;wifiPass=oldPass;
    if(!oldSsid.isEmpty()){WiFi.disconnect();WiFi.begin(oldSsid.c_str(),oldPass.c_str());}
    setStatus("Echec: ancien reseau conserve");
    if(oldSsid.isEmpty())scanWifi();else changeView(View::WIFI_CONFIRM);
  }
}

void pollInbox(){
  if(!configured()||WiFi.status()!=WL_CONNECTED)return;
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/messages?after="+String(cursor)))return;
  int code=http.GET();if(code!=200){http.end();setStatus("Reception HTTP "+String(code));serverReady=false;return;}
  String payload=http.getString();http.end();
  JsonDocument doc;
  if(deserializeJson(doc,payload)){setStatus("Reponse invalide");return;}
  JsonArray messages=doc["messages"].as<JsonArray>();
  for(JsonObject m:messages){
    uint32_t id=m["id"]|0u;if(id<=cursor)continue;
    String sender=m["sender"]|"?",kind=m["kind"]|"?";
    String text=kind=="voice"?"Message vocal #"+String(id):m["text"].as<String>();
    if(!remember(id,sender,kind,text)) {
      setStatus("microSD: historique non sauvegarde");
      break; // Do not advance persistent cursor and silently lose a message.
    }
    if(kind=="voice"){lastVoiceId=id;prefs.putUInt("voice",id);}
    cursor=id;
    // Without microSD, keep cursor volatile: missed items are replayed on reboot.
    if(sdReady) prefs.putUInt("cursor",cursor);
    setStatus("Nouveau message de "+sender);
  }
}
void refreshSent(){
  if(!configured()) {setStatus("Configurer serveur et jeton");paint();return;}
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/outbox?after=0")){paint();return;}
  const int code=http.GET();
  String body=code==200 ? http.getString() : "";
  http.end();
  if(code!=200){setStatus("Boite envoi HTTP "+String(code));paint();return;}
  JsonDocument doc;
  if(deserializeJson(doc,body)){setStatus("Reponse serveur invalide");paint();return;}
  JsonArray list=doc["messages"].as<JsonArray>();
  if(list.size()==0){lastSentInfo="Aucun message envoye";}
  else {
    JsonObject last=list[0].as<JsonObject>();
    String peer=last["recipient"]|"?",kind=last["kind"]|"?";
    bool read=(last["read"]|0)==1;
    lastSentInfo=String("Vers ")+peer+" / "+kind+(read?" / LU":" / non lu");
  }
  paint();
}
void acknowledgeMessage(uint32_t id){
  if(!id || !configured() || WiFi.status()!=WL_CONNECTED)return;
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/messages/"+String(id)+"/read"))return;
  const int result=http.POST((uint8_t*)nullptr,0);
  http.end();
  if(result!=200)setStatus("Accuse lecture non transmis");
}
void sendText(){
  editor.trim();if(editor.isEmpty())return;
  if(!pendingTextId.length()){pendingTextId=nonce();prefs.putString("txtid",pendingTextId);}
  JsonDocument doc;doc["to"]=recipient;doc["text"]=editor;doc["client_id"]=pendingTextId;
  String body;serializeJson(doc,body);WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/messages"))return;
  http.addHeader("Content-Type","application/json");
  int code=http.POST((uint8_t*)body.c_str(),body.length());http.end();
  if(code>=200&&code<300){editor="";prefs.putString("draft","");pendingTextId="";prefs.remove("txtid");setStatus("Texte envoye");changeView(View::HOME);}
  else{setStatus("Envoi refuse: "+String(code));paint();}
}
void writeLE16(File& f,uint16_t v){uint8_t a[2]={(uint8_t)v,(uint8_t)(v>>8)};f.write(a,2);}
void writeLE32(File& f,uint32_t v){uint8_t a[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)};f.write(a,4);}
void writeWavHeader(File& f,uint32_t samples){
  uint32_t bytes=samples*2;
  f.write((const uint8_t*)"RIFF",4);writeLE32(f,bytes+36);f.write((const uint8_t*)"WAVEfmt ",8);
  writeLE32(f,16);writeLE16(f,1);writeLE16(f,1);writeLE32(f,AUDIO_RATE);
  writeLE32(f,AUDIO_RATE*2);writeLE16(f,2);writeLE16(f,16);f.write((const uint8_t*)"data",4);writeLE32(f,bytes);
}
void recordVoice(){
  if(pendingVoiceId.length() || (sdReady && SD.exists(TX_AUDIO))){
    setStatus("Vocal deja present: U envoyer, Z effacer");paint();return;
  }
  if(!sdReady){setStatus("Inserer une microSD (vocal)");paint();return;}
  SD.remove(TX_AUDIO); // Ensure a new recording never appends to a previous WAV.
  File f=SD.open(TX_AUDIO,FILE_WRITE);
  if(!f){setStatus("Erreur creation WAV");paint();return;}
  f.seek(0);writeWavHeader(f,AUDIO_FRAME*AUDIO_FRAMES);
  if(f.position()!=44){f.close();SD.remove(TX_AUDIO);setStatus("Entete WAV impossible");paint();return;}
  M5Cardputer.Speaker.end();
  if(!M5Cardputer.Mic.begin()){f.close();SD.remove(TX_AUDIO);setStatus("Micro non initialise");paint();return;}
  delay(1200); // ES8311 needs codec warm-up after Mic.begin.
  setStatus("REC 3 secondes (micro actif)");paint();
  bool ok=true;uint32_t total=0;
  int16_t audio[AUDIO_FRAME]; // 2 KB stack; never requires PSRAM
  for(size_t i=0;i<AUDIO_FRAMES;i++){
    if(!M5Cardputer.Mic.record(audio,AUDIO_FRAME,AUDIO_RATE,false)){ok=false;break;}
    uint32_t t=millis();while(M5Cardputer.Mic.isRecording() && millis()-t<1800)delay(1);
    if(M5Cardputer.Mic.isRecording()){ok=false;break;}
    if(f.write((uint8_t*)audio,sizeof(audio))!=sizeof(audio)){ok=false;break;}
    total+=AUDIO_FRAME;
  }
  M5Cardputer.Mic.end();f.close();
  if(!ok||total!=AUDIO_FRAME*AUDIO_FRAMES){SD.remove(TX_AUDIO);setStatus("Echec enregistrement");paint();return;}
  File verify=SD.open(TX_AUDIO,FILE_READ);
  const bool exact=verify && verify.size()==44+2*total;
  if(verify)verify.close();
  if(!exact){SD.remove(TX_AUDIO);setStatus("WAV incomplet sur microSD");paint();return;}
  pendingVoiceId=nonce();prefs.putString("vocid",pendingVoiceId);
  setStatus("Vocal pret: U pour envoyer");paint();
}
void uploadVoice(){
  if(!sdReady){setStatus("MicroSD requise");paint();return;}
  File f=SD.open(TX_AUDIO,FILE_READ);if(!f){setStatus("Enregistrer d'abord (V)");paint();return;}
  if(f.size()<44||f.size()>MAX_VOICE){f.close();setStatus("WAV trop volumineux");paint();return;}
  if(pendingVoiceId.isEmpty()){pendingVoiceId=nonce();prefs.putString("vocid",pendingVoiceId);}
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/voice/"+recipient)){f.close();return;}
  http.addHeader("Content-Type","audio/wav");http.addHeader("X-Client-Id",pendingVoiceId);
  setStatus("Envoi vocal en cours...");paint();
  int code=http.sendRequest("POST",&f,f.size());http.end();f.close();
  if(code>=200&&code<300){
    // Confirmed upload: do not allow the same WAV to be sent again with a fresh id.
    pendingVoiceId="";prefs.remove("vocid");
    const bool deleted=SD.remove(TX_AUDIO);
    setStatus(deleted?"Vocal envoye":"Vocal envoye (effacer WAV: Z)");
  }
  else setStatus("Vocal en attente (U): "+String(code));
  paint();
}
void downloadVoice(){
  if(!sdReady||!lastVoiceId){setStatus("Pas de microSD ou vocal");paint();return;}
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/voice/"+String(lastVoiceId)))return;
  int code=http.GET();int length=http.getSize();
  if(code!=200||length<44||length>(int)MAX_VOICE){http.end();setStatus("Vocal indisponible: "+String(code));paint();return;}
  SD.remove(TMP_RX);File f=SD.open(TMP_RX,FILE_WRITE);
  if(!f){http.end();setStatus("MicroSD non accessible");paint();return;}
  auto* stream=http.getStreamPtr();uint8_t block[1024];int got=0;
  uint32_t last=millis();
  while(got<length && millis()-last<12000){
    size_t available=stream->available();
    if(available){size_t want=min((int)sizeof(block),min((int)available,length-got));
      int n=stream->readBytes(block,want);
      if(n<=0||f.write(block,n)!=(size_t)n)break;
      got+=n;last=millis();
    }else delay(5);
  }
  f.close();http.end();
  if(got!=length){SD.remove(TMP_RX);setStatus("Telechargement incomplet");paint();return;}
  File check=SD.open(TMP_RX,FILE_READ);uint8_t h[44];
  bool valid=check&&check.size()==(size_t)length&&check.read(h,44)==44&&
    memcmp(h,"RIFF",4)==0&&memcmp(h+8,"WAVE",4)==0&&memcmp(h+12,"fmt ",4)==0&&
    memcmp(h+36,"data",4)==0&&h[20]==1&&h[21]==0&&h[22]==1&&h[23]==0&&
    h[24]==0x80&&h[25]==0x3e&&h[26]==0&&h[27]==0&&h[34]==16&&h[35]==0&&
    ((uint32_t)h[4]|((uint32_t)h[5]<<8)|((uint32_t)h[6]<<16)|((uint32_t)h[7]<<24))==(uint32_t)length-8&&
    ((uint32_t)h[40]|((uint32_t)h[41]<<8)|((uint32_t)h[42]<<16)|((uint32_t)h[43]<<24))==(uint32_t)length-44;
  if(check)check.close();
  if(!valid){SD.remove(TMP_RX);setStatus("WAV invalide");paint();return;}
  // Recover a previous interrupted replacement before touching backup.
  if(!SD.exists(RX_AUDIO) && SD.exists(RX_BACKUP)) {
    if(!SD.rename(RX_BACKUP,RX_AUDIO)) {
      SD.remove(TMP_RX);setStatus("Recuperation ancien WAV echouee");paint();return;
    }
  }
  // Transactional replacement: preserve the previous playable WAV on rename failure.
  if(SD.exists(RX_AUDIO)) SD.remove(RX_BACKUP);
  const bool oldExists=SD.exists(RX_AUDIO);
  if(oldExists && !SD.rename(RX_AUDIO,RX_BACKUP)){
    SD.remove(TMP_RX);setStatus("Sauvegarde ancien vocal echouee");paint();return;
  }
  if(!SD.rename(TMP_RX,RX_AUDIO)){
    if(oldExists) SD.rename(RX_BACKUP,RX_AUDIO);
    SD.remove(TMP_RX);setStatus("Remplacement WAV echoue");paint();return;
  }
  if(oldExists) SD.remove(RX_BACKUP);
  prefs.putUInt("rx_saved",lastVoiceId);setStatus("Vocal telecharge: lecture");paint();
}
void playVoice(){
  if(!sdReady){setStatus("MicroSD absente");paint();return;}
  if(lastVoiceId && prefs.getUInt("rx_saved",0)!=lastVoiceId){
    downloadVoice();
    if(prefs.getUInt("rx_saved",0)!=lastVoiceId){setStatus("Nouveau vocal non telecharge");paint();return;}
  }
  File f=SD.open(RX_AUDIO,FILE_READ);
  if(!f && lastVoiceId){downloadVoice();f=SD.open(RX_AUDIO,FILE_READ);}
  if(!f){setStatus("Aucun vocal telecharge");paint();return;}
  uint8_t header[44];if(f.read(header,44)!=44 || memcmp(header,"RIFF",4)||memcmp(header+8,"WAVE",4)||memcmp(header+12,"fmt ",4)||memcmp(header+36,"data",4)){
    f.close();setStatus("Audio corrompu");paint();return;
  }
  M5Cardputer.Mic.end();M5Cardputer.Speaker.setVolume(95);
  if(!M5Cardputer.Speaker.begin()){f.close();setStatus("Haut-parleur indisponible");paint();return;}
  setStatus("Lecture du message vocal");paint();
  alignas(4) int16_t samples[1024];bool ok=true;
  while(f.available()){
    size_t bytes=f.read((uint8_t*)samples,sizeof(samples));bytes&=~(size_t)1;
    if(!bytes)break;
    if(!M5Cardputer.Speaker.playRaw(samples,bytes/2,AUDIO_RATE,false,1,0)){ok=false;break;}
    uint32_t t=millis();while(M5Cardputer.Speaker.isPlaying()&&millis()-t<1300)delay(2);
    if(M5Cardputer.Speaker.isPlaying()){ok=false;M5Cardputer.Speaker.stop();break;}
  }
  f.close();M5Cardputer.Speaker.end();setStatus(ok?"Lecture terminee":"Erreur de lecture");paint();
}
void openHomeSelection(){
  switch(homeIndex){
    case 0:pageHistory=0;textPage=0;pollInbox();changeView(View::HISTORY);
      if(historySize)acknowledgeMessage(history[historySize-1].id);break;
    case 1:changeView(View::COMPOSE);break;
    case 2:changeView(View::SENT);refreshSent();break;
    case 3:recordVoice();break;
    case 4:uploadVoice();break;
    case 5:playVoice();break;
    case 6:wifiSetup();break;
    case 7:configIndex=0;changeView(View::CONFIG);break;
    case 8:changeView(View::DIAG);break;
    case 9:if(!sdReady){initSd();setStatus(sdReady?"microSD reconnue":"microSD absente");}paint();break;
    case 10:changeView(View::DISCARD_VOICE);break;
  }
}
void handleInput(){
  // Library v1.1.1 uses Fn+;/. for arrows and Fn+backtick for Esc.
  // isChange() only checks the number of pressed keys, so track whole chord instead.
  static String lastChord="";
  if(!M5Cardputer.Keyboard.isPressed()){lastChord="";return;}
  const auto k=M5Cardputer.Keyboard.keysState();
  const bool escPressed=k.fn && M5Cardputer.Keyboard.isKeyPressed('`');
  const bool upPressed=k.fn && M5Cardputer.Keyboard.isKeyPressed(';');
  const bool downPressed=k.fn && M5Cardputer.Keyboard.isKeyPressed('.');
  const bool leftPressed=k.fn && M5Cardputer.Keyboard.isKeyPressed(',');
  const bool rightPressed=k.fn && M5Cardputer.Keyboard.isKeyPressed('/');
  String chord=String(k.fn?"F":"f")+String(k.shift?"S":"s")+
    String(k.enter?"E":"")+String(k.del?"D":"");
  for(char ch:k.word)chord+=ch;
  if(chord==lastChord)return;
  lastChord=chord;
  if(escPressed || (leftPressed && view!=View::HISTORY)){
    if(view==View::WIFI_PASSWORD || view==View::WIFI_LIST || view==View::WIFI_CONFIRM){
      wifiSsid=prefs.getString("ssid","");wifiPass=prefs.getString("pwd","");
      changeView(View::HOME);
    }else if(view==View::EDIT_URL || view==View::EDIT_TOKEN || view==View::EDIT_PEER){
      input="";changeView(View::CONFIG);
    }else if(view==View::DELETE_CONFIRM){changeView(View::WIFI_CONFIRM);}
    else{changeView(View::HOME);}
    return;
  }
  const bool choose=k.enter||rightPressed;
  if(view==View::HOME){
    if(upPressed)homeIndex=(homeIndex+HOME_COUNT-1)%HOME_COUNT;
    if(downPressed)homeIndex=(homeIndex+1)%HOME_COUNT;
    if(choose){openHomeSelection();return;}
    paint();return;
  }
  if(view==View::CONFIG){
    if(upPressed)configIndex=(configIndex+CONFIG_COUNT-1)%CONFIG_COUNT;
    if(downPressed)configIndex=(configIndex+1)%CONFIG_COUNT;
    if(choose){
      switch(configIndex){
        case 0:input=serverUrl;changeView(View::EDIT_URL);return;
        case 1:input="";changeView(View::EDIT_TOKEN);return;
        case 2:input=recipient;changeView(View::EDIT_PEER);return;
        case 3:wifiSetup();return;
      }
    }
    paint();return;
  }
  if(view==View::WIFI_CONFIRM){
    if(upPressed)confirmIndex=(confirmIndex+2)%3;
    if(downPressed)confirmIndex=(confirmIndex+1)%3;
    if(choose){
      if(confirmIndex==0){
        if(connectSaved())serverTest();changeView(View::HOME);return;
      }
      if(confirmIndex==1){scanWifi();return;}
      if(confirmIndex==2){changeView(View::DELETE_CONFIRM);return;}
    }
    paint();return;
  }
  if(view==View::WIFI_LIST){
    if(scanCount){
      if(upPressed)wifiIndex=(wifiIndex+scanCount-1)%scanCount;
      if(downPressed)wifiIndex=(wifiIndex+1)%scanCount;
      if(choose){chooseWiFi(wifiIndex);return;}
    }
    paint();return;
  }
  if(view==View::DELETE_CONFIRM){
    if(choose){
      forgetNetwork(wifiSsid);prefs.remove("ssid");prefs.remove("pwd");
      wifiSsid="";wifiPass="";WiFi.disconnect();
      serverReady=false;setStatus("Wi-Fi oublie");scanWifi();
    }
    return;
  }
  if(view==View::DISCARD_VOICE){
    if(choose){pendingVoiceId="";prefs.remove("vocid");
      if(sdReady)SD.remove(TX_AUDIO);
      setStatus("Brouillon vocal efface");changeView(View::HOME);}
    return;
  }
  if(view==View::WIFI_PASSWORD||view==View::EDIT_URL||view==View::EDIT_TOKEN||
     view==View::EDIT_PEER||view==View::COMPOSE){
    // Fn chords do not type punctuation into passwords or drafts.
    if(k.fn){return;}
    String& target=view==View::COMPOSE?editor:input;
    const bool erase=k.del&&target.length();
    if(erase)target.remove(target.length()-1);
    bool edited=erase;
    for(char ch:k.word){
      if(ch>=32&&ch<=126&&target.length()<(view==View::COMPOSE?600u:160u)){
        target+=ch;edited=true;
      }
    }
    if(view==View::COMPOSE&&edited){
      if(pendingTextId.length()){pendingTextId="";prefs.remove("txtid");}
      persistDraft();
    }
    if(k.enter){
      if(view==View::WIFI_PASSWORD){storeWiFiAfterTest(input);input="";return;}
      if(view==View::EDIT_URL){
        input.trim();
        if(!input.startsWith("https://")||input.indexOf(' ',8)>=0){
          setStatus("HTTPS obligatoire");
        }else{
          while(input.endsWith("/"))input.remove(input.length()-1);
          serverUrl=input;prefs.putString("server",serverUrl);
          serverReady=false;setStatus("Adresse enregistree");changeView(View::CONFIG);return;
        }
      }
      if(view==View::EDIT_TOKEN){
        if(input.length()<12){setStatus("Jeton trop court (12 min)");}
        else{deviceToken=input;prefs.putString("token",deviceToken);
          serverReady=false;input="";setStatus("Jeton enregistre");
          changeView(View::CONFIG);return;}
      }
      if(view==View::EDIT_PEER){
        input.trim();
        if(input!="alice"&&input!="bob")setStatus("Choisir alice ou bob");
        else{recipient=input;prefs.putString("peer",recipient);changeView(View::CONFIG);return;}
      }
      if(view==View::COMPOSE){sendText();return;}
    }
    paint();return;
  }
  if(view==View::HISTORY){
    bool changed=false;
    if(upPressed&&pageHistory+1<historySize){pageHistory++;textPage=0;changed=true;}
    if(downPressed&&pageHistory>0){pageHistory--;textPage=0;changed=true;}
    if(historySize){
      const int i=historySize-1-pageHistory;
      if(history[i].kind=="text"){
        const size_t pages=max(1,(int)((history[i].body.length()+92)/93));
        if(rightPressed&&textPage+1<pages)textPage++;
        if(leftPressed&&textPage>0)textPage--;
      }
      if(choose&&history[i].kind=="voice"){lastVoiceId=history[i].id;playVoice();}
    }
    if(changed&&historySize)acknowledgeMessage(history[historySize-1-pageHistory].id);
    paint();return;
  }
  if(view==View::SENT){if(choose)refreshSent();return;}
  if(view==View::DIAG){if(choose){serverTest();paint();}return;}
}
} // namespace
void setup(){
 Serial.begin(115200);auto cfg=M5.config();M5Cardputer.begin(cfg,true);
 M5Cardputer.Display.setRotation(1);
 loadSettings();initSd();paint();
 WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
 if(connectRememberedWiFi()){serverTest();}
  else{setStatus("Choisir le Wi-Fi dans le menu");}
 paint();
}
void loop(){
 M5Cardputer.update();handleInput();
 uint32_t now=millis();
 if(WiFi.status()!=WL_CONNECTED){
   serverReady=false;
   if(!wifiSsid.isEmpty()&&now-lastWifiAttempt>20000){lastWifiAttempt=now;WiFi.reconnect();}
 }else if(now-lastCheck>12000){
   lastCheck=now;
   if(configured()){
     if(!serverReady)serverTest();
     if(serverReady){
       pollInbox();
       // A send explicitly attempted earlier may be retried once online.
       // The original client id prevents duplicate delivery on lost responses.
       if(pendingTextId.length() && !editor.isEmpty())sendText();
     }
   }
   if(view==View::DIAG||view==View::HOME)paint();
 }
 delay(15);
}