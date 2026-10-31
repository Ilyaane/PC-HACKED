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
 'WAV streamed to microSD': 'f.write((uint8_t*)audio,sizeof(audio))' in cpp,
 'WAV uploaded from SD': 'http.sendRequest("POST",&f,f.size())' in cpp,
 'WAV backup': 'SD.rename(RX_AUDIO,RX_BACKUP)' in cpp,
 'WAV rollback': 'SD.rename(RX_BACKUP,RX_AUDIO)' in cpp,
 'WAV plays with I2S': 'M5Cardputer.Speaker.playRaw(samples,bytes/2,AUDIO_RATE' in cpp,
 'Never use mic and speaker concurrently': 'M5Cardputer.Mic.end()' in cpp and 'M5Cardputer.Speaker.end()' in cpp,
 'Wi-Fi persist after success': 'prefs.putString("ssid",ssid)' in cpp and 'prefs.putString("pwd",pass)' in cpp,
 'Wi-Fi success and NO branch': 'case View::WIFI_CONFIRM' in cpp and 'scanWifi();return;' in cpp,
 'Wi-Fi retry': 'WiFi.reconnect()' in cpp,
 'HTTPS root certificates': 'client.setCACert(NEXUS_TRUSTED_ROOTS)' in cpp,
 'No insecure TLS': 'setInsecure' not in cpp and 'setInsecure' not in worker,
 'HTTP timeouts': 'http.setTimeout(' in cpp and 'http.setConnectTimeout(' in cpp,
 'NTP time sync': 'configTime(' in cpp,
 'Text paged, not clipped when stored': 'history[historySize++]={id,sender,kind,text};' in cpp,
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