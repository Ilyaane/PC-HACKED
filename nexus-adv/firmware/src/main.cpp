/* NEXUS ADV v1.0.0 RELEASE CANDIDATE SOURCE - Cardputer ADV ONLY.
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
#include "nexus_morse.h"
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace {
constexpr uint16_t COL_BG=0x0000, COL_PANEL=0x18C3, COL_ACCENT=0xE81F, COL_TEXT=0xFFFF, COL_MUTED=0xAD55, COL_WARN=0xFD20;
constexpr uint16_t COL_ACCENT2=0x07FF, COL_LINE=0x3190;
constexpr int SD_SCK=40, SD_MISO=39, SD_MOSI=14, SD_CS=12;
constexpr uint32_t AUDIO_RATE=16000;
constexpr size_t AUDIO_FRAME=1024, AUDIO_FRAMES=48; // 3.072 seconds, 98,348-byte WAV
constexpr size_t MAX_VOICE=120000;
constexpr char TX_AUDIO[]="/nexus_tx.wav", NEW_TX_AUDIO[]="/nexus_tx.new", OLD_TX_AUDIO[]="/nexus_tx.bak", RX_AUDIO[]="/nexus_rx.wav", TMP_RX[]="/nexus_rx.tmp", RX_BACKUP[]="/nexus_rx.bak";
constexpr char HISTORY[]="/nexus_history.txt";
constexpr char VOICE_ROOT[]="/NEXUS/MESSAGES_VOCAUX";
constexpr char VOICE_INDEX[]="/NEXUS/MESSAGES_VOCAUX/INDEX.txt";
constexpr char TEXT_ROOT[]="/NEXUS/MESSAGES_TEXTE";
constexpr uint8_t HISTORY_CAP=24;
constexpr uint8_t VOICE_LIST_MAX=24;

Preferences prefs;
String wifiSsid, wifiPass, serverUrl, deviceToken, recipient="bob", editor, statusLine="Initialisation";
String pendingTextId, pendingVoiceId;
String lastSentInfo="";
uint32_t cursor=0, lastVoiceId=0, lastCheck=0, lastWifiAttempt=0;
uint32_t pendingReadId=0, draftChangedAt=0;
bool draftDirty=false, wifiScanPending=false;
uint32_t wifiScanStartedAt=0, wifiAttemptStartedAt=0;
struct NetRequest {
 enum class Kind { POLL, SEND_TEXT };
 Kind kind=Kind::POLL;
 String url,token,recipient,payload,text,clientId;
 uint32_t cursor=0,readId=0;
};
struct NetReply {
 NetRequest::Kind kind=NetRequest::Kind::POLL;
 int sessionCode=-1,inboxCode=-1,unreadCode=-1,readCode=-1,sendCode=-1;
 String session,inbox,unread,sendResponse,clientId,sentText,recipient;
 uint32_t readId=0;
};
QueueHandle_t netReplyQueue=nullptr;
bool netWorkerBusy=false,textSendQueued=false;
enum class WifiAttempt { NONE, BOOT, USER };
WifiAttempt wifiAttempt=WifiAttempt::NONE;
int bootWifiSlot=0;
String wifiAttemptSsid, wifiAttemptPass, wifiPreviousSsid, wifiPreviousPass;
uint32_t unreadCount=0, latestUnreadId=0;
String latestUnreadPreview, latestUnreadSender;
bool unreadSynced=false;
bool notificationSound=true;
uint32_t notifiedMessageId=0;
int batteryVoltageSmooth=-1;
int batteryLevelSmooth=-1;
uint32_t batterySampleAt=0;
bool sdReady=false, netReady=false, serverReady=false, prefsReady=false, storageFault=false;
enum class View { HOME, SECTION, VOICE_ARCHIVE, VOICE_READY, VOICE_RECORDING, SEND_CONFIRM, SEND_PROGRESS, SEND_RESULT, MORSE_EDIT, MORSE_RESULT, MORSE_TRAIN, STORAGE_INFO, BATTERY_INFO, SEARCH_EDIT, SEARCH_RESULTS, SEARCH_VIEW, WIFI_CONFIRM, WIFI_LIST, WIFI_PASSWORD, CONFIG, EDIT_URL, EDIT_TOKEN, EDIT_PEER, COMPOSE, HISTORY, SENT, DIAG, DELETE_CONFIRM, DISCARD_VOICE };
View view=View::HOME;
String input;
String scanNames[24];
bool scanSaved[24]={}, scanOpen[24]={};
uint8_t homeIndex=0, configIndex=0, wifiIndex=0, confirmIndex=0;
constexpr uint8_t HOME_COUNT=6, CONFIG_COUNT=4, WIFI_SLOTS=5;
uint8_t sectionIndex=0, morseMode=0;
uint8_t morseWpm=12; char morseQuizLetter='A'; bool morseQuizAnswered=false;
String morseQuizAnswer;
String morseOutput;
uint8_t scanCount=0, scanPage=0, pageHistory=0;
uint8_t textPage=0, voiceSelection=0, voiceCount=0, voiceChoice=0;
uint32_t voiceRecordedSamples=0;
constexpr uint8_t SEARCH_MAX=24;
String searchPaths[SEARCH_MAX];
uint8_t searchSize=0,searchSelection=0,searchPage=0;
uint32_t searchMatches=0,searchScanned=0;
bool searchLimited=false;
String searchQuery,searchViewBody;
struct VoiceArchive {uint32_t id=0; String sender,stamp,path;};
VoiceArchive voiceArchives[VOICE_LIST_MAX];
bool fullRepaint=true;
struct History {uint32_t id=0; String sender,kind,body;};
History history[HISTORY_CAP];
uint8_t historySize=0;

String masked(size_t count){String s;for(size_t i=0;i<count && i<40;i++)s+='*';return s;}
String clipped(String s,size_t max){if(s.length()>max)return s.substring(0,max-2)+"..";return s;}
void setStatus(const String& msg); // defined immediately below
// Debounce draft writes: one NVS write after a pause, not one per key.
void persistDraft(){draftDirty=true;draftChangedAt=millis();}
void flushDraftIfDue(){
  if(!draftDirty || !prefsReady || millis()-draftChangedAt<1300)return;
  if(prefs.putString("draft",editor)>0 && prefs.getString("draft","")==editor)
    draftDirty=false;
  else {
    storageFault=true;
    setStatus("Brouillon non sauvegarde : NVS pleine");
    draftChangedAt=millis();
  }
}
void setStatus(const String& msg){ statusLine=clipped(msg,60);Serial.println("NEXUS: "+statusLine); }
// NEXUS Neon UI: original lightweight vector art with Bruce-like navigation.
// Drawn from primitives; no copied icons, bitmap assets, or extra dependencies.
struct UiPalette {uint16_t dark,mid,primary,shine;};
UiPalette palette(){
  static const UiPalette colors[]={
    {0x080C,0x11C8,0x05FF,0x7DFF}, // Messages: blue / cyan
    {0x2004,0x49A7,0xFB2D,0xFDE9}, // Voice: warm coral
    {0x180A,0x492B,0xC31F,0xEBBF}, // Morse: neon violet
    {0x0026,0x1169,0x07FF,0x7DFF}, // Wi-Fi: blue / teal
    {0x0805,0x1287,0x3FEA,0xB7FB}, // System: green
    {0x2080,0x51E0,0xFF20,0xFFCF}  // Tools: gold
  };
  return colors[homeIndex<HOME_COUNT?homeIndex:0];
}
uint16_t colorMix(uint16_t x,uint16_t y,uint8_t ratio){
  const uint16_t r=(((x>>11)&31)*(255-ratio)+((y>>11)&31)*ratio)/255;
  const uint16_t g=(((x>>5)&63)*(255-ratio)+((y>>5)&63)*ratio)/255;
  const uint16_t b=((x&31)*(255-ratio)+(y&31)*ratio)/255;
  return (r<<11)|(g<<5)|b;
}
void drawBackdrop(){
  auto& d=M5Cardputer.Display; const auto theme=palette();
  // Smooth full-colour background in stripes, inexpensive compared with bitmaps.
  for(int y=22;y<114;y+=3){
    const uint8_t fade=(uint8_t)((y-22)*170/92);
    d.fillRect(0,y,240,3,colorMix(theme.mid,theme.dark,fade));
  }
  d.drawLine(0,91,240,38,colorMix(theme.primary,theme.dark,105));
  d.drawLine(0,99,240,46,colorMix(theme.primary,theme.dark,135));
  for(int i=0;i<8;i++)
    d.fillCircle((i*47+21)%239,24+(i*19)%85,1,colorMix(theme.shine,theme.dark,125));
}
void drawStatusBar(const String& heading){
  auto& d=M5Cardputer.Display;const auto p=palette();
  d.fillRect(0,0,240,22,0x080F);
  d.fillRect(0,20,240,2,p.primary);
  d.fillRoundRect(5,3,17,15,4,p.primary);
  d.setTextSize(1);d.setTextColor(0x0000,p.primary);d.setCursor(10,7);d.print("N");
  d.setTextColor(0xFFFF,0x080F);d.setCursor(28,7);d.print(clipped(heading,24));
  const uint16_t signal=WiFi.status()==WL_CONNECTED?p.shine:0xFDA0;
  // Small signal, SD and battery indicators.
  d.fillCircle(191,13,2,signal);
  d.drawLine(184,8,190,4,signal);d.drawLine(190,4,197,8,signal);
  d.drawLine(187,10,190,8,signal);d.drawLine(190,8,194,10,signal);
  d.fillRoundRect(203,5,12,10,2,sdReady?p.shine:0x4208);
  d.setTextColor(0x0000,sdReady?p.shine:0x4208);d.setCursor(206,6);d.print("S");
  const int batt=constrain((int)M5Cardputer.Power.getBatteryLevel(),0,100);
  d.drawRect(219,5,16,11,0xFFFF);d.fillRect(236,8,2,5,0xFFFF);
  if(batt)d.fillRect(221,7,12*batt/100,7,batt<15?0xFBC0:p.shine);
}
void drawFooter(const String& text){
  auto& d=M5Cardputer.Display;const auto p=palette();
  d.fillRect(0,114,240,21,0x080F);
  d.fillRect(0,114,240,2,p.primary);
  d.setTextSize(1);d.setTextColor(0xD6BA,0x080F);
  d.setCursor(6,120);d.print(clipped(text,37));
}
void drawLines(const String& title,const String& a="",const String& b="",
               const String& c="",const String& d="",const String& e=""){
  auto& l=M5Cardputer.Display;const auto p=palette();
  drawBackdrop();drawStatusBar(title);
  const String lines[]={a,b,c,d,e};
  for(int i=0;i<5;i++){
    const int y=25+i*17;
    const uint16_t fill=(i%2)?colorMix(p.dark,0x0000,130):colorMix(p.mid,0x0000,125);
    l.fillRoundRect(6,y,228,16,4,fill);
    if(i==0)l.fillRect(7,y+3,2,10,p.primary);
    l.setTextSize(1);l.setTextColor(i==0?p.shine:0xFFFF,fill);
    l.setCursor(14,y+4);l.print(clipped(lines[i],35));
  }
  drawFooter(statusLine);
}
void menuFrame(const String& title){drawBackdrop();drawStatusBar(title);}
void drawMenu(const String& title,const String opts[],int count,int selected,
              const String& hint="; Haut   . Bas   Entree OK"){
  auto& l=M5Cardputer.Display;const auto p=palette();
  menuFrame(title);const int start=(selected/5)*5;
  for(int row=0;row<5;row++){
    const int idx=start+row;if(idx>=count)break;
    const int y=26+17*row;const bool active=idx==selected;
    const uint16_t fill=active?p.primary:colorMix(p.mid,0x0000,115);
    l.fillRoundRect(5,y-1,230,17,5,fill);
    if(active){
      l.drawRoundRect(5,y-1,230,17,5,p.shine);
      l.fillTriangle(10,y+4,10,y+12,17,y+8,0x0000);
    }else l.fillCircle(13,y+8,2,p.shine);
    l.setTextSize(1);l.setTextColor(active?0x0000:0xFFFF,fill);
    l.setCursor(23,y+4);l.print(clipped(opts[idx],32));
  }
  drawFooter((count>5?String(selected+1)+"/"+String(count)+" ":"")+hint);
}

String sectionTitle(int section){
  static const char* names[]={"MESSAGES","VOCAL","MORSE","WI-FI","SYSTEME","AUTRES"};
  return String(names[constrain(section,0,5)]);
}
int sectionOptions(int section,String items[]){
  if(section==0){items[0]="Boite de reception";items[1]="Nouveau message";items[2]="Messages envoyes";return 3;}
  if(section==1){items[0]="Enregistrer un vocal";items[1]="Envoyer le vocal";items[2]="Archives microSD";
    items[3]="Ecouter dernier vocal";items[4]="Effacer brouillon vocal";return 5;}
  if(section==2){items[0]="Ecrire en Morse";items[1]="Jouer le Morse (son)";items[2]="Decoder son (beta)";items[3]="Entrainement auditif";items[4]="Vitesse : "+String(morseWpm)+" MPM";return 5;}
  if(section==3){items[0]="Gerer les reseaux Wi-Fi";items[1]="Etat reseau";return 2;}
  if(section==4){items[0]="Parametres NEXUS";items[1]="Diagnostics";items[2]=String("Son notification : ")+(notificationSound?"ON":"OFF");items[3]="Detecter la microSD";return 4;}
  items[0]="Rechercher dans textes";items[1]="Batterie : tension et %";items[2]="Espace sur microSD";items[3]="A propos de NEXUS";return 4;
}
void drawHome(){
  auto& l=M5Cardputer.Display;const auto p=palette();
  static const char* names[]={"MESSAGES","VOCAUX","MORSE","WI-FI","SYSTEME","AUTRES"};
  static const char* captions[]={"Conversations","Audio et microSD",
    "Coder et decoder","Reseaux disponibles","Reglages","Archives et outils"};
  drawBackdrop();drawStatusBar("NEXUS ADV");
  // Solid title panel and glowing navigation, similar rhythm to Bruce's carousel.
  l.fillRoundRect(38,26,164,76,11,colorMix(p.dark,0x0000,70));
  l.drawRoundRect(38,26,164,76,11,p.primary);
  l.drawFastHLine(52,30,136,p.shine);
  l.fillTriangle(10,65,30,52,30,78,p.primary);
  l.fillTriangle(230,65,210,52,210,78,p.primary);
  l.fillTriangle(15,65,26,58,26,72,p.shine);
  l.fillTriangle(225,65,214,58,214,72,p.shine);
  const int x=120,y=61;
  switch(homeIndex){
    case 0: // Crisp envelope + incoming notification
      l.fillRoundRect(x-28,y-20,56,39,5,p.shine);
      l.fillRoundRect(x-25,y-17,50,32,3,p.dark);
      l.drawLine(x-25,y-15,x,y+5,p.shine);
      l.drawLine(x+25,y-15,x,y+5,p.shine);
      l.drawLine(x-25,y+14,x-8,y+1,p.shine);
      l.drawLine(x+25,y+14,x+8,y+1,p.shine);
      l.fillCircle(x+29,y-23,7,p.primary);break;
    case 1: // Clear microphone silhouette and waves
      l.fillRoundRect(x-9,y-23,18,30,9,p.shine);
      l.drawRoundRect(x-21,y-12,42,32,13,p.primary);
      l.fillRect(x-21,y-13,7,12,p.dark);
      l.fillRect(x+14,y-13,7,12,p.dark);
      l.drawFastVLine(x,y+20,9,p.shine);
      l.fillRoundRect(x-14,y+29,28,4,2,p.shine);
      l.drawLine(x-30,y-9,x-33,y+8,p.primary);
      l.drawLine(x+30,y-9,x+33,y+8,p.primary);break;
    case 2: // Morse pulse line, dots and dashes
      l.fillCircle(x-30,y-11,7,p.shine);
      l.fillRoundRect(x-13,y-17,43,12,5,p.shine);
      l.fillCircle(x+36,y-11,6,p.primary);
      l.fillRoundRect(x-36,y+13,24,8,4,p.primary);
      l.fillCircle(x+3,y+17,6,p.shine);
      l.fillRoundRect(x+17,y+13,23,8,4,p.shine);break;
    case 3: // Three WiFi signal tiers
      l.fillCircle(x,y+22,7,p.shine);
      for(int i=-1;i<=1;i++){
        l.drawLine(x+i*10,y+11-abs(i)*7,x+i*25,y-14-abs(i)*5,p.shine);
        if(i!=0)l.drawLine(x+i*25,y-14-abs(i)*5,x+i*39,y+1-abs(i)*3,p.primary);
      }
      l.drawLine(x-34,y-20,x,y-34,p.primary);
      l.drawLine(x,y-34,x+34,y-20,p.primary);break;
    case 4: // Gear
      l.fillCircle(x,y,25,p.shine);
      l.fillCircle(x,y,18,p.dark);
      l.fillCircle(x,y,9,p.primary);
      l.fillCircle(x,y,3,p.dark);
      for(int i=0;i<8;i++){
        const float a=i*3.14159265f/4;
        l.drawLine(x+25*cosf(a),y+25*sinf(a),x+31*cosf(a),y+31*sinf(a),p.shine);
      }break;
    default: // Utility folder
      l.fillRoundRect(x-36,y-20,36,12,3,p.shine);
      l.fillRoundRect(x-36,y-14,72,45,5,p.primary);
      l.drawRoundRect(x-32,y-9,64,34,4,p.shine);
      l.fillCircle(x-19,y+8,5,p.dark);
      l.fillCircle(x,y+8,5,p.dark);
      l.fillCircle(x+19,y+8,5,p.dark);break;
  }
  l.fillRoundRect(58,87,124,13,3,p.primary);
  l.setTextSize(1);l.setTextColor(0x0000,p.primary);
  l.setCursor(120-strlen(names[homeIndex])*3,90);l.print(names[homeIndex]);
  l.setTextColor(0xFFFF,p.dark);
  l.setCursor(120-strlen(captions[homeIndex])*3,105);l.print(captions[homeIndex]);
  if(unreadCount>0 && homeIndex==0){
    l.fillRoundRect(170,24,36,15,5,0xFC00);
    l.setTextColor(0x0000,0xFC00);l.setCursor(174,28);
    l.print(unreadCount>99?"99+":String(unreadCount));
  }
  drawFooter(" , / CHOISIR   ENTREE OUVRIR");
}

void paint(){
 // Batch drawing to avoid visible partial frames and SPI chatter.
 auto& display=M5Cardputer.Display;
 display.startWrite();
 static bool firstFrame=true;
 static View lastView=View::HOME;
 fullRepaint=firstFrame || lastView!=view;
 firstFrame=false;lastView=view;
 switch(view){
 case View::HOME:drawHome();break;
 case View::SECTION: {
   String items[6];int count=sectionOptions(homeIndex,items);
   drawMenu(sectionTitle(homeIndex),items,count,sectionIndex);
   break;}
 case View::MORSE_EDIT: {
   String tail=input.length()>90?input.substring(input.length()-90):input;
   drawLines(morseMode==0?"MORSE VERS TEXTE":"MORSE EN AUDIO",
     "Tape du texte et valide",
     tail.substring(0,30),
     tail.length()>30?tail.substring(30,60):"",
     tail.length()>60?tail.substring(60):"",
     "Entree: convertir  OPT: retour");break;}
 case View::MORSE_TRAIN:drawLines("MORSE : ENTRAINEMENT",
     "Ecoute et tape la lettre",morseQuizAnswered?("Reponse: "+String(morseQuizLetter)):("A toi: "+morseQuizAnswer),
     "Vitesse: "+String(morseWpm)+" MPM",morseQuizAnswered?"Entree: nouveau test":"Entree: verifier", "OPT: retour");break;
 case View::MORSE_RESULT: {
   String shown=morseOutput.length()>120?morseOutput.substring(morseOutput.length()-120):morseOutput;
   drawLines("RESULTAT MORSE",
     shown.substring(0,30),
     shown.length()>30?shown.substring(30,60):"",
     shown.length()>60?shown.substring(60,90):"",
     shown.length()>90?shown.substring(90):"",
     "OPT: retour");break;}
 case View::VOICE_READY: {
   if(!sdReady || !SD.exists(TX_AUDIO)){
     drawLines("VOCAL NON DISPONIBLE","Aucun brouillon sur microSD","Retour : OPT");
     break;
   }
   String items[]={"Envoyer le vocal","Ecouter le brouillon","Reenregistrer (remplacer)","Garder et retourner"};
   drawMenu("VOCAL PRET",items,4,voiceChoice,";/. choisir  ENTREE confirmer");
   break;
 }
 case View::VOICE_RECORDING: {
   const uint32_t seconds=voiceRecordedSamples/AUDIO_RATE;
   drawLines("ENREGISTREMENT VOCAL",
      "Micro actif : 16 kHz mono",
      "Duree: "+String(seconds)+"/3 secondes",
      "Carte microSD en ecriture",
      "OPT : annuler",
      "Patiente : capture audio");break;
 }
 case View::SEND_CONFIRM: {
   const String shown=editor.length()>92?editor.substring(0,92)+"...":editor;
   drawLines("CONFIRMER L'ENVOI",
     "Vers : "+recipient,
     shown.substring(0,min((size_t)31,shown.length())),
     shown.length()>31?shown.substring(31,min((size_t)62,shown.length())):"",
     "ENTREE envoyer / OPT modifier",
     "Le texte reste en brouillon");break;
 }
 case View::SEND_PROGRESS:
   drawLines("ENVOI EN COURS",
      "Message vers : "+recipient,
      "Connexion securisee HTTPS",
      "Clavier libre : retour OPT",
      "Patiente : confirmation",
      "Aucun doublon envoye");break;
 case View::SEND_RESULT:
   drawLines("STATUT DE L'ENVOI",
      clipped(statusLine,35),
      "ENTREE : retour messages",
      "OPT : revenir au brouillon");break;
 case View::VOICE_ARCHIVE: {
   if(!sdReady){drawLines("ARCHIVES VOCALES","Inserer une microSD"," puis detecter microSD","OPT: retour");break;}
   if(!voiceCount){drawLines("ARCHIVES VOCALES","Aucun vocal archive","Les vocaux recus sont","sauvegardes automatiquement","OPT: retour");break;}
   String opts[VOICE_LIST_MAX];
   for(int i=0;i<voiceCount;i++){
     const VoiceArchive& e=voiceArchives[voiceCount-1-i];
     opts[i]=e.stamp+" "+e.sender+" #"+String(e.id);
   }
   drawMenu("VOCAL "+String(voiceSelection+1)+"/"+String(voiceCount),
            opts,voiceCount,voiceSelection);
   break;}
 case View::WIFI_CONFIRM: {
   const String opts[]={"Connecter : "+clipped(wifiSsid,23),"Choisir un autre Wi-Fi","Oublier ce reseau"};
   drawMenu("WI-FI MEMORISE",opts,3,confirmIndex);break;}
 case View::WIFI_LIST: {
   if(scanCount==0){drawLines("RESEAUX WI-FI",wifiScanPending?"Recherche en cours...":"Aucun reseau trouve","Fn+ESC : retour");break;}
   String opts[24];
   for(int i=0;i<scanCount;i++)
     opts[i]=String(scanSaved[i]?"[MEM] ":scanOpen[i]?"[LIB] ":"[WPA] ")+clipped(scanNames[i],28);
   drawMenu("RESEAUX  "+String(wifiIndex+1)+"/"+String(scanCount),opts,scanCount,wifiIndex,
      "MEM=sauve  ;/. + Entree");break;}
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
 case View::EDIT_TOKEN:drawLines("JETON PERSONNEL","Minimum 10 caracteres",
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
 case View::DIAG:drawLines("DIAGNOSTICS",
    "Wi-Fi: "+String(WiFi.status()==WL_CONNECTED?"OK ":"KO ")+clipped(wifiSsid,20),
    "IP: "+WiFi.localIP().toString(),
    "RAM libre: "+String(ESP.getFreeHeap())+" octets",
    "Serveur: "+String(serverReady?"OK":"NON")+"   NVS: "+String(!prefsReady?"KO":storageFault?"ERR":"OK"),
    "Role: "+String(recipient=="bob"?"ALICE":"BOB")+"  Non lus: "+String(unreadCount));break;
 case View::BATTERY_INFO: {
   const String v=batteryVoltageSmooth>0?String(batteryVoltageSmooth)+" mV":"mesure indisponible";
   const String percent=batteryLevelSmooth>=0?String(batteryLevelSmooth)+" %":"indisponible";
   drawLines("BATTERIE / ESTIMATION",
     "Niveau estime: "+percent,"Tension mesuree: "+v,
     "Pas de jauge precise a bord",
     "Tester debranche si possible",
     "Entree: actualiser");break;}
 case View::SEARCH_EDIT: {
   const String value=input.length()>68?input.substring(input.length()-68):input;
   drawLines("RECHERCHE ARCHIVES TXT","Recherche (min 2 signes)",
     value.substring(0,34),value.length()>34?value.substring(34):"",
     "Entree: chercher", "OPT: retour");break;}
 case View::SEARCH_RESULTS: {
   if(!searchSize){
     drawLines("RESULTATS ARCHIVES","Aucun message trouve",
       "Fichiers scannes: "+String(searchScanned),
       searchLimited?"Recherche partielle":"Recherche terminee",
       "OPT: retour");break;
   }
   String opts[SEARCH_MAX];
   for(int i=0;i<searchSize;i++){
     String p=searchPaths[i];int slash=p.lastIndexOf('/');
     opts[i]=slash>=0?p.substring(slash+1):p;
   }
   drawMenu("RESULTATS "+String(searchMatches)+" ("+String(searchSize)+")",
     opts,searchSize,searchSelection);break;}
 case View::SEARCH_VIEW: {
   const size_t count=max((size_t)1,(searchViewBody.length()+92)/93);
   if(searchPage>=count)searchPage=0;
   String p=searchViewBody.substring(searchPage*93,(searchPage+1)*93);
   drawLines("MESSAGE p"+String(searchPage+1)+"/"+String(count),
     p.substring(0,31),p.length()>31?p.substring(31,62):"",
     p.length()>62?p.substring(62,93):"",
     "Virgule/slash: page",
     "OPT: resultats");break;}
 case View::STORAGE_INFO: {
   String free="MicroSD absente",total="Vides";
   if(sdReady){const uint64_t t=SD.totalBytes(),u=SD.usedBytes();
     free="Libre: "+String((uint32_t)((t>u?t-u:0)/(1024*1024)))+" Mio";
     total="Total: "+String((uint32_t)(t/(1024*1024)))+" Mio";}
   drawLines("STOCKAGE ET VERSION","NEXUS STABILITE BETA",free,total,
     "Code conserve en NVS", "OPT : retour");break;}
 case View::DELETE_CONFIRM:drawLines("EFFACER WI-FI?", "ENTREE: oublier le reseau", "ESC: ne rien effacer");break;
 case View::DISCARD_VOICE:drawLines("JETER BROUILLON VOCAL?", "ENTREE: supprimer le WAV", "ESC: le conserver");break;
 }
 display.endWrite();
}
void changeView(View v){view=v;paint();}
bool saveSetting(const char* key, const String& value){
  if(!prefsReady){storageFault=true;setStatus("MEMOIRE NVS indisponible");return false;}
  prefs.putString(key,value);
  // Preferences write can fail when NVS partition is corrupted/full or missing;
  // do not claim success before an immediate readback.
  if(prefs.getString(key, String("__NVS_NOT_FOUND__")) != value){
    storageFault=true;setStatus("ERREUR NVS: reglage non sauvegarde");return false;
  }
  return true;
}
String wifiKey(int slot,char type){return String("w")+type+String(slot);}
int wifiSlot(const String& ssid){
  if(ssid.isEmpty() || !prefsReady)return -1;
  for(int i=0;i<WIFI_SLOTS;i++)
    if(prefs.getString(wifiKey(i,'s').c_str(),"")==ssid)return i;
  return -1;
}
bool rememberNetwork(const String& ssid,const String& pass){
  if(ssid.isEmpty() || !prefsReady)return false;
  int slot=wifiSlot(ssid);
  if(slot<0){
    slot=-1;
    for(int i=0;i<WIFI_SLOTS;i++)
      if(prefs.getString(wifiKey(i,'s').c_str(),"").isEmpty()){slot=i;break;}
    if(slot<0)slot=0; // bounded replacement when all five slots are used
  }
  const String passKey=wifiKey(slot,'p');
  const String ssidKey=wifiKey(slot,'s');
  // Write password first, SSID last, to avoid labeling incomplete credentials saved.
  const bool passOk=saveSetting(passKey.c_str(),pass);
  const bool ssidOk=passOk && saveSetting(ssidKey.c_str(),ssid);
  return passOk && ssidOk;
}
void forgetNetwork(const String& ssid){
  const int slot=wifiSlot(ssid);
  if(slot>=0){prefs.remove(wifiKey(slot,'s').c_str());prefs.remove(wifiKey(slot,'p').c_str());}
}

String nonce(){char buf[25]; snprintf(buf,sizeof(buf),"%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());return String(buf);}
void loadSettings(){
  prefsReady=prefs.begin("nexusadv",false);
  if(!prefsReady){storageFault=true;setStatus("NVS KO: verifier partition flash");return;}
  wifiSsid=prefs.getString("ssid",""); wifiPass=prefs.getString("pwd","");
  if(!wifiSsid.isEmpty() && wifiSlot(wifiSsid)<0)rememberNetwork(wifiSsid,wifiPass); // migrate old NVS
  serverUrl=prefs.getString("server",""); deviceToken=prefs.getString("token","");
  recipient=prefs.getString("peer","bob"); editor=prefs.getString("draft","");
  unreadCount=prefs.getUInt("unread",0);unreadSynced=false;
  notificationSound=prefs.getBool("beep",true);
  notifiedMessageId=prefs.getUInt("notified",0u);
  morseWpm=(uint8_t)constrain((int)prefs.getUInt("morsewpm",12u),5,30);
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
    if(historySize==HISTORY_CAP){for(int k=1;k<HISTORY_CAP;k++)history[k-1]=history[k];historySize--;}
    auto& h=history[historySize++];h.id=line.substring(0,a).toInt();h.sender=line.substring(a+1,b);h.kind=line.substring(b+1,c);h.body=line.substring(c+1);
  }f.close();
}
bool remember(uint32_t id,String sender,String kind,String text){
  text.replace("\n"," ");text.replace("\r"," ");text.replace("|","/");
  // Do not persist duplicate messages when NVS cursor is behind the SD journal.
  for(uint8_t i=0;i<historySize;i++)if(history[i].id==id)return true;
  if(sdReady){
    File f=SD.open(HISTORY,FILE_APPEND);
    if(!f)return false;
    const String line=String(id)+"|"+sender+"|"+kind+"|"+text+"\n";
    const bool ok=f.print(line)==line.length();
    f.flush();f.close();
    if(!ok)return false;
  }
  if(historySize==HISTORY_CAP){
    for(int i=1;i<HISTORY_CAP;i++)history[i-1]=history[i];
    historySize--;
  }
  History& h=history[historySize++];
  h.id=id;h.sender=sender;h.kind=kind;h.body=text;
  return true;
}
void initSd(){
  SPI.begin(SD_SCK,SD_MISO,SD_MOSI,SD_CS);
  sdReady=SD.begin(SD_CS,SPI,20000000);
  if(sdReady){historySize=0;loadHistory();}
  else{historySize=0;cursor=0;} // No persistent journal: fetch from server on boot.
}
bool timeReady(){
  if(time(nullptr)>1700000000)return true;
  configTime(0,0,"pool.ntp.org","time.google.com");
  for(int i=0;i<50&&time(nullptr)<1700000000;i++)delay(100);
  return time(nullptr)>1700000000;
}
bool connectSaved(){
  if(wifiSsid.isEmpty()) {setStatus("Aucun Wi-Fi memorise");netReady=false;return false;}
  WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
  WiFi.disconnect(false,false);delay(80);
  WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());
  setStatus("Connexion Wi-Fi...");paint();
  for(int i=0;i<50&&WiFi.status()!=WL_CONNECTED;i++)delay(200);
  netReady=WiFi.status()==WL_CONNECTED;
  if(!netReady){serverReady=false;setStatus("Connexion echouee (W)");return false;}
  setStatus("Wi-Fi connecte / test TLS");
  timeReady();return true;
}
bool connectRememberedWiFi(){
  if(!prefsReady)return false;
  const String previousSsid=wifiSsid,previousPass=wifiPass;
  if(!wifiSsid.isEmpty() && connectSaved())return true;
  for(int slot=0;slot<WIFI_SLOTS;slot++){
    const String ssid=prefs.getString(wifiKey(slot,'s').c_str(),"");
    if(ssid.isEmpty()||ssid==previousSsid)continue;
    wifiSsid=ssid;wifiPass=prefs.getString(wifiKey(slot,'p').c_str(),"");
    if(connectSaved()){
      if(!saveSetting("pwd",wifiPass) || !saveSetting("ssid",wifiSsid)){
        setStatus("Wi-Fi OK, mais non memorise: NVS KO");
      }
      return true;
    }
  }
  wifiSsid=previousSsid;wifiPass=previousPass;
  return false;
}
bool configured(){return serverUrl.startsWith("https://") && !serverUrl.endsWith("/") && deviceToken.length()>=10;}

bool openHttp(HTTPClient& http, WiFiClientSecure& client,const String& path){
  if(WiFi.status()!=WL_CONNECTED){setStatus("Wi-Fi deconnecte");return false;}
  if(!configured()){setStatus("Regler serveur et jeton");return false;}
  if(!timeReady()){setStatus("Horloge non synchronisee");return false;}
  client.setCACert(NEXUS_TRUSTED_ROOTS);
  client.setTimeout(10000);
  http.setTimeout(10000);http.setConnectTimeout(7500);
  if(!http.begin(client,serverUrl+path)){setStatus("Erreur ouverture HTTPS");return false;}
  http.addHeader("Authorization","Bearer "+deviceToken);
  // The same code may be used by both Cardputers; the role is derived
  // from the configured opposite recipient. This is NOT independent authentication.
  http.addHeader("X-Nexus-User",recipient=="bob"?"alice":"bob");
  return true;
}
bool serverTest(){
  if(!configured()) {serverReady=false;setStatus("Configurer serveur et jeton");return false;}
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/session")){serverReady=false;return false;}
  int code=http.GET();String response=code==200?http.getString():"";
  http.end();
  JsonDocument doc;
  const String expected=recipient=="bob"?"alice":"bob";
  serverReady=(code==200 && !deserializeJson(doc,response) && doc["ok"]==true && doc["user"]==expected);
  setStatus(serverReady?"Serveur NEXUS OK":("Serveur inaccessible: "+String(code)));
  return serverReady;
}
// Async Wi-Fi state machine: keyboard processing never waits for association.
void beginWifiAsync(const String& ssid,const String& pass,bool boot){
  if(ssid.isEmpty())return;
  if(wifiScanPending){wifiScanPending=false;WiFi.scanDelete();}
  wifiPreviousSsid=prefsReady?prefs.getString("ssid",""):wifiSsid;
  wifiPreviousPass=prefsReady?prefs.getString("pwd",""):wifiPass;
  wifiAttemptSsid=ssid;wifiAttemptPass=pass;
  wifiAttempt=boot?WifiAttempt::BOOT:WifiAttempt::USER;
  wifiAttemptStartedAt=millis();
  if(WiFi.status()!=WL_CONNECTED || WiFi.SSID()!=ssid){
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false,false);
    WiFi.begin(ssid.c_str(),pass.c_str());
  }
  setStatus("Connexion Wi-Fi en cours, clavier actif");
}
void servicePendingWifi(){
  if(wifiAttempt==WifiAttempt::NONE)return;
  if(WiFi.status()==WL_CONNECTED && WiFi.SSID()==wifiAttemptSsid){
    const bool boot=wifiAttempt==WifiAttempt::BOOT;
    wifiAttempt=WifiAttempt::NONE;
    wifiSsid=wifiAttemptSsid;wifiPass=wifiAttemptPass;netReady=true;serverReady=false;
    const bool changed=wifiSsid!=wifiPreviousSsid;
    if(!boot || changed){
      const bool ok=saveSetting("pwd",wifiPass) &&
        saveSetting("ssid",wifiSsid) && rememberNetwork(wifiSsid,wifiPass);
      setStatus(ok?"Wi-Fi memorise, interface active":"Wi-Fi OK / sauvegarde NVS impossible");
    }else setStatus("Wi-Fi connecte, interface active");
    lastCheck=millis(); // allow loop to run before a background server poll
    if(!boot && (view==View::WIFI_LIST || view==View::WIFI_PASSWORD || view==View::WIFI_CONFIRM))
      changeView(View::HOME);
    else if(view==View::DIAG || view==View::HOME)paint();
    return;
  }
  if(millis()-wifiAttemptStartedAt<9000)return;
  if(wifiAttempt==WifiAttempt::BOOT){
    while(prefsReady && bootWifiSlot<WIFI_SLOTS){
      const int slot=bootWifiSlot++;
      const String ssid=prefs.getString(wifiKey(slot,'s').c_str(),"");
      if(ssid.isEmpty() || ssid==wifiPreviousSsid)continue;
      const String pass=prefs.getString(wifiKey(slot,'p').c_str(),"");
      beginWifiAsync(ssid,pass,true);return;
    }
    wifiAttempt=WifiAttempt::NONE;
    wifiSsid=wifiPreviousSsid;wifiPass=wifiPreviousPass;
    setStatus("Wi-Fi absent: clavier et menus disponibles");
    return;
  }
  wifiAttempt=WifiAttempt::NONE;
  wifiSsid=wifiPreviousSsid;wifiPass=wifiPreviousPass;
  if(!wifiSsid.isEmpty())WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());
  setStatus("Echec de connexion; ancien Wi-Fi conserve");
  if(view==View::WIFI_PASSWORD || view==View::WIFI_LIST)
    changeView(wifiSsid.isEmpty()?View::WIFI_LIST:View::WIFI_CONFIRM);
}
void scanWifi(){
  // Let the user explicitly switch to discovery even during auto-reconnect.
  wifiAttempt=WifiAttempt::NONE;
  scanCount=0;scanPage=0;wifiIndex=0;
  changeView(View::WIFI_LIST);
  setStatus("Recherche Wi-Fi...");paint();
  // Asynchronous scan: never wait for scan results in the keyboard/UI call.
  WiFi.scanDelete();
  wifiScanPending=true;wifiScanStartedAt=millis();
  int started=WiFi.scanNetworks(true,true);
  if(started==WIFI_SCAN_FAILED){
    wifiScanPending=false;
    setStatus("Scan indisponible, reessayer");paint();
  }
}
void serviceWifiScan(){
  if(!wifiScanPending)return;
  int n=WiFi.scanComplete();
  if(n==WIFI_SCAN_RUNNING){
    if(millis()-wifiScanStartedAt>12000){
      wifiScanPending=false;WiFi.scanDelete();
      setStatus("Scan expire: reessayer");paint();
    }
    return;
  }
  wifiScanPending=false;
  if(n<0){WiFi.scanDelete();setStatus("Scan echoue: reessayer");paint();return;}
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
  // Do not call connectSaved/serverTest here: both block the keyboard.
  beginWifiAsync(wifiSsid,newPass,false);
  changeView(View::WIFI_LIST);
}


bool createSDPath(const String& folder) {
  String part="";
  for(size_t i=1;i<=folder.length();i++){
    if(i==folder.length() || folder[i]=='/'){
      const String piece=folder.substring(0,i);
      if(piece.length() && !SD.exists(piece.c_str()) && !SD.mkdir(piece.c_str()))return false;
    }
  }
  return true;
}
String voiceTime(uint32_t created){
  if(created<1700000000UL)return "DATE_INCONNUE";
  time_t sec=(time_t)created+4*3600;struct tm info={};gmtime_r(&sec,&info);
  char b[24];strftime(b,sizeof(b),"%Y-%m-%d_%H-%M-%S",&info);return String(b);
}
String voiceFolder(uint32_t created,const String& sender){
  String p=String(VOICE_ROOT)+"/RECUS";
  if(created<1700000000UL)return p+"/DATE_INCONNUE/"+sender;
  time_t sec=(time_t)created+4*3600;struct tm info={};gmtime_r(&sec,&info);
  char b[16];strftime(b,sizeof(b),"/%Y/%m/%d",&info);return p+String(b)+"/"+sender;
}
String voiceFile(uint32_t id,uint32_t created,const String& sender){
  return voiceFolder(created,sender)+"/"+voiceTime(created)+"_"+sender+"_ID"+String(id)+".wav";
}
bool wavFileValid(const String& path){
  File f=SD.open(path.c_str(),FILE_READ);if(!f)return false;
  size_t sz=f.size();uint8_t b[44];
  bool ok=sz>=44 && sz<=MAX_VOICE && f.read(b,44)==44;f.close();
  if(!ok)return false;
  uint32_t riff=(uint32_t)b[4]|((uint32_t)b[5]<<8)|((uint32_t)b[6]<<16)|((uint32_t)b[7]<<24);
  uint32_t data=(uint32_t)b[40]|((uint32_t)b[41]<<8)|((uint32_t)b[42]<<16)|((uint32_t)b[43]<<24);
  return memcmp(b,"RIFF",4)==0 && memcmp(b+8,"WAVE",4)==0 &&
    memcmp(b+12,"fmt ",4)==0 && memcmp(b+36,"data",4)==0 &&
    riff==sz-8 && data==sz-44 &&
    b[20]==1 && b[21]==0 && b[22]==1 && b[23]==0 &&
    b[24]==0x80 && b[25]==0x3e && b[26]==0 && b[27]==0 &&
    b[34]==16 && b[35]==0;
}
String archivedPathById(uint32_t id){
  if(!sdReady || !SD.exists(VOICE_INDEX))return "";
  File f=SD.open(VOICE_INDEX,FILE_READ);if(!f)return "";
  const String prefix=String(id)+"|";String found="";
  while(f.available()){
    String line=f.readStringUntil('\n');line.trim();
    if(!line.startsWith(prefix))continue;
    const int last=line.lastIndexOf('|');
    if(last>0)found=line.substring(last+1);
  }f.close();return found;
}
bool writeArchiveIndex(uint32_t id,uint32_t created,const String& sender,const String& path){
  if(archivedPathById(id).length())return true;
  File f=SD.open(VOICE_INDEX,FILE_APPEND);if(!f)return false;
  const String line=String(id)+"|"+sender+"|"+voiceTime(created)+"|"+path+"\n";
  bool ok=f.print(line)==line.length();f.flush();f.close();return ok;
}
bool saveVoiceArchive(uint32_t id,const String& sender,uint32_t created){
  if(!id || !sdReady || (sender!="alice" && sender!="bob"))return false;
  const String path=voiceFile(id,created,sender);
  if(!createSDPath(voiceFolder(created,sender)))return false;
  if(wavFileValid(path))return writeArchiveIndex(id,created,sender,path);
  if(SD.exists(path.c_str()))return false; // never overwrite damaged files
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/voice/"+String(id)))return false;
  const int code=http.GET(),sz=http.getSize();
  if(code!=200 || sz<44 || sz>(int)MAX_VOICE){http.end();return false;}
  const String temp=path+".part";
  SD.remove(temp.c_str());
  File out=SD.open(temp.c_str(),FILE_WRITE);
  if(!out){http.end();return false;}
  uint8_t buffer[1024];int got=0;uint32_t progress=millis();bool ok=true;
  WiFiClient* network=http.getStreamPtr();
  while(got<sz && millis()-progress<12000){
    const int available=network->available();
    if(available>0){
      const size_t count=min((size_t)available,min(sizeof(buffer),(size_t)(sz-got)));
      const int n=network->readBytes(buffer,count);
      if(n<=0 || out.write(buffer,n)!=(size_t)n){ok=false;break;}
      got+=n;progress=millis();
    }else delay(5);
  }
  out.flush();out.close();http.end();
  if(!ok || got!=sz || !wavFileValid(temp)){
    SD.remove(temp.c_str());return false;
  }
  if(!SD.rename(temp.c_str(),path.c_str())){SD.remove(temp.c_str());return false;}
  return writeArchiveIndex(id,created,sender,path);
}
void listVoiceArchives(){
  voiceCount=0;voiceSelection=0;
  if(!sdReady)return;
  File f=SD.open(VOICE_INDEX,FILE_READ);if(!f)return;
  while(f.available()){
    String line=f.readStringUntil('\n');line.trim();
    int a=line.indexOf('|'),b=line.indexOf('|',a+1),c=line.indexOf('|',b+1);
    if(a<1 || b<=a || c<=b)continue;
    VoiceArchive entry;
    entry.id=(uint32_t)line.substring(0,a).toInt();
    entry.sender=line.substring(a+1,b);entry.stamp=line.substring(b+1,c);
    entry.path=line.substring(c+1);
    if(!entry.id || !entry.path.startsWith(String(VOICE_ROOT)+"/RECUS/"))continue;
    if(voiceCount==VOICE_LIST_MAX){
      for(int j=1;j<VOICE_LIST_MAX;j++)voiceArchives[j-1]=voiceArchives[j];
      --voiceCount;
    }
    voiceArchives[voiceCount++]=entry;
  }f.close();
}

bool archiveText(uint32_t id,const String& from,uint32_t created,
                 const String& body,bool outgoing){
  if(!sdReady || !id)return false;
  String folder=voiceFolder(created,from);
  folder.replace(VOICE_ROOT,TEXT_ROOT);
  if(outgoing)folder.replace("/RECUS/","/ENVOYES/");
  if(!createSDPath(folder))return false;
  const String path=folder+"/"+voiceTime(created)+"_"+from+"_ID"+String(id)+".txt";
  if(SD.exists(path.c_str()))return true;
  const String temp=path+".part";
  SD.remove(temp.c_str());
  File f=SD.open(temp.c_str(),FILE_WRITE);
  if(!f)return false;
  String header="NEXUS TEXTE\nID: "+String(id)+"\nDe: "+from+
    "\nDate: "+voiceTime(created)+" (Reunion)\nDirection: "+
    String(outgoing?"envoye":"recu")+"\n\n";
  const size_t expected=header.length()+body.length()+1;
  bool ok=f.print(header)==header.length();
  if(ok)ok=f.print(body)==body.length();
  if(ok)ok=f.print("\n")==1;
  f.flush();f.close();
  if(!ok){SD.remove(temp.c_str());return false;}
  File verify=SD.open(temp.c_str(),FILE_READ);
  const bool valid=verify && verify.size()==expected;
  if(verify)verify.close();
  if(!valid){SD.remove(temp.c_str());return false;}
  if(!SD.rename(temp.c_str(),path.c_str())){SD.remove(temp.c_str());return false;}
  return true;
}

bool applyUnreadPayload(const String& payload){
  JsonDocument doc;if(deserializeJson(doc,payload))return false;
  const uint32_t n=doc["unread"]|0u;
  const uint32_t id=doc["latest"]["id"]|0u;
  const String from=doc["latest"]["sender"]|"";
  const String preview=doc["latest"]["preview"]|"";
  const bool changed=unreadCount!=n || latestUnreadId!=id;
  const bool notify=unreadSynced && id>notifiedMessageId && n>0 && notificationSound;
  if(unreadCount!=n && prefsReady)prefs.putUInt("unread",n);
  unreadCount=n;latestUnreadId=id;
  latestUnreadSender=from;latestUnreadPreview=clipped(preview,48);
  unreadSynced=true;
  if(id>notifiedMessageId){
    notifiedMessageId=id;
    if(prefsReady)prefs.putUInt("notified",id);
  }
  if(notify){
    M5Cardputer.Mic.end();M5Cardputer.Speaker.setVolume(48);
    if(M5Cardputer.Speaker.begin()){
      M5Cardputer.Speaker.tone(950,85);delay(105);
      M5Cardputer.Speaker.stop();M5Cardputer.Speaker.end();
    }
  }
  if(changed && (view==View::HOME || view==View::SECTION))paint();
  return true;
}
bool refreshUnread(){
  if(!configured() || WiFi.status()!=WL_CONNECTED)return false;
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/unread"))return false;
  const int code=http.GET();const String payload=code==200?http.getString():"";
  http.end();
  if(code!=200)return false; // Old server still supports other features.
  return applyUnreadPayload(payload);
}
void applyInboxPayload(const String& payload){
  const uint32_t oldCursor=cursor, oldVoiceId=lastVoiceId;
  JsonDocument doc;
  if(deserializeJson(doc,payload)){setStatus("Reponse invalide");return;}
  JsonArray messages=doc["messages"].as<JsonArray>();
  for(JsonObject m:messages){
    uint32_t id=m["id"]|0u;if(id<=cursor)continue;
    String sender=m["sender"]|"?",kind=m["kind"]|"?";
    String text=kind=="voice"?"Message vocal #"+String(id):m["text"].as<String>();
    // The message cursor cannot move past a voice before the WAV is on microSD.
    // Failure retries on the next poll instead of silently losing the archive.
    if(sdReady){
      const uint32_t created=m["created"]|0u;
      if(kind=="voice" && !saveVoiceArchive(id,sender,created)){
        setStatus("Vocal non archive: verifier microSD / reseau");
        break;
      }
      if(kind=="text" && !archiveText(id,sender,created,text,false)){
        setStatus("Texte non archive: microSD indisponible");
        break;
      }
    }
    if(!remember(id,sender,kind,text)) {
      setStatus("microSD: historique non sauvegarde");
      break; // Do not advance persistent cursor and silently lose a message.
    }
    if(kind=="voice")lastVoiceId=id;
    cursor=id;
    // Without microSD, keep cursor volatile: missed items are replayed on reboot.
    setStatus("Nouveau message de "+sender);
  }
  // Persist only once after the batch to reduce NVS wear and FULL errors.
  if(sdReady && prefsReady && cursor!=oldCursor){
    if(prefs.putUInt("cursor",cursor)!=sizeof(uint32_t)){
      storageFault=true;setStatus("NVS pleine: curseur non sauvegarde");
    }
  }
  if(prefsReady && lastVoiceId!=oldVoiceId){
    if(prefs.putUInt("voice",lastVoiceId)!=sizeof(uint32_t))
      storageFault=true;
  }
}
void pollInbox(){
  if(!configured()||WiFi.status()!=WL_CONNECTED)return;
  WiFiClientSecure client;HTTPClient http;
  if(!openHttp(http,client,"/messages?after="+String(cursor)))return;
  int code=http.GET();if(code!=200){http.end();setStatus("Reception HTTP "+String(code));serverReady=false;return;}
  String payload=http.getString();http.end();
  applyInboxPayload(payload);
}
// No HTTPS request runs in the UI loop. The worker owns network objects
// and passes immutable completed responses back to the UI through a queue.
void nexusNetworkWorker(void* argument){
  NetRequest* request=static_cast<NetRequest*>(argument);
  NetReply* reply=new NetReply();
  if(!reply){delete request;vTaskDelete(nullptr);return;}
  reply->readId=request->readId;
  reply->kind=request->kind;
  reply->clientId=request->clientId;
  reply->sentText=request->text;
  reply->recipient=request->recipient;
  if(WiFi.status()==WL_CONNECTED && request->url.startsWith("https://")){
    if(time(nullptr)<1700000000){
      configTime(0,0,"pool.ntp.org","time.google.com");
      for(int i=0;i<40 && time(nullptr)<1700000000;i++)vTaskDelay(pdMS_TO_TICKS(100));
    }
    auto networkCall=[&](const String& path,int& status,String* content,bool post=false){
      WiFiClientSecure client;HTTPClient http;
      client.setCACert(NEXUS_TRUSTED_ROOTS);
      client.setTimeout(4500);
      http.setConnectTimeout(3500);http.setTimeout(4500);
      if(!http.begin(client,request->url+path)){status=-1;return;}
      http.addHeader("Authorization","Bearer "+request->token);
      http.addHeader("X-Nexus-User",request->recipient=="bob"?"alice":"bob");
      status=post?http.POST((uint8_t*)nullptr,0):http.GET();
      if(content && status==200 && http.getSize()<=16384)
        *content=http.getString();
      http.end();
    };
    if(request->kind==NetRequest::Kind::SEND_TEXT){
      WiFiClientSecure client;HTTPClient http;
      client.setCACert(NEXUS_TRUSTED_ROOTS);client.setTimeout(5000);
      http.setConnectTimeout(4000);http.setTimeout(5000);
      if(http.begin(client,request->url+"/messages")){
        http.addHeader("Authorization","Bearer "+request->token);
        http.addHeader("X-Nexus-User",request->recipient=="bob"?"alice":"bob");
        http.addHeader("Content-Type","application/json");
        reply->sendCode=http.POST((uint8_t*)request->payload.c_str(),request->payload.length());
        if(reply->sendCode>=200 && reply->sendCode<300 && http.getSize()<=2048)
          reply->sendResponse=http.getString();
        http.end();
      }
    }else{
      networkCall("/session",reply->sessionCode,&reply->session);
      if(reply->sessionCode==200){
        networkCall("/messages?after="+String(request->cursor),reply->inboxCode,&reply->inbox);
        networkCall("/unread",reply->unreadCode,&reply->unread);
        if(request->readId)
          networkCall("/messages/"+String(request->readId)+"/read",reply->readCode,nullptr,true);
      }
    }
  }
  delete request;
  if(!netReplyQueue || xQueueSend(netReplyQueue,&reply,0)!=pdTRUE)delete reply;
  vTaskDelete(nullptr);
}
void serviceNetworkReplies(){
  if(!netReplyQueue)return;
  NetReply* reply=nullptr;
  if(xQueueReceive(netReplyQueue,&reply,0)!=pdTRUE)return;
  netWorkerBusy=false;
  if(!reply)return;
  if(reply->kind==NetRequest::Kind::SEND_TEXT){
    JsonDocument response;
    const bool received=reply->sendCode>=200 && reply->sendCode<300 &&
        !deserializeJson(response,reply->sendResponse) && (response["id"]|0u)>0;
    const uint32_t id=received?(response["id"]|0u):0u;
    if(received && pendingTextId==reply->clientId){
      const String from=reply->recipient=="bob"?"alice":"bob";
      const bool archived=!sdReady || archiveText(id,from,(uint32_t)time(nullptr),reply->sentText,true);
      // Only clear an editor that matches the successfully acknowledged payload.
      if(editor==reply->sentText){editor="";draftDirty=false;if(prefsReady)prefs.remove("draft");}
      pendingTextId="";if(prefsReady)prefs.remove("txtid");
      setStatus(archived?"Message envoye et confirme":"Envoye, archive microSD non sauvegardee");
      lastSentInfo="Message #"+String(id)+" vers "+reply->recipient;
      if(view==View::SEND_PROGRESS)changeView(View::SEND_RESULT);
    }else {
      setStatus("Envoi non confirme (HTTP "+String(reply->sendCode)+") : reessayer");
      if(view==View::SEND_PROGRESS)changeView(View::SEND_RESULT);
    }
    delete reply;return;
  }
  JsonDocument session;
  const String expected=recipient=="bob"?"alice":"bob";
  serverReady=reply->sessionCode==200 && !deserializeJson(session,reply->session)
    && session["ok"]==true && session["user"]==expected;
  if(serverReady){
    if(reply->inboxCode==200 && reply->inbox.length())applyInboxPayload(reply->inbox);
    if(reply->unreadCode==200 && reply->unread.length())applyUnreadPayload(reply->unread);
    if(reply->readId && reply->readCode==200 && pendingReadId==reply->readId)
      pendingReadId=0;
  } else setStatus("Serveur indisponible; menus accessibles");
  if(view==View::DIAG)paint();
  delete reply;
}
void scheduleNetworkPoll(){
  if(netWorkerBusy || textSendQueued || wifiScanPending || wifiAttempt!=WifiAttempt::NONE ||
     !netReplyQueue || !configured() || WiFi.status()!=WL_CONNECTED)return;
  if(ESP.getFreeHeap()<35000){setStatus("RAM faible: requete reportee");return;}
  NetRequest* request=new NetRequest();
  if(!request){setStatus("RAM insuffisante");return;}
  request->url=serverUrl;request->token=deviceToken;request->recipient=recipient;
  request->cursor=cursor;request->readId=pendingReadId;
  netWorkerBusy=true;
  if(xTaskCreatePinnedToCore(nexusNetworkWorker,"nexus_https",12288,request,1,nullptr,0)!=pdPASS){
    netWorkerBusy=false;delete request;
    setStatus("Tache HTTPS non disponible");
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
  else refreshUnread();
}
// Send requests use the existing FreeRTOS HTTPS worker rather than the UI loop.
void sendText(){
  editor.trim();
  if(editor.isEmpty()){setStatus("Ecris un message avant l'envoi");changeView(View::COMPOSE);return;}
  if(!configured()){setStatus("Configurer serveur et jeton");changeView(View::COMPOSE);return;}
  if(WiFi.status()!=WL_CONNECTED){setStatus("Wi-Fi absent : brouillon conserve");changeView(View::COMPOSE);return;}
  if(textSendQueued){setStatus("Un envoi est deja programme");return;}
  if(pendingTextId.isEmpty()){
    const String id=nonce();
    if(!prefsReady){setStatus("NVS indisponible : envoi non securise");changeView(View::COMPOSE);return;}
    prefs.putString("txtid",id);
    if(prefs.getString("txtid","")!=id){
      storageFault=true;setStatus("NVS pleine : identifiant non sauvegarde");
      changeView(View::COMPOSE);return;
    }
    pendingTextId=id;
  }
  textSendQueued=true;
  setStatus("Envoi HTTPS en cours...");
  changeView(View::SEND_PROGRESS);
}
void tryStartTextSend(){
  if(!textSendQueued || netWorkerBusy)return;
  if(WiFi.status()!=WL_CONNECTED){textSendQueued=false;setStatus("Wi-Fi perdu, texte garde");changeView(View::SEND_RESULT);return;}
  if(ESP.getFreeHeap()<35000){textSendQueued=false;setStatus("RAM insuffisante : texte garde");changeView(View::SEND_RESULT);return;}
  NetRequest* request=new NetRequest();
  if(!request){textSendQueued=false;setStatus("Memoire indisponible");changeView(View::SEND_RESULT);return;}
  request->kind=NetRequest::Kind::SEND_TEXT;
  request->url=serverUrl;request->token=deviceToken;request->recipient=recipient;
  request->text=editor;request->clientId=pendingTextId;
  JsonDocument payload;payload["to"]=recipient;payload["text"]=editor;payload["client_id"]=pendingTextId;
  serializeJson(payload,request->payload);
  netWorkerBusy=true;textSendQueued=false;
  if(xTaskCreatePinnedToCore(nexusNetworkWorker,"nexus_send",12288,request,1,nullptr,0)!=pdPASS){
    netWorkerBusy=false;delete request;
    setStatus("Tache HTTPS indisponible : reessayer");
    changeView(View::SEND_RESULT);
  }
}

void writeLE16(File& f,uint16_t v){uint8_t a[2]={(uint8_t)v,(uint8_t)(v>>8)};f.write(a,2);}
void writeLE32(File& f,uint32_t v){uint8_t a[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)};f.write(a,4);}
void writeWavHeader(File& f,uint32_t samples){
  uint32_t bytes=samples*2;
  f.write((const uint8_t*)"RIFF",4);writeLE32(f,bytes+36);f.write((const uint8_t*)"WAVEfmt ",8);
  writeLE32(f,16);writeLE16(f,1);writeLE16(f,1);writeLE32(f,AUDIO_RATE);
  writeLE32(f,AUDIO_RATE*2);writeLE16(f,2);writeLE16(f,16);f.write((const uint8_t*)"data",4);writeLE32(f,bytes);
}
void recordVoice(bool replaceOld=false){
  if(!sdReady){setStatus("Carte microSD absente : vocal impossible");paint();return;}
  // An earlier recording must remain available until the user chooses to replace it.
  if(!replaceOld && SD.exists(TX_AUDIO)){
    voiceChoice=0;setStatus("Brouillon vocal present : choisir une action");
    changeView(View::VOICE_READY);return;
  }
  if(!replaceOld && pendingVoiceId.length() && !SD.exists(TX_AUDIO)){
    pendingVoiceId="";if(prefsReady)prefs.remove("vocid"); // stale ID only
  }
  SD.remove(NEW_TX_AUDIO);
  File f=SD.open(NEW_TX_AUDIO,FILE_WRITE);
  if(!f){setStatus("Impossible de creer WAV sur microSD");paint();return;}
  writeWavHeader(f,AUDIO_FRAME*AUDIO_FRAMES);
  if(f.position()!=44){
    f.close();SD.remove(NEW_TX_AUDIO);setStatus("Erreur entete WAV : SD");paint();return;
  }
  M5Cardputer.Speaker.end(); // exclusive I2S with microphone in M5Unified
  M5Cardputer.Mic.end(); // clear any interrupted capture from a previous attempt
  if(!M5Cardputer.Mic.begin()){
    f.close();SD.remove(NEW_TX_AUDIO);setStatus("Micro ES8311 non initialise");paint();return;
  }
  voiceRecordedSamples=0;changeView(View::VOICE_RECORDING);
  delay(1150); // ES8311 ADC analog warm-up, not a UI/network wait
  alignas(4) int16_t audio[AUDIO_FRAME];
  bool failed=false,cancelled=false,allZero=true;uint32_t samples=0;
  for(size_t i=0;i<AUDIO_FRAMES;i++){
    if(!M5Cardputer.Mic.record(audio,AUDIO_FRAME,AUDIO_RATE,false)){
      failed=true;setStatus("Micro : capture refusee (I2S)");break;
    }
    const uint32_t beginAt=millis();
    while(M5Cardputer.Mic.isRecording() && millis()-beginAt<1600){
      M5Cardputer.update();
      if(M5Cardputer.Keyboard.keysState().opt){cancelled=true;break;}
      delay(2);
    }
    if(cancelled)break;
    if(M5Cardputer.Mic.isRecording()){
      failed=true;setStatus("Micro : capture trop lente");break;
    }
    for(int j=0;j<AUDIO_FRAME;j++)if(audio[j]!=0){allZero=false;break;}
    if(f.write((uint8_t*)audio,sizeof(audio))!=sizeof(audio)){
      failed=true;setStatus("Carte microSD : ecriture interrompue");break;
    }
    samples+=AUDIO_FRAME;voiceRecordedSamples=samples;
    if(i%8==7)paint(); // visible recording progress every ~0.5 s
  }
  M5Cardputer.Mic.end();f.flush();f.close();
  if(cancelled || failed || samples!=AUDIO_FRAME*AUDIO_FRAMES || allZero){
    SD.remove(NEW_TX_AUDIO);
    if(cancelled)setStatus("Enregistrement annule : ancien vocal conserve");
    else if(allZero && !failed)setStatus("Micro muet : verifier codec ES8311");
    else if(!failed)setStatus("Enregistrement incomplet");
    paint();return;
  }
  File verify=SD.open(NEW_TX_AUDIO,FILE_READ);
  const bool valid=verify && verify.size()==44+2*samples;
  if(verify)verify.close();
  if(!valid){
    SD.remove(NEW_TX_AUDIO);setStatus("MicroSD : fichier WAV incomplet");paint();return;
  }
  // Transactional replacement: preserve the old voice until the new one is valid.
  SD.remove(OLD_TX_AUDIO);
  const bool hadOld=SD.exists(TX_AUDIO);
  if(hadOld && !SD.rename(TX_AUDIO,OLD_TX_AUDIO)){
    SD.remove(NEW_TX_AUDIO);setStatus("Impossible de proteger l'ancien vocal");paint();return;
  }
  if(!SD.rename(NEW_TX_AUDIO,TX_AUDIO)){
    if(hadOld)SD.rename(OLD_TX_AUDIO,TX_AUDIO);
    SD.remove(NEW_TX_AUDIO);
    setStatus("Impossible de finaliser WAV, ancien conserve");paint();return;
  }
  if(hadOld)SD.remove(OLD_TX_AUDIO);
  pendingVoiceId=nonce();
  if(prefsReady){
    prefs.putString("vocid",pendingVoiceId);
    if(prefs.getString("vocid","")!=pendingVoiceId){
      storageFault=true;setStatus("Vocal pret, NVS pleine : ID temporaire");
    }else setStatus("Vocal de 3 s enregistre !");
  }else setStatus("Vocal pret, memoire NVS indisponible");
  voiceChoice=0;changeView(View::VOICE_READY);
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

void playArchiveWav(const String& path){
  if(!sdReady || !wavFileValid(path)){
    setStatus("Vocal absent ou fichier WAV invalide");paint();return;
  }
  File f=SD.open(path.c_str(),FILE_READ);
  if(!f){setStatus("Erreur ouverture archive");paint();return;}
  f.seek(44);M5Cardputer.Mic.end();
  M5Cardputer.Speaker.setVolume(95);
  if(!M5Cardputer.Speaker.begin()){f.close();setStatus("Haut-parleur indisponible");paint();return;}
  setStatus("Lecture archive microSD");paint();
  alignas(4) int16_t samples[1024];bool ok=true;
  while(f.available()){
    size_t bytes=f.read((uint8_t*)samples,sizeof(samples));bytes&=~(size_t)1;
    if(!bytes)break;
    if(!M5Cardputer.Speaker.playRaw(samples,bytes/2,AUDIO_RATE,false,1,0)){ok=false;break;}
    uint32_t t=millis();
    while(M5Cardputer.Speaker.isPlaying()&&millis()-t<1400)delay(2);
    if(M5Cardputer.Speaker.isPlaying()){M5Cardputer.Speaker.stop();ok=false;break;}
  }
  f.close();M5Cardputer.Speaker.end();
  setStatus(ok?"Lecture archive terminee":"Erreur lecture microSD");paint();
}
void playMorseAudio(const String& encoded){
  if(encoded.isEmpty()){setStatus("Texte non compatible Morse");return;}
  M5Cardputer.Mic.end();M5Cardputer.Speaker.setVolume(75);
  if(!M5Cardputer.Speaker.begin()){setStatus("Haut-parleur indisponible");return;}
  const uint32_t unit=1200u/morseWpm; // standard PARIS timing, one dit = 1200/WPM ms
  bool aborted=false;
  for(size_t i=0;i<encoded.length();i++){
    const char c=encoded[i];
    if(c=='.' || c=='-'){
      const uint32_t duration=(c=='.'?unit:3*unit);
      if(!M5Cardputer.Speaker.tone(880,duration)){aborted=true;break;}
      delay(duration+15);
      M5Cardputer.Speaker.stop();delay(unit);
    }else if(c=='/'){delay(4*unit);}
    else if(c==' '){delay(2*unit);}
    M5Cardputer.update();
    if(M5Cardputer.Keyboard.keysState().opt){aborted=true;break;}
  }
  M5Cardputer.Speaker.end();
  setStatus(aborted?"Morse arrete":"Lecture Morse terminee");
}
void decodeMorseMicrophone(){
  // Experimental clean-tone decoder: 880 Hz bursts with approx. 120ms dots.
  // It does not attempt full-band speech recognition or decode arbitrary radios.
  M5Cardputer.Speaker.end();
  if(!M5Cardputer.Mic.begin()){
    morseOutput="Micro indisponible";changeView(View::MORSE_RESULT);return;
  }
  delay(900);
  constexpr int chunk=256, chunks=320;
  static float energy[chunks];
  int16_t samples[chunk];
  morseOutput="";setStatus("Ecoute Morse (environ 5 secondes)");paint();
  float maxEnergy=0;
  const float coeff=2*cosf(2*3.141592653589793f*880/AUDIO_RATE);
  bool good=true;int n=0;
  for(int k=0;k<chunks;k++){
    if(!M5Cardputer.Mic.record(samples,chunk,AUDIO_RATE,false)){good=false;break;}
    uint32_t t=millis();
    while(M5Cardputer.Mic.isRecording() && millis()-t<700)delay(1);
    if(M5Cardputer.Mic.isRecording()){good=false;break;}
    // Goertzel band energy around an 880Hz reference tone.
    float s0=0,s1=0,s2=0;
    for(int i=0;i<chunk;i++){
      s0=samples[i]/32768.0f+coeff*s1-s2;
      s2=s1;s1=s0;
    }
    const float power=s1*s1+s2*s2-coeff*s1*s2;
    energy[n++]=power;
    if(power>maxEnergy)maxEnergy=power;
  }
  M5Cardputer.Mic.end();
  if(!good || n<60 || maxEnergy<0.4f){
    morseOutput="Signal faible ou micro indisponible";changeView(View::MORSE_RESULT);return;
  }
  // Consecutive 16ms windows are classified against peak energy.
  const float threshold=maxEnergy*0.21f;
  String pattern,output;bool previous=false;int length=0;
  bool started=false;
  for(int i=0;i<=n;i++){
    const bool on=i<n && energy[i]>threshold;
    if(i==0){previous=on;length=1;continue;}
    if(i<n && on==previous){length++;continue;}
    const uint32_t ms=(uint32_t)length*16;
    if(previous){
      if(ms>=56){pattern+=ms<220?".":"-";started=true;}
    }else if(started){
      if(ms>=760){
        if(pattern.length()){output+=NexusMorse::decodeSymbol(pattern);pattern="";}
        if(output.length()&&!output.endsWith(" "))output+=" ";
      }else if(ms>=280){
        if(pattern.length()){output+=NexusMorse::decodeSymbol(pattern);pattern="";}
      }
    }
    previous=on;length=1;
  }
  if(pattern.length())output+=NexusMorse::decodeSymbol(pattern);
  output.trim();
  morseOutput=output.length()?output:"Aucun Morse reconnu";
  setStatus("Decodage beta: tone 880Hz, bruit faible");
  changeView(View::MORSE_RESULT);
}

void measureBattery(){
  // Cardputer ADV: voltage and voltage-derived percentage, not precision state-of-charge.
  int values[7],n=0;
  for(int k=0;k<7;k++){
    int sample=M5Cardputer.Power.getBatteryVoltage();
    if(sample>=2800 && sample<=4500)values[n++]=sample;
    delay(12);
  }
  if(n){
    for(int i=1;i<n;i++){int t=values[i],j=i-1;while(j>=0&&values[j]>t){values[j+1]=values[j];j--;}values[j+1]=t;}
    const int median=values[n/2];
    batteryVoltageSmooth=batteryVoltageSmooth<0?median:(batteryVoltageSmooth*3+median)/4;
  }else batteryVoltageSmooth=-1;
  const int percent=M5Cardputer.Power.getBatteryLevel();
  if(percent>=0&&percent<=100)
    batteryLevelSmooth=batteryLevelSmooth<0?percent:(batteryLevelSmooth*3+percent)/4;
  else batteryLevelSmooth=-1;
  batterySampleAt=millis();
}
bool textFileMatch(const String& path,const String& keyword){
  File f=SD.open(path.c_str(),FILE_READ);
  if(!f)return false;
  const size_t size=f.size();
  if(size==0||size>4096){f.close();return false;}
  String data=f.readString();f.close();data.toLowerCase();
  return data.indexOf(keyword)>=0;
}
void scanTextFolder(const String& directory,const String& keyword,int depth){
  if(depth>9||searchScanned>=12000||!sdReady)return;
  File folder=SD.open(directory.c_str(),FILE_READ);
  if(!folder || !folder.isDirectory()){if(folder)folder.close();return;}
  File item=folder.openNextFile();
  while(item && searchScanned<12000){
    String path=String(item.path());
    const bool folderItem=item.isDirectory();
    item.close();
    if(path.startsWith(String(TEXT_ROOT)+"/")){
      if(folderItem)scanTextFolder(path,keyword,depth+1);
      else if(path.endsWith(".txt")){
        searchScanned++;
        if(textFileMatch(path,keyword)){
          searchMatches++;
          if(searchSize==SEARCH_MAX){
            for(int i=1;i<SEARCH_MAX;i++)searchPaths[i-1]=searchPaths[i];
            searchSize--;
          }
          searchPaths[searchSize++]=path;
        }
      }
    }
    item=folder.openNextFile();
  }
  if(item)item.close();
  folder.close();
  if(searchScanned>=12000)searchLimited=true;
}
void searchTextArchive(){
  searchSize=searchSelection=searchPage=0;
  searchMatches=searchScanned=0;searchLimited=false;
  searchQuery=input;searchQuery.trim();
  if(!sdReady){setStatus("Inserer microSD pour chercher");changeView(View::SECTION);return;}
  if(searchQuery.length()<2){setStatus("Mot de recherche trop court");paint();return;}
  String keyword=searchQuery;keyword.toLowerCase();
  scanTextFolder(TEXT_ROOT,keyword,0);
  setStatus(String(searchMatches)+" resultat(s), "+String(searchScanned)+" fichiers"+(searchLimited?" (limite)":""));
  changeView(View::SEARCH_RESULTS);
}
void openArchiveText(){
  if(searchSelection>=searchSize)return;
  File f=SD.open(searchPaths[searchSelection].c_str(),FILE_READ);
  if(!f){setStatus("Archive introuvable");paint();return;}
  if(f.size()>4096){f.close();setStatus("Archive trop grande");paint();return;}
  searchViewBody=f.readString();f.close();
  searchPage=0;changeView(View::SEARCH_VIEW);
}

void openSectionSelection(){
  if(homeIndex==0){
    switch(sectionIndex){
      case 0:pageHistory=0;textPage=0;changeView(View::HISTORY);
        // Show cached messages instantly; defer server acknowledgement.
        if(historySize)pendingReadId=history[historySize-1].id;
        break;
      case 1:changeView(View::COMPOSE);break;
      case 2:changeView(View::SENT);refreshSent();break;
    }
  }else if(homeIndex==1){
    switch(sectionIndex){
      case 0:recordVoice();break;
      case 1:uploadVoice();break;
      case 2:listVoiceArchives();changeView(View::VOICE_ARCHIVE);break;
      case 3:playVoice();break;
      case 4:changeView(View::DISCARD_VOICE);break;
    }
  }else if(homeIndex==2){
    if(sectionIndex==2){decodeMorseMicrophone();}
    else if(sectionIndex==3){
      morseQuizLetter=(char)('A'+(esp_random()%26));
      morseQuizAnswered=false;morseQuizAnswer="";
      changeView(View::MORSE_TRAIN);
      playMorseAudio(NexusMorse::encode(String(morseQuizLetter)));
      paint();
    }else if(sectionIndex==4){
      morseWpm=(uint8_t)(morseWpm>=30?5:morseWpm+1);
      prefs.putUInt("morsewpm",morseWpm);
      setStatus("Morse: "+String(morseWpm)+" mots/min");paint();
    }else{morseMode=sectionIndex;input="";changeView(View::MORSE_EDIT);}
  }else if(homeIndex==3){
    if(sectionIndex==0)wifiSetup();
    else changeView(View::DIAG);
  }else if(homeIndex==4){
    switch(sectionIndex){
      case 0:configIndex=0;changeView(View::CONFIG);break;
      case 1:changeView(View::DIAG);break;
      case 2:notificationSound=!notificationSound;
        if(prefsReady)prefs.putBool("beep",notificationSound);
        setStatus(notificationSound?"Notification sonore activee":"Notification sonore coupee");paint();break;
      case 3:initSd();setStatus(sdReady?"microSD reconnue":"microSD absente");paint();break;
    }
  }else{
    if(sectionIndex==0){searchQuery="";input="";changeView(View::SEARCH_EDIT);}
    else if(sectionIndex==1){measureBattery();changeView(View::BATTERY_INFO);}
    else changeView(View::STORAGE_INFO);
  }
}

void handleInput(){
  // In menus the physical ; . , / keys work as arrows WITHOUT Fn.
  // Text/password editors retain their normal punctuation keys.
  // Fn+arrows remain supported. Change detection includes direction keys.
  static String lastChord="";
  if(!M5Cardputer.Keyboard.isPressed()){lastChord="";return;}
  const auto k=M5Cardputer.Keyboard.keysState();
  // M5Cardputer 1.1.1 has only fn/opt/del/enter and printable keys.
  const bool escPressed=k.opt || (k.fn && M5Cardputer.Keyboard.isKeyPressed('`'));
  const bool upPressed=M5Cardputer.Keyboard.isKeyPressed(';');
  const bool downPressed=M5Cardputer.Keyboard.isKeyPressed('.');
  const bool leftPressed=M5Cardputer.Keyboard.isKeyPressed(',');
  const bool rightPressed=M5Cardputer.Keyboard.isKeyPressed('/');
  String chord=String(k.fn?"F":"f")+String(k.shift?"S":"s")+
    String(k.enter?"E":"")+String(k.del?"D":"")+
    String(k.opt?"O":"")+
    String(upPressed?"U":"")+String(downPressed?"V":"")+
    String(leftPressed?"L":"")+String(rightPressed?"R":"");
  for(char ch:k.word)chord+=ch;
  if(chord==lastChord)return;
  lastChord=chord;
  const bool editing=(view==View::WIFI_PASSWORD || view==View::EDIT_URL ||
    view==View::EDIT_TOKEN || view==View::EDIT_PEER || view==View::COMPOSE ||
    view==View::MORSE_EDIT || view==View::SEARCH_EDIT);
  // Comma and slash must remain typeable in text, URLs and Wi-Fi passwords.
  if(escPressed || (leftPressed && !editing && view!=View::HISTORY && view!=View::HOME && view!=View::SEARCH_VIEW)){
    if(view==View::WIFI_PASSWORD || view==View::WIFI_LIST || view==View::WIFI_CONFIRM){
      wifiSsid=prefs.getString("ssid","");wifiPass=prefs.getString("pwd","");
      changeView(View::HOME);
    }else if(view==View::EDIT_URL || view==View::EDIT_TOKEN || view==View::EDIT_PEER){
      input="";changeView(View::CONFIG);
    }else if(view==View::DELETE_CONFIRM){changeView(View::WIFI_CONFIRM);}
    else if(view==View::SEARCH_VIEW)changeView(View::SEARCH_RESULTS);
    else if(view==View::SEND_CONFIRM)changeView(View::COMPOSE);
    else if(view==View::SEND_PROGRESS)changeView(View::SECTION);
    else if(view==View::SEND_RESULT)changeView(editor.isEmpty()?View::SECTION:View::COMPOSE);
    else if(view==View::MORSE_RESULT || view==View::MORSE_EDIT || view==View::MORSE_TRAIN ||
            view==View::VOICE_READY || view==View::VOICE_ARCHIVE || view==View::STORAGE_INFO || view==View::BATTERY_INFO ||
            view==View::SEARCH_EDIT || view==View::SEARCH_RESULTS)changeView(View::SECTION);
    else if(view==View::SECTION)changeView(View::HOME);
    else{changeView(View::SECTION);}
    return;
  }
  const bool choose=k.enter||(rightPressed && view!=View::HOME);
  if(view==View::HOME){
    bool moved=false;
    if(leftPressed){homeIndex=(homeIndex+HOME_COUNT-1)%HOME_COUNT;moved=true;}
    if(rightPressed){homeIndex=(homeIndex+1)%HOME_COUNT;moved=true;}
    if(k.enter){sectionIndex=0;changeView(View::SECTION);return;}
    if(moved)paint();return;
  }
  if(view==View::SECTION){
    String options[6];const int count=sectionOptions(homeIndex,options);
    bool moved=false;
    if(upPressed){sectionIndex=(sectionIndex+count-1)%count;moved=true;}
    if(downPressed){sectionIndex=(sectionIndex+1)%count;moved=true;}
    if(choose){openSectionSelection();return;}
    if(moved)paint();return;
  }
  if(view==View::MORSE_TRAIN){
    if(morseQuizAnswered){
      if(k.enter){morseQuizLetter=(char)('A'+(esp_random()%26));
        morseQuizAnswered=false;morseQuizAnswer="";
        playMorseAudio(NexusMorse::encode(String(morseQuizLetter)));paint();}
    }else{
      if(k.del&&morseQuizAnswer.length())morseQuizAnswer.remove(morseQuizAnswer.length()-1);
      for(char ch:k.word)if(isalpha((unsigned char)ch)&&morseQuizAnswer.length()<1)morseQuizAnswer+=String((char)toupper((unsigned char)ch));
      if(k.enter){morseQuizAnswered=true;
        setStatus(morseQuizAnswer==String(morseQuizLetter)?"Bonne reponse !":"Essaye encore / nouvelle lettre");}
      paint();
    }
    return;
  }
  if(view==View::SEND_CONFIRM){
    if(k.enter){sendText();return;}
    return;
  }
  if(view==View::SEND_PROGRESS){
    if(escPressed){changeView(View::SECTION);return;}
    return;
  }
  if(view==View::SEND_RESULT){
    if(k.enter){changeView(View::SECTION);return;}
    if(escPressed){changeView(editor.isEmpty()?View::SECTION:View::COMPOSE);return;}
    return;
  }
  if(view==View::VOICE_READY){
    bool moved=false;
    if(upPressed){voiceChoice=(voiceChoice+3)%4;moved=true;}
    if(downPressed){voiceChoice=(voiceChoice+1)%4;moved=true;}
    if(choose){
      if(voiceChoice==0){uploadVoice();return;}
      if(voiceChoice==1){playArchiveWav(TX_AUDIO);paint();return;}
      if(voiceChoice==2){recordVoice(true);return;}
      changeView(View::SECTION);return;
    }
    if(moved)paint();return;
  }
  if(view==View::VOICE_ARCHIVE){
    if(voiceCount){
      bool moved=false;
      if(upPressed){voiceSelection=(voiceSelection+voiceCount-1)%voiceCount;moved=true;}
      if(downPressed){voiceSelection=(voiceSelection+1)%voiceCount;moved=true;}
      if(choose){
        const VoiceArchive& e=voiceArchives[voiceCount-1-voiceSelection];
        playArchiveWav(e.path);return;
      }
      if(moved)paint();
    }
    return;
  }
  if(view==View::BATTERY_INFO){
    if(k.enter){measureBattery();paint();}
    return;
  }
  if(view==View::SEARCH_RESULTS){
    bool moved=false;
    if(searchSize){
      if(upPressed){searchSelection=(searchSelection+searchSize-1)%searchSize;moved=true;}
      if(downPressed){searchSelection=(searchSelection+1)%searchSize;moved=true;}
      if(k.enter||rightPressed){openArchiveText();return;}
    }
    if(moved)paint();return;
  }
  if(view==View::SEARCH_VIEW){
    const size_t pages=max((size_t)1,(searchViewBody.length()+92)/93);
    if(rightPressed && searchPage+1<pages){searchPage++;paint();}
    else if(leftPressed && searchPage>0){searchPage--;paint();}
    return;
  }
  if(view==View::CONFIG){
    bool moved=false;
    if(upPressed){configIndex=(configIndex+CONFIG_COUNT-1)%CONFIG_COUNT;moved=true;}
    if(downPressed){configIndex=(configIndex+1)%CONFIG_COUNT;moved=true;}
    if(choose){
      switch(configIndex){
        case 0:input=serverUrl;changeView(View::EDIT_URL);return;
        case 1:input="";changeView(View::EDIT_TOKEN);return;
        case 2:input=recipient;changeView(View::EDIT_PEER);return;
        case 3:wifiSetup();return;
      }
    }
    if(moved)paint();return;
  }
  if(view==View::WIFI_CONFIRM){
    bool moved=false;
    if(upPressed){confirmIndex=(confirmIndex+2)%3;moved=true;}
    if(downPressed){confirmIndex=(confirmIndex+1)%3;moved=true;}
    if(choose){
      if(confirmIndex==0){
        beginWifiAsync(wifiSsid,wifiPass,false);return;
      }
      if(confirmIndex==1){scanWifi();return;}
      if(confirmIndex==2){changeView(View::DELETE_CONFIRM);return;}
    }
    if(moved)paint();return;
  }
  if(view==View::WIFI_LIST){
    bool moved=false;
    // Enter on an empty/failed scan retries discovery.
    if(!wifiScanPending && !scanCount && k.enter){scanWifi();return;}
    if(scanCount){
      if(upPressed){wifiIndex=(wifiIndex+scanCount-1)%scanCount;moved=true;}
      if(downPressed){wifiIndex=(wifiIndex+1)%scanCount;moved=true;}
      if(choose){chooseWiFi(wifiIndex);return;}
    }
    if(moved)paint();return;
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
     view==View::EDIT_PEER||view==View::COMPOSE||view==View::MORSE_EDIT||view==View::SEARCH_EDIT){
    // Fn chords do not type punctuation into passwords or drafts.
    if(k.fn){return;}
    String& target=view==View::COMPOSE?editor:input;
    const bool erase=k.del&&target.length();
    if(erase)target.remove(target.length()-1);
    bool edited=erase;
    for(char ch:k.word){
      if(ch>=32&&ch<=126&&target.length()<(view==View::COMPOSE?600u:view==View::MORSE_EDIT?70u:view==View::SEARCH_EDIT?48u:160u)){
        target+=ch;edited=true;
      }
    }
    if(view==View::COMPOSE&&edited){
      if(pendingTextId.length()){pendingTextId="";prefs.remove("txtid");}
      persistDraft();
    }
    if(k.enter){
      if(view==View::SEARCH_EDIT){searchTextArchive();return;}
      if(view==View::MORSE_EDIT){
        morseOutput=NexusMorse::encode(input);
        if(morseOutput.isEmpty())morseOutput="Aucun caractere pris en charge";
        if(morseMode==1 && morseOutput!="Aucun caractere pris en charge")
          playMorseAudio(morseOutput);
        changeView(View::MORSE_RESULT);return;
      }
      if(view==View::WIFI_PASSWORD){storeWiFiAfterTest(input);input="";return;}
      if(view==View::EDIT_URL){
        input.trim();
        if(!input.startsWith("https://")||input.indexOf(' ',8)>=0){
          setStatus("HTTPS obligatoire");
        }else{
          while(input.endsWith("/"))input.remove(input.length()-1);
          if(!saveSetting("server",input)){paint();return;}
          serverUrl=input;serverReady=false;setStatus("Adresse sauvegardee");
          changeView(View::CONFIG);return;
        }
      }
      if(view==View::EDIT_TOKEN){
        if(input.length()<10){setStatus("Jeton trop court (10 min)");}
        else{
          if(!saveSetting("token",input)){paint();return;}
          deviceToken=input;serverReady=false;input="";setStatus("Jeton sauvegarde");
          changeView(View::CONFIG);return;}
      }
      if(view==View::EDIT_PEER){
        input.trim();
        if(input!="alice"&&input!="bob")setStatus("Choisir alice ou bob");
        else{
          if(!saveSetting("peer",input)){paint();return;}
          recipient=input;serverReady=false;setStatus("Correspondant sauvegarde");
          changeView(View::CONFIG);return;}
      }
      if(view==View::COMPOSE){
        if(editor.isEmpty()){setStatus("Message vide");paint();return;}
        changeView(View::SEND_CONFIRM);return;
      }
    }
    if(edited || k.enter)paint();return;
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
    if(changed&&historySize)pendingReadId=history[historySize-1-pageHistory].id;
    if(changed||leftPressed||rightPressed)paint();return;
  }
  if(view==View::SENT){if(choose)refreshSent();return;}
  if(view==View::DIAG){if(choose){serverTest();paint();}return;}
}
} // namespace
void setup(){
 Serial.begin(115200);auto cfg=M5.config();M5Cardputer.begin(cfg,true);
 M5Cardputer.Display.setRotation(1);
 loadSettings();initSd();paint();
 netReplyQueue=xQueueCreate(1,sizeof(NetReply*));
 WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
 if(!wifiSsid.isEmpty())beginWifiAsync(wifiSsid,wifiPass,true);
 else if(!storageFault)setStatus("Choisir le Wi-Fi dans le menu");
 paint();
}
void loop(){
 M5Cardputer.update();handleInput();
 serviceWifiScan();servicePendingWifi();flushDraftIfDue();
 serviceNetworkReplies();tryStartTextSend();
 const uint32_t now=millis();
 if(WiFi.status()!=WL_CONNECTED){
   serverReady=false;
   if(wifiAttempt==WifiAttempt::NONE && !wifiScanPending && !netWorkerBusy &&
      !wifiSsid.isEmpty() && now-lastWifiAttempt>20000){
     lastWifiAttempt=now;WiFi.reconnect();
   }
 }else if(now-lastCheck>12000){
   lastCheck=now;
   // Queue HTTPS on a separate FreeRTOS task; the keyboard always keeps running.
   scheduleNetworkPoll();
 }
 delay(10);
}
