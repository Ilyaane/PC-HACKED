#!/usr/bin/env python3
"""Contract tests for the KVXputer overlay; no device or Wi-Fi required."""
import os
from pathlib import Path
import tempfile
import unittest
from install_overlay import install, ROOT

class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.kvx = self.base / "kvx"
        self.nexus = self.base / "nexus"
        (self.kvx / "src/root/ui").mkdir(parents=True)
        (self.nexus / "nexus-adv/firmware/include").mkdir(parents=True)
        (self.kvx / "src/root/ui/main_menu.h").write_text(
            '#include "menu/discovery/discovery_menu.h"\n'
            '    DiscoveryMenu discoveryMenu;\n', encoding="utf-8")
        (self.kvx / "platformio.ini").write_text(
            "[env]\nlib_deps =\n\tmikalhart/TinyGPSPlus\n", encoding="utf-8")
        (self.kvx / "src/root/ui/menu_registry.cpp").write_text(
            'const MenuDescriptor kMenus[] = {\n'
            '    {"config", "Config", &mainMenu.configMenu, MENU_FLAG_NONE},\n'
            '};\n', encoding="utf-8")
        (self.nexus / "nexus-adv/firmware/include/ca_roots.h").write_text(
            'const char NEXUS_TRUSTED_ROOTS[]="test";', encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def test_install_idempotent_and_last_tile(self):
        install(self.kvx, self.nexus, "")
        install(self.kvx, self.nexus, "")
        menu = (self.kvx / "src/root/ui/main_menu.h").read_text()
        registry = (self.kvx / "src/root/ui/menu_registry.cpp").read_text()
        self.assertEqual(menu.count("NexusNeonMenu nexusNeonMenu"), 1)
        self.assertEqual(registry.count('"nexus-neon"'), 1)
        self.assertEqual(registry.count('"kvx-mqtt"'), 1)
        self.assertLess(registry.index('"nexus-neon"'), registry.index('"kvx-mqtt"'))
        self.assertGreater(registry.index('"nexus-neon"'), registry.index('"config"'))
        self.assertTrue((self.kvx / "src/menu/nexus_neon/nexus_neon_app.cpp").is_file())
        self.assertTrue((self.kvx / "src/menu/kvx_mqtt/kvx_mqtt_app.cpp").is_file())
        self.assertEqual((self.kvx / "platformio.ini").read_text().count("256dpi/MQTT@2.5.2"), 1)
        self.assertIn("KvxMqttMenu kvxMqttMenu", menu)
        self.assertIn('#define KVX_MQTT_HOST ""',
                      (self.kvx / "src/menu/kvx_mqtt/kvx_mqtt_private.h").read_text())
        self.assertIn('#define NEXUS_NEON_SHARED_TOKEN ""',
                      (self.kvx / "src/menu/nexus_neon/nexus_neon_secrets.h").read_text())

    def test_reject_wrong_secret(self):
        with self.assertRaises(AssertionError):
            install(self.kvx, self.nexus, "wrong-secret")

    def test_native_module_not_firmware_loader(self):
        text = (ROOT / "overlay/src/menu/nexus_neon/nexus_neon_app.cpp").read_text()
        self.assertIn("xTaskCreatePinnedToCore", text)
        self.assertIn("setCACert", text)
        self.assertIn("pressed(UpPress)", text)
        self.assertIn("pressed(NextPress)", text)
        self.assertIn('["client_id"]', text)
        self.assertNotIn('setInsecure(', text)
        self.assertNotIn('esp_ota_', text)
        self.assertNotIn("void setup()", text)
        self.assertNotIn("void loop()", text)
        self.assertNotIn("M5Cardputer", text)
        code = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("//"))
        self.assertNotIn("MQTT", code)
        self.assertNotIn("prefs.putString(\"token\"", text)
        self.assertIn("key.fn && key.exit_key", text)
        self.assertIn("NEXUS NEON V10", text)
        self.assertIn("NexusVoice::record(sRecipient)", text)
        self.assertIn("NexusVoice::send(sRecipient)", text)
        # Confirmation of a contact must open the standalone-style editor.
        self.assertIn("sHome = 1;\n            newDraft();", text)
        self.assertIn("sDraft=sRecipient==\"bob\"?sDraftBob:sDraftAlice;", text)
        self.assertIn("case 1: // pencil and compose", text)
        self.assertIn("sDraftBob=sDraft;", text)
        self.assertIn("sDraftAlice=sDraft;", text)
        self.assertIn('if(sRecipient=="bob"){sDraftBob=sDraft;sIdBob="";}', text)
        self.assertIn("else sIdAlice=sClientId;", text)
        self.assertIn("sClientId=sRecipient==\"bob\"?sIdBob:sIdAlice;", text)
        self.assertIn("NexusVoice::receive(id,ownRole(sRecipient))", text)
        self.assertIn('kind == "voice"', text)
        voice_src = (ROOT / "overlay/src/menu/nexus_neon/nexus_voice.cpp").read_text()
        self.assertIn("mic_record_wav_to_path", voice_src)
        self.assertIn("playAudioFile(&SD,OUT,PLAYBACK_BLOCKING)", voice_src)
        self.assertIn('http.sendRequest("POST",&in,in.size())', voice_src)
        self.assertIn("constexpr size_t MAX_WAV=120000", voice_src)
        self.assertIn("read32(h+24)==16000", voice_src)
        self.assertIn("if(!remember())", voice_src)
        self.assertNotIn("M5Cardputer.Mic", voice_src)
        self.assertIn("if(rep->op != Op::FETCH", text)
        self.assertIn('const uint32_t receiptId = decoded ? (receipt["id"] | 0u) : 0u;', text)
        self.assertIn('Envoi non confirme : brouillon conserve', text)
        self.assertIn('addHistory(receiptId,', text)

    def test_mqtt_native_and_no_wifi_ownership(self):
        text = (ROOT / "overlay/src/menu/kvx_mqtt/kvx_mqtt_app.cpp").read_text()
        self.assertIn("KVX_MQTT_HOST", text)
        self.assertIn("SD.open(CONFIG_FILE", text)
        self.assertIn("k.fn && k.exit_key", text)
        self.assertIn("workerFinished.store(true)", text)
        self.assertNotIn("void setup()", text)
        self.assertNotIn("void loop()", text)
        self.assertNotIn("M5Cardputer.", text)
        self.assertNotIn("WiFi.begin(", text)
        self.assertNotIn("setInsecure(", text)
        self.assertNotIn("ESP.restart()", text)
        self.assertNotIn("d.startWrite()", text)
        self.assertNotIn("d.endWrite()", text)
        self.assertIn("pendingReconfigure", text)
        self.assertNotIn("#include <Preferences.h>", text)
        self.assertNotIn("prefs.putString", text)

    def test_mqtt_config_editor_and_lazy_sd(self):
        code = (ROOT / "overlay/src/menu/kvx_mqtt/kvx_mqtt_app.cpp").read_text()
        self.assertIn('#include "root/storage/sd_functions.h"', code)
        self.assertIn("sdOK=setupSdCard() && sdcardMounted", code)
        self.assertIn('SD.open(CONFIG_FILE,FILE_READ)', code)
        self.assertIn('String typed=keyboard(editBuf,110,prompt,false)', code)
        self.assertIn("size_t settingsTextOffset=0;", code)
        self.assertIn("const size_t start=std::min(settingsTextOffset,editBuf.length())", code)
        self.assertNotIn("shown+='*'", code)
        self.assertNotIn("revealPassword", code)
        self.assertIn('if(f.size()>8192){f.close();return false;}', code)
        self.assertIn('cfg.port>0 && validNode(cfg.id)', code)
        self.assertIn('cfg.port=(p>0&&p<65536)?(uint16_t)p:0;', code)
        self.assertIn('if(typed!=String("\\x1B"))', code)
        self.assertIn('currentPage=Page::Settings;', code)
        # Editing is a transaction: ESC reverts and existing drafts survive reconnect.
        self.assertIn("beforeSettings=cfg;", code)
        self.assertIn("cfg=beforeSettings;", code)
        self.assertIn("settingsIdx=configIsValid()?7:0;", code)
        self.assertIn("draft=preservedDraft;", code)
        self.assertIn('if(value.endsWith("\\r"))value.remove(value.length()-1);', code)
        self.assertIn("SelPress=false;", code)
        self.assertIn('MQTT: hote, compte ou a/b invalide', code)
        # No raw character event is inserted into the 8 config fields.
        fields = code.split('void onKey(')[1].split('void keyPress(')[0]
        setting_branch = fields.split('if(currentPage==Page::Settings)')[1].split('}else if(currentPage==Page::Home)')[0]
        self.assertNotIn('editBuf+=typed', setting_branch)
        last = code.split('void finishSetting()')[1].split('void navigateHistory(')[0]
        self.assertNotIn('settingsIdx=0', last)

    def test_plaintext_keyboard_patch_is_idempotent(self):
        from patch_keyboard_plaintext import patch
        target=self.kvx / "src/root/input/mykeyboard.cpp"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(
            "String generalKeyboard() {\\n"
            "    bool mask_input = false\\n"
            ") {\\n"
            "    max_FM_size = tftWidth / (LW * FM) - 1;\\n"
        )
        # Test the actual upstream match, independently from firmware compilation.
        patch(self.kvx)
        once=target.read_text()
        patch(self.kvx)
        self.assertEqual(once,target.read_text())
        self.assertIn("mask_input = false;",once)

    def test_nexus_allocations_do_not_deadlock(self):
        code = (ROOT / "overlay/src/menu/nexus_neon/nexus_neon_app.cpp").read_text()
        self.assertNotIn('ESP.getFreeHeap() < 43000', code)
        self.assertIn('heap_caps_get_largest_free_block', code)
        self.assertIn('req->response = new (std::nothrow) NetReply()', code)
        self.assertIn('sBusy.store(false)', code)
        self.assertIn('std::atomic<bool> sBusy', code)
        self.assertIn('kWorkerStack + 2048U', code)
        self.assertIn('vTaskDelete(nullptr)', code)

    def test_ble_compact_font_is_reference_for_all_menus(self):
        script = (ROOT / 'tools/patch_ble_font.py').read_text()
        self.assertIn('static constexpr int kKvxMenuFont = 1', script)
        self.assertIn('segment = re.sub', script)
        self.assertIn('display.index("Opt_Coord listFiles(")', script)
        self.assertIn('display.index("Opt_Coord drawOptions(")', script)
        self.assertIn('uiRowH(1)', script)

if __name__ == "__main__":
    unittest.main(verbosity=2)
