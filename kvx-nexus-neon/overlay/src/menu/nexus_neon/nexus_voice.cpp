#include "nexus_voice.h"
#include "nexus_neon_secrets.h"
#include "ca_roots.h"
#include "root/storage/sd_functions.h"
#include "menu/others/mic.h"
#include "menu/others/audio.h"
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <new>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace NexusVoice {
namespace {
constexpr size_t MAX_WAV=120000;
constexpr const char *DIR="/nexus", *OUT="/nexus/draft.wav";
constexpr const char *RAW="/nexus/raw48.wav", *TEMP="/nexus/new16.wav";
std::atomic<bool> running{false}, finished{false}, aborting{false};
bool mounted=false, pendingDownload=false, downloaded=false;
bool transferOK=false;
uint32_t fetchedID=0;
String state="Vocal pret", peer, idKey;
enum class Work { Send, Receive };
struct Job {Work kind;String to, key;uint32_t messageID=0;};

uint32_t read32(const uint8_t *b) {
    return uint32_t(b[0]) | (uint32_t(b[1])<<8) |
           (uint32_t(b[2])<<16) | (uint32_t(b[3])<<24);
}
void write32(uint8_t *b,uint32_t v) {
    for(int i=0;i<4;i++)b[i]=uint8_t(v>>(8*i));
}
String receivedPath(uint32_t id) { return String(DIR)+"/rx_"+String(id)+".wav"; }
bool valid(const String &path) {
    File f=SD.open(path.c_str(),FILE_READ);
    if(!f)return false;
    const size_t bytes=f.size();
    uint8_t h[44]={};
    const bool complete=(bytes>=44 && bytes<=MAX_WAV && f.read(h,44)==44);
    f.close();
    return complete && !memcmp(h,"RIFF",4) && !memcmp(h+8,"WAVE",4) &&
           !memcmp(h+12,"fmt ",4) && !memcmp(h+36,"data",4) &&
           read32(h+4)==bytes-8 && read32(h+16)==16 &&
           h[20]==1 && h[21]==0 && h[22]==1 && h[23]==0 &&
           read32(h+24)==16000 && read32(h+28)==32000 &&
           h[32]==2 && h[33]==0 && h[34]==16 && h[35]==0 &&
           read32(h+40)==bytes-44;
}
bool remember() {
    Preferences prefs;
    if(!prefs.begin("nexusneon",false))return false;
    const bool a=prefs.putString("vocid",idKey)==idKey.length();
    const bool b=prefs.putString("vocpeer",peer)==peer.length();
    prefs.end();
    return a&&b;
}
void forget() {
    peer="";idKey="";
    Preferences prefs;
    if(prefs.begin("nexusneon",false)){
        prefs.remove("vocid");prefs.remove("vocpeer");prefs.end();
    }
}
bool convert48To16() {
    File input=SD.open(RAW,FILE_READ);
    if(!input)return false;
    uint8_t h48[44]={};
    const size_t size=input.size();
    if(size<48044 || size>480000 || input.read(h48,44)!=44 ||
       memcmp(h48,"RIFF",4)!=0 || memcmp(h48+8,"WAVE",4)!=0 ||
       memcmp(h48+12,"fmt ",4)!=0 || memcmp(h48+36,"data",4)!=0 ||
       read32(h48+24)!=48000 || h48[20]!=1 || h48[22]!=1 ||
       h48[34]!=16 || read32(h48+40)>size-44) {
        input.close();return false;
    }
    SD.remove(TEMP);
    File output=SD.open(TEMP,FILE_WRITE);
    if(!output){input.close();return false;}
    uint8_t h[44]={};
    memcpy(h,"RIFF",4);memcpy(h+8,"WAVEfmt ",8);
    memcpy(h+36,"data",4);write32(h+16,16);
    h[20]=1;h[22]=1;
    write32(h+24,16000);write32(h+28,32000);h[32]=2;h[34]=16;
    bool ok=output.write(h,44)==44;
    uint32_t outputBytes=0;
    // 768 samples per read, 256 output samples: ~3 seconds require ~2 KB RAM.
    alignas(4) int16_t from[768],to[256];
    while(ok && input.available()>=6) {
        const size_t want=std::min((size_t)input.available(),sizeof(from));
        const size_t bytes=(want/6)*6;
        if(bytes<6 || input.read((uint8_t*)from,bytes)!=(int)bytes){ok=false;break;}
        const size_t samples=bytes/6;
        for(size_t i=0;i<samples;i++){
            const int32_t n=int32_t(from[3*i])+from[3*i+1]+from[3*i+2];
            to[i]=int16_t(n/3);
        }
        const size_t nbytes=samples*2;
        if(outputBytes+nbytes>MAX_WAV-44 ||
           output.write((const uint8_t*)to,nbytes)!=nbytes){ok=false;break;}
        outputBytes+=(uint32_t)nbytes;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if(outputBytes<1600)ok=false;
    if(ok){
        write32(h+4,outputBytes+36);write32(h+40,outputBytes);
        ok=output.seek(0) && output.write(h,44)==44;
    }
    output.flush();output.close();input.close();
    SD.remove(RAW);
    if(!ok || !valid(TEMP)){SD.remove(TEMP);return false;}
    return true;
}
bool networkReady() {
    return WiFi.status()==WL_CONNECTED &&
           strlen(NEXUS_NEON_SHARED_TOKEN)>=10 &&
           time(nullptr)>=1700000000 && storage();
}
void worker(void *arg) {
    Job *job=static_cast<Job*>(arg);
    bool ok=false;
    if(networkReady() && !aborting.load()) {
        WiFiClientSecure client;
        client.setCACert(NEXUS_TRUSTED_ROOTS);
        client.setTimeout(8000);
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(8000);
        const String url=String(NEXUS_NEON_SERVER_URL) +
              (job->kind==Work::Send?"/voice/"+job->to:
               "/voice/"+String(job->messageID));
        if(http.begin(client,url)) {
            http.addHeader("Authorization",String("Bearer ")+NEXUS_NEON_SHARED_TOKEN);
            http.addHeader("X-Nexus-User",job->kind==Work::Send?
                    (job->to=="alice"?"bob":"alice"):job->to);
            if(job->kind==Work::Send) {
                File in=SD.open(OUT,FILE_READ);
                if(in && in.size()>=44 && in.size()<=MAX_WAV) {
                    http.addHeader("Content-Type","audio/wav");
                    http.addHeader("X-Client-Id",job->key);
                    const int code=http.sendRequest("POST",&in,in.size());
                    if(code>=200 && code<300 && http.getSize()<=2048) {
                        JsonDocument doc;
                        ok=!deserializeJson(doc,http.getString()) &&
                           (doc["id"]|0u)>0;
                    }
                }
                if(in)in.close();
            }else {
                const int code=http.GET(),len=http.getSize();
                if(code==200 && len>=44 && len<=(int)MAX_WAV) {
                    const String target=receivedPath(job->messageID);
                    const String temp=target+".part";
                    SD.remove(temp.c_str());
                    File out=SD.open(temp.c_str(),FILE_WRITE);
                    if(out) {
                        int got=0;uint8_t chunk[1024];
                        uint32_t progress=millis();
                        WiFiClient *stream=http.getStreamPtr();
                        while(got<len && !aborting.load() &&
                              millis()-progress<12000) {
                            const int avail=stream->available();
                            if(avail<=0){vTaskDelay(pdMS_TO_TICKS(5));continue;}
                            const size_t n=std::min((size_t)avail,
                                std::min(sizeof(chunk),(size_t)(len-got)));
                            const int read=stream->readBytes(chunk,n);
                            if(read<=0 || out.write(chunk,read)!=(size_t)read)break;
                            got+=read;progress=millis();
                        }
                        out.flush();out.close();
                        if(got==len && !aborting.load() && valid(temp) &&
                           SD.rename(temp.c_str(),target))ok=true;
                    }
                    if(!ok)SD.remove(temp.c_str());
                }
            }
            http.end();
        }
    }
    transferOK=ok;
    pendingDownload=(job->kind==Work::Receive);
    fetchedID=job->messageID;
    delete job;
    running.store(false);
    finished.store(true);
    vTaskDelete(nullptr);
}
bool begin(Work kind,const String &recipient,const String &key,uint32_t messageID) {
    if(running.load() || finished.load())return false;
    if(!networkReady()){state="Wi-Fi/HTTPS ou horloge indisponible";return false;}
    Job *job=new (std::nothrow) Job{kind,recipient,key,messageID};
    if(!job){state="Memoire indisponible";return false;}
    aborting.store(false);
    running.store(true);
    if(xTaskCreatePinnedToCore(worker,"nexus_voice_tls",12288,job,1,nullptr,0)!=pdPASS) {
        delete job;running.store(false);state="Tache reseau indisponible";return false;
    }
    state=kind==Work::Send?"Envoi vocal HTTPS...":"Reception vocal HTTPS...";
    return true;
}
} // namespace
bool storage(){
    if(!mounted)mounted=setupSdCard() && sdcardMounted;
    if(!mounted)return false;
    return SD.exists(DIR) || SD.mkdir(DIR);
}
bool hasDraft(){return storage() && valid(OUT);}
String draftRecipient(){
    if(!hasDraft())return "";
    if(peer.isEmpty()) {
        Preferences p;
        if(p.begin("nexusneon",true)) {
            idKey=p.getString("vocid","");
            peer=p.getString("vocpeer","");
            p.end();
        }
    }
    return peer;
}
bool record(const String &recipient){
    if(busy()){state="Transfert en cours";return false;}
    if(!storage()){state="Carte microSD indisponible";return false;}
    if(SD.exists(OUT)){state="Ancien vocal preserve : supprimer d'abord";return false;}
    audioSilenceSpeaker();
    stopAudioPlayback();
    uint32_t bytes=0;
    const bool captured=mic_record_wav_to_path(&SD,RAW,3000,&bytes,1.0f);
    if(!captured || bytes<48044 || !convert48To16()){
        SD.remove(RAW);SD.remove(TEMP);
        state="Capture microphone / WAV echouee";return false;
    }
    if(!SD.rename(TEMP,OUT)){SD.remove(TEMP);state="Ecriture microSD echouee";return false;}
    idKey="kvxv-"+String((uint32_t)esp_random(),HEX)+String((uint32_t)esp_random(),HEX);
    peer=recipient;
    if(!remember()){state="WAV conserve, ID non sauvegarde";return false;}
    state="Vocal pret (3 secondes)";return true;
}
bool playDraft(){
    if(busy() || !hasDraft()){state="Aucun WAV disponible";return false;}
    audioSilenceSpeaker();
    const bool played=playAudioFile(&SD,OUT,PLAYBACK_BLOCKING);
    state=played?"Lecture terminee":"Lecture echouee";
    return played;
}
bool removeDraft(){
    if(busy() || !storage())return false;
    if(!SD.remove(OUT))return false;
    forget();state="Vocal supprime";return true;
}
bool send(const String &recipient){
    if(busy()){state="HTTPS occupe";return false;}
    if(!hasDraft()){state="Vocal manquant";return false;}
    if(draftRecipient()!=recipient){state="Vocal destine a "+peer;return false;}
    if(idKey.isEmpty() || !remember()){state="ID de reprise non sauvegarde";return false;}
    return begin(Work::Send,recipient,idKey,0);
}
bool receive(uint32_t id,const String &ownRole){
    if(busy() || !id)return false;
    if(!storage())return false;
    if(valid(receivedPath(id))){state="Vocal deja telecharge";return true;}
    if(ownRole!="alice" && ownRole!="bob")return false;
    return begin(Work::Receive,ownRole,"",id);
}
bool playReceived(uint32_t id){
    if(busy() || !storage() || !valid(receivedPath(id))){state="WAV recu invalide";return false;}
    audioSilenceSpeaker();
    const bool ok=playAudioFile(&SD,receivedPath(id),PLAYBACK_BLOCKING);
    state=ok?"Vocal ecoute":"Lecture echouee";
    return ok;
}
bool busy(){return running.load();}
bool poll(){
    if(!finished.exchange(false))return false;
    downloaded=transferOK && pendingDownload;
    if(transferOK) {
        if(!pendingDownload) {SD.remove(OUT);forget();}
        state=pendingDownload?"Vocal recu sur microSD":"Envoi vocal confirme";
    }else state=pendingDownload?"Telechargement echoue":"Envoi non confirme : conserve";
    return true;
}
bool completedDownload(){return downloaded;}
uint32_t lastDownloadId(){return fetchedID;}
String status(){return state;}
void cancel(){aborting.store(true);}
} // namespace NexusVoice
