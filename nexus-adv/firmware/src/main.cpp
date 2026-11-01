/* NEXUS ADV v0.4.1 SOURCE - Cardputer ADV ONLY.
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
constexpr uint16_t COL_BG=0x1083, COL_PANEL=0x2128, COL_ACCENT=0x576D, COL_TEXT=0xFFFF, COL_MUTED=0xAD75, COL_WARN=0xFBC0;
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
void paint(){
 switch(view){
 case View::HOME: drawLines("NEXUS / CARDPUTER ADV", "T: texte  I: recu  O: envoye", "V: vocal  U: envoyer vocal", "P: lire vocal  R: microSD", "W: WiFi  C: reglages", "D: diag  Z: jeter vocal");break;
 case View::WIFI_CONFIRM: drawLines("WI-FI MEMORISE",clipped(wifiSsid,32),"ENTREE: OUI, connecter","N: NON, autre reseau","F: oublier le Wi-Fi", "ESC: retour");break;
 case View::WIFI_LIST: {
   String rows[5];for(int i=0;i<5;i++){
     int pos=scanPage*5+i;
     if(pos<scanCount)rows[i]=String(i+1)+" "+clipped(scanNames[pos],28);
   }
   drawLines("WI-FI "+String(scanPage+1)+"/"+String(max(1,(int)(scanCount+4)/5)),rows[0],rows[1],rows[2],rows[3],rows[4]);
   break;}
 case View::WIFI_PASSWORD: drawLines("MOT DE PASSE WI-FI",clipped(wifiSsid,32),masked(input.length()),"ENTREE: tester/sauver", "ESC: annuler");break;
 case View::CONFIG: drawLines("REGLAGES DU SERVEUR", "1: adresse HTTPS", "2: jeton de cet appareil", "3: correspondant (alice/bob)", "W: configuration Wi-Fi", "ESC: retour");break;
 case View::EDIT_URL: drawLines("SERVEUR HTTPS", "Adresse du Worker:",clipped(input,34),"ENTREE: enregistrer","ESC: annuler");break;
 case View::EDIT_TOKEN: drawLines("JETON PERSONNEL", "Secret (minimum 12 chars)",masked(input.length()),"ENTREE: enregistrer","ESC: annuler");break;
 case View::EDIT_PEER: drawLines("DESTINATAIRE", "Choisir alice ou bob",input,"ENTREE: enregistrer","ESC: annuler");break;
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
String nonce(){char buf[25]; snprintf(buf,sizeof(buf),"%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());return String(buf);}
void loadSettings(){
  prefs.begin("nexusadv",false);
  wifiSsid=prefs.getString("ssid",""); wifiPass=prefs.getString("pwd","");
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
bool configured(){return serverUrl.startsWith("https://") && !serverUrl.endsWith("/") && deviceToken.length()>=12;}

bool openHttp(HTTPClient& http, WiFiClientSecure& client,const String& path){
  if(WiFi.status()!=WL_CONNECTED){setStatus("Wi-Fi deconnecte");return false;}
  if(!configured()){setStatus("Regler serveur + jeton (C)");return false;}
  if(!timeReady()){setStatus("Horloge non synchronisee");return false;}
  client.setCACert(NEXUS_TRUSTED_ROOTS);
  client.setTimeout(10000);
  http.setTimeout(10000);http.setConnectTimeout(7500);
  if(!http.begin(client,serverUrl+path)){setStatus("Erreur ouverture HTTPS");return false;}
  http.addHeader("Authorization","Bearer "+deviceToken);
  return true;
}
bool serverTest(){
  if(!configured()) {serverReady=false;setStatus("Configurer serveur et jeton (C)");return false;}
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
  int n=WiFi.scanNetworks(false,true);scanCount=0;scanPage=0;
  for(int i=0;i<n && scanCount<24;i++){
    String name=WiFi.SSID(i);if(name.isEmpty())continue;
    bool duplicate=false;for(int j=0;j<scanCount;j++)if(scanNames[j]==name)duplicate=true;
    if(!duplicate)scanNames[scanCount++]=name;
  }
  WiFi.scanDelete();setStatus(scanCount?"1-5: choix  N: suite  B: avant":"Aucun reseau trouve");paint();
}
void wifiSetup(){
  if(wifiSsid.isEmpty())scanWifi();else changeView(View::WIFI_CONFIRM);
}
void storeWiFiAfterTest(const String& newPass){
  String ssid=wifiSsid,pass=newPass;
  const String oldSsid=prefs.getString("ssid","");
  const String oldPass=prefs.getString("pwd","");
  wifiPass=pass;
  if(connectSaved()){
    prefs.putString("ssid",ssid);prefs.putString("pwd",pass);
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
void handleInput(){
  if(!(M5Cardputer.Keyboard.isChange()&&M5Cardputer.Keyboard.isPressed()))return;
  const auto k=M5Cardputer.Keyboard.keysState();
  // M5Cardputer 1.1.1 exposes no esc/up/down members: use Fn+` / Fn+; / Fn+.
  const bool escPressed=k.fn && M5Cardputer.Keyboard.isKeyPressed('`');
  const bool upPressed=k.fn && M5Cardputer.Keyboard.isKeyPressed(';');
  const bool downPressed=k.fn && M5Cardputer.Keyboard.isKeyPressed('.');
  if(escPressed){
    if(view==View::WIFI_PASSWORD || view==View::WIFI_LIST) {
      // Cancel a change without losing the saved Wi-Fi settings in RAM.
      wifiSsid=prefs.getString("ssid","");wifiPass=prefs.getString("pwd","");
    }
    input="";changeView(View::HOME);return;
  }
  if(view==View::WIFI_LIST){for(char ch:k.word){
    if((ch=='n'||ch=='N') && (scanPage+1)*5<scanCount){scanPage++;paint();return;}
    if((ch=='b'||ch=='B') && scanPage>0){scanPage--;paint();return;}
    int index=scanPage*5+(ch-'1');
    if(ch>='1'&&ch<='5'&&index<scanCount){wifiSsid=scanNames[index];input="";changeView(View::WIFI_PASSWORD);return;}
  }return;}
  if(view==View::WIFI_CONFIRM){
    if(k.enter){connectSaved();serverTest();changeView(View::HOME);return;}
    for(char ch:k.word){if(ch=='n'||ch=='N'){scanWifi();return;}if(ch=='f'||ch=='F'){changeView(View::DELETE_CONFIRM);return;}}return;
  }
  if(view==View::DELETE_CONFIRM){if(k.enter){prefs.remove("ssid");prefs.remove("pwd");wifiSsid="";wifiPass="";WiFi.disconnect();setStatus("Wi-Fi oublie");scanWifi();}return;}
  if(view==View::DISCARD_VOICE){if(k.enter){pendingVoiceId="";prefs.remove("vocid");if(sdReady)SD.remove(TX_AUDIO);setStatus("Brouillon vocal efface");changeView(View::HOME);}return;}
  if(view==View::CONFIG){for(char ch:k.word){switch(ch){
   case '1':input=serverUrl;changeView(View::EDIT_URL);return;
   case '2':input="";changeView(View::EDIT_TOKEN);return;
   case '3':input=recipient;changeView(View::EDIT_PEER);return;
   case 'w':case 'W':wifiSetup();return;
  }}return;}
  if(view==View::WIFI_PASSWORD||view==View::EDIT_URL||view==View::EDIT_TOKEN||view==View::EDIT_PEER||view==View::COMPOSE){
    String& target=(view==View::COMPOSE)?editor:input;
    if(k.del){if(target.length())target.remove(target.length()-1);}
    bool edited=false;
    for(char ch:k.word){if(ch>=32&&ch<=126&&target.length()<((view==View::COMPOSE)?600:160)){target+=ch;edited=true;}}
    if(view==View::COMPOSE && (edited||k.del) && pendingTextId.length()){
      // A modified draft is a new message, not a retry of the previous request.
      pendingTextId="";prefs.remove("txtid");
    }
    if(view==View::COMPOSE)persistDraft();
    if(k.enter){
      if(view==View::WIFI_PASSWORD){storeWiFiAfterTest(input);input="";return;}
      if(view==View::EDIT_URL){input.trim();if(!input.startsWith("https://")||input.indexOf(' ',8)>=0){setStatus("HTTPS obligatoire");}
        else{while(input.endsWith("/"))input.remove(input.length()-1);serverUrl=input;prefs.putString("server",serverUrl);serverReady=false;setStatus("Adresse enregistree");changeView(View::CONFIG);return;}}
      if(view==View::EDIT_TOKEN){if(input.length()<12){setStatus("Jeton trop court");}else{deviceToken=input;prefs.putString("token",deviceToken);serverReady=false;input="";changeView(View::CONFIG);setStatus("Jeton enregistre");return;}}
      if(view==View::EDIT_PEER){input.trim();if(input!="alice"&&input!="bob")setStatus("Choisir alice ou bob");
        else{recipient=input;prefs.putString("peer",recipient);changeView(View::CONFIG);return;}}
      if(view==View::COMPOSE){sendText();return;}
    }
    paint();return;
  }
  if(view==View::HISTORY){
    bool changed=false;
    if(upPressed&&pageHistory+1<historySize){pageHistory++;textPage=0;changed=true;}
    if(downPressed&&pageHistory>0){pageHistory--;textPage=0;changed=true;}
    if(historySize){
      int i=historySize-1-pageHistory;
      if(history[i].kind=="text") for(char ch:k.word){
        const size_t pages=max(1,(int)((history[i].body.length()+92)/93));
        if((ch=='n'||ch=='N') && textPage+1<pages)textPage++;
        if((ch=='b'||ch=='B') && textPage>0)textPage--;
      }
    }
    if(changed && historySize)acknowledgeMessage(history[historySize-1-pageHistory].id);
    for(char ch:k.word)if(ch=='p'||ch=='P')playVoice();
    paint();return;
  }
  if(view==View::SENT){if(k.enter)refreshSent();return;}
  if(view!=View::HOME)return;
  for(char ch:k.word){switch(ch){
    case 't':case 'T':changeView(View::COMPOSE);return;
    case 'i':case 'I':pageHistory=0;textPage=0;pollInbox();changeView(View::HISTORY);
      if(historySize)acknowledgeMessage(history[historySize-1].id);return;
    case 'o':case 'O':changeView(View::SENT);refreshSent();return;
    case 'r':case 'R':if(!sdReady){initSd();setStatus(sdReady?"microSD reconnue":"microSD absente");paint();}return;
    case 'w':case 'W':wifiSetup();return;
    case 'c':case 'C':changeView(View::CONFIG);return;
    case 'd':case 'D':changeView(View::DIAG);return;
    case 'v':case 'V':recordVoice();return;
    case 'u':case 'U':uploadVoice();return;
    case 'p':case 'P':playVoice();return;
    case 'z':case 'Z':changeView(View::DISCARD_VOICE);return;
  }}
}
} // namespace
void setup(){
 Serial.begin(115200);auto cfg=M5.config();M5Cardputer.begin(cfg,true);
 M5Cardputer.Display.setRotation(1);
 loadSettings();initSd();paint();
 WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
 if(!wifiSsid.isEmpty()){connectSaved();if(netReady)serverTest();}
 else{setStatus("Configurer le Wi-Fi (W)");}
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