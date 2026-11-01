"""Source assertions only: NOT a build or a device test."""
from pathlib import Path
import re, sys
root=Path(__file__).resolve().parents[1]
cpp=(root/'firmware/src/main.cpp').read_text()
ini=(root/'firmware/platformio.ini').read_text()
worker=(root/'cloudflare/src/index.mjs').read_text()
checks={
 'ADV-only target': 'board = m5stack-stamps3' in ini,
 'Correct 8MB flash': 'board_build.flash_size = 8MB' in ini,
 'Correct 8MB partition scheme': 'board_build.partitions = default_8MB.csv' in ini,
 'App capacity enforced': 'board_upload.maximum_size = 3342336' in ini,
 'No PSRAM flag': 'BOARD_HAS_PSRAM' not in ini,
 'No PSRAM allocation': 'ps_malloc' not in cpp,
 'Keyboard enabled': 'M5Cardputer.begin(cfg,true)' in cpp,
 'Graphical menu': 'drawMenu(' in cpp and 'fillRoundRect' in cpp,
 'Focus arrow drawn': 'fillTriangle' in cpp,
 'Bare punctuation menu navigation': 'const bool upPressed=M5Cardputer.Keyboard.isKeyPressed(\';\')' in cpp and 'const bool downPressed=M5Cardputer.Keyboard.isKeyPressed(\'.\')' in cpp,
 'Preserve punctuation in editors': 'leftPressed && !editing' in cpp,
 'OPT escape': 'escPressed=k.opt' in cpp,
 'Correct 1.1.1 keyboard fields': 'k.up' not in cpp and 'k.backspace' not in cpp and 'k.esc' not in cpp,
 'No full-screen clear per key': 'if(fullRepaint)l.fillScreen(COL_BG);' in cpp and 'if(moved)paint();return;' in cpp,
 'NVS status validated': 'prefsReady=prefs.begin("nexusadv",false)' in cpp and 'prefs.getString(key, String("__NVS_NOT_FOUND__")) != value' in cpp,
 'Wi-Fi saved only after readback': 'const bool saved=saveSetting("pwd",pass) && saveSetting("ssid",ssid) && rememberNetwork(ssid,pass)' in cpp,
 'Diagnostics show role, IP, RAM, Wi-Fi and NVS': 'Role: ' in cpp and 'NVS: ' in cpp and 'WiFi.localIP().toString()' in cpp and 'ESP.getFreeHeap()' in cpp,

 'Horizontal icon home': 'void drawHome()' in cpp and 'case View::HOME:drawHome()' in cpp,
 'Section-based navigation': 'void openSectionSelection()' in cpp and 'case View::SECTION:' in cpp,
 'Morse code library': (root/'firmware/include/nexus_morse.h').exists(),
 'Voice archive path and index': 'VOICE_ROOT' in cpp and 'saveVoiceArchive(' in cpp and 'VOICE_INDEX' in cpp,
 'Unread server summary': "pathname==='/unread'" in worker and 'refreshUnread()' in cpp,
 'Boot-time inbox download': 'if(serverTest()){pollInbox();refreshUnread();}' in cpp,
 'Unread badge rendered': '" nouv"' in cpp and 'unreadCount' in cpp,
 'Text archived by date and ID': 'archiveText(' in cpp and 'TEXT_ROOT' in cpp and 'voiceTime(created)' in cpp,
 'Sent text also archived': 'saved=archiveText(id,from,(uint32_t)time(nullptr),editor,true)' in cpp,
 '24 recent offline messages': 'HISTORY_CAP=24' in cpp and 'loadHistory()' in cpp,
 'Six horizontal home categories': 'HOME_COUNT=6' in cpp and '"AUTRES"' in cpp,

 'Notification toggle in System': 'Son notification : ' in cpp and 'prefs.putBool("beep",notificationSound)' in cpp,
 'Notification persisted': 'notificationSound=prefs.getBool("beep",true)' in cpp,
 'Sound only after a new unread ID': 'id>notifiedMessageId && n>0 && notificationSound' in cpp,
 'Notification tone bounded': 'M5Cardputer.Speaker.tone(950,85)' in cpp and 'M5Cardputer.Speaker.end()' in cpp,
 'Archive search query editing': 'case View::SEARCH_EDIT' in cpp and 'searchTextArchive()' in cpp,
 'Archive search scans only NEXUS text root': 'scanTextFolder(TEXT_ROOT,keyword,0)' in cpp and 'path.startsWith(String(TEXT_ROOT)+"/")' in cpp,
 'Archive search is bounded': 'searchScanned>=12000' in cpp and 'searchSize==SEARCH_MAX' in cpp,
 'Archive search result view': 'case View::SEARCH_VIEW' in cpp and 'openArchiveText()' in cpp,
 'Battery uses official M5Stack API': 'M5Cardputer.Power.getBatteryLevel()' in cpp and 'M5Cardputer.Power.getBatteryVoltage()' in cpp,
 'Battery smoothes readings': 'batteryVoltageSmooth*3+median' in cpp,
 'Battery explicitly says estimated': 'Niveau estime: ' in cpp and 'Pas de jauge precise' in cpp,
 'Battery refresh via Enter': 'if(k.enter){measureBattery();paint();}' in cpp,

 'NVS independent of microSD': 'prefsReady=prefs.begin("nexusadv",false)' in cpp and 'if(!sdReady)' in cpp,

 'No letter shortcut menu': "case 't':case 'T'" not in cpp and 'openSectionSelection()' in cpp,
 'Wi-Fi password is visible': 'case View::WIFI_PASSWORD' in cpp and 'shown=input.length()' in cpp,
 'Five saved Wi-Fi slots': 'WIFI_SLOTS=5' in cpp and 'rememberNetwork(' in cpp and 'wifiSlot(' in cpp,
 'Saved Wi-Fi reused': "scanSaved[selected]" in cpp or "const int saved=wifiSlot(wifiSsid)" in cpp,
 'Shared code minimum is 10': 'deviceToken.length()>=10' in cpp and 'input.length()<10' in cpp,
 'Device role declared to Worker': 'http.addHeader("X-Nexus-User"' in cpp,
 'Device role linked to recipient': 'recipient=="bob"?"alice":"bob"' in cpp,
 'Shared token accepted by server': 'env.SHARED_TOKEN' in worker and 'request.headers.get(\'X-Nexus-User\')' in worker,


 'WAV streamed to microSD': 'f.write((uint8_t*)audio,sizeof(audio))' in cpp,
 'WAV uploaded from SD': 'http.sendRequest("POST",&f,f.size())' in cpp,
 'WAV backup': 'SD.rename(RX_AUDIO,RX_BACKUP)' in cpp,
 'WAV rollback': 'SD.rename(RX_BACKUP,RX_AUDIO)' in cpp,
 'WAV plays with I2S': 'M5Cardputer.Speaker.playRaw(samples,bytes/2,AUDIO_RATE' in cpp,
 'Never use mic and speaker concurrently': 'M5Cardputer.Mic.end()' in cpp and 'M5Cardputer.Speaker.end()' in cpp,
 'Wi-Fi persist after success': 'saveSetting("ssid",ssid)' in cpp and 'saveSetting("pwd",pass)' in cpp,
 'Wi-Fi success and NO branch': 'case View::WIFI_CONFIRM' in cpp and 'scanWifi();return;' in cpp,
 'Wi-Fi retry': 'WiFi.reconnect()' in cpp,
 'HTTPS root certificates': 'client.setCACert(NEXUS_TRUSTED_ROOTS)' in cpp,
 'No insecure TLS': 'setInsecure' not in cpp and 'setInsecure' not in worker,
 'HTTP timeouts': 'http.setTimeout(' in cpp and 'http.setConnectTimeout(' in cpp,
 'NTP time sync': 'configTime(' in cpp,
 'Text paged, not clipped when stored': 'h.id=id;h.sender=sender;h.kind=kind;h.body=text;' in cpp,
 'Send draft persisted': 'prefs.putString("draft",editor)' in cpp,
 'Read acknowledgment': '/read' in cpp and 'acknowledgeMessage' in cpp,
 'Sent status screen': 'case View::SENT' in cpp and 'refreshSent()' in cpp,
 'Token not embedded in source': 'ALICE_TOKEN' not in cpp and 'BOB_TOKEN' not in cpp,
 'Access authorization required': 'if(!sender) return fail(401' in worker,
 'Distinct tokens': 'env.ALICE_TOKEN===env.BOB_TOKEN' in worker,
 'Idempotent insert': 'ON CONFLICT(sender,client_id) DO NOTHING' in worker,
 'Idempotent conflict validation': "return fail(409,'Client id already used for a different message')" in worker,
 'WAV format validation': 'const isWav = b =>' in worker,
 'No-store responses': "'cache-control':'no-store'" in worker,
 'Cannot read others WAV': "WHERE id=? AND recipient=?" in worker,
 'Cloudflare D1 schema': 'UNIQUE(sender,client_id)' in (root/'cloudflare/schema.sql').read_text(),
 'GitHub CI build workflow': 'pio run -e cardputer-adv' in (root/'.github/workflows/build-adv.yml').read_text(),
 'No firmware binary fabricated': not list(root.rglob('*.bin')),
 '3 public root certificates': (root/'firmware/include/ca_roots.h').read_text().count('BEGIN CERTIFICATE')==3,
}
failed=[name for name,ok in checks.items() if not ok]
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL')+' '+name)
print(f'RESULT: {len(checks)-len(failed)}/{len(checks)} source checks passed. Not a C++ compilation.')
if failed: sys.exit(1)