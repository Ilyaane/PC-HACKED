#!/usr/bin/env python3
"""Contract tests for the KVXputer overlay; no device or Wi-Fi required."""
from pathlib import Path
import tempfile
import unittest
from install_overlay import install, ROOT

class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.kvx = self.base / "kvx"
        (self.kvx / "src/root/ui").mkdir(parents=True)
        (self.kvx / "src/root/ui/main_menu.h").write_text(
            '#include "menu/discovery/discovery_menu.h"\n'
            '    DiscoveryMenu discoveryMenu;\n', encoding="utf-8")
        (self.kvx / "platformio.ini").write_text(
            "[env]\nlib_deps =\n\tmikalhart/TinyGPSPlus\n", encoding="utf-8")
        (self.kvx / "src/root/ui/menu_registry.cpp").write_text(
            'const MenuDescriptor kMenus[] = {\n'
            '    {"config", "Config", &mainMenu.configMenu, MENU_FLAG_NONE},\n'
            '};\n', encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def test_install_idempotent_and_last_tile(self):
        install(self.kvx)
        install(self.kvx)
        menu = (self.kvx / "src/root/ui/main_menu.h").read_text()
        registry = (self.kvx / "src/root/ui/menu_registry.cpp").read_text()
        self.assertEqual(menu.count("KvxMqttMenu kvxMqttMenu"), 1)
        self.assertEqual(registry.count('"kvx-mqtt"'), 1)
        self.assertGreater(registry.index('"kvx-mqtt"'), registry.index('"config"'))
        self.assertNotIn("nexusNeonMenu", menu + registry)
        self.assertFalse((self.kvx / "src/menu/nexus_neon").exists())
        self.assertTrue((self.kvx / "src/menu/kvx_mqtt/kvx_mqtt_app.cpp").is_file())
        self.assertEqual((self.kvx / "platformio.ini").read_text().count("256dpi/MQTT@2.5.2"), 1)

    def test_mqtt_native_and_no_wifi_ownership(self):
        text = (ROOT / "overlay/src/menu/kvx_mqtt/kvx_mqtt_app.cpp").read_text()
        self.assertIn("KVX_MQTT_HOST", text)
        self.assertIn("SD.open(CONFIG_FILE", text)
        self.assertNotIn("k.exit_key", text)
        self.assertIn("esc && (isFnKeyHeld() || k.fn)", text)
        self.assertIn("MBEDTLS_ERR_SSL_ALLOC_FAILED", text)
        startup = text.split("void appStart(){", 1)[1].split("void appService(){", 1)[0]
        self.assertNotIn("loadHistory();", startup)
        self.assertIn("historyLoaded=false", startup)
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
        self.assertIn('// Native compact inline editor', code)
        self.assertIn('editBuf.length()<110', code)
        self.assertIn("size_t settingsTextOffset=0;", code)
        self.assertIn("const size_t start=std::min(settingsTextOffset,editBuf.length())", code)
        self.assertNotIn("shown+='*'", code)
        self.assertNotIn("revealPassword", code)
        self.assertIn('if(f.size()>8192){f.close();return false;}', code)
        self.assertIn('cfg.port>0 && validNode(cfg.id)', code)
        self.assertIn('cfg.port=(p>0&&p<65536)?(uint16_t)p:0;', code)
        self.assertIn('if(k.pressed && k.fn && k.del)', code)  # Fn+Del clears inline field
        self.assertIn('currentPage=Page::Settings;', code)
        # Editing is a transaction: ESC reverts and existing drafts survive reconnect.
        self.assertIn("beforeSettings=cfg;", code)
        self.assertIn("cfg=beforeSettings;", code)
        self.assertIn("settingsIdx=configIsValid()?2:0;", code)
        self.assertIn("WiFi.hostByName(netCfg.host.c_str(),brokerIp)", code)
        self.assertIn("tls.lastError(tlsInfo,sizeof(tlsInfo))", code)
        self.assertIn("MQTT TCP/TLS: hote, port, CA", code)
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
            "String generalKeyboard() {\n"
            "    bool mask_input = false\n"
            ") {\n"
            "    max_FM_size = tftWidth / (LW * FM) - 1;\n"
        )
        # Test the actual upstream match, independently from firmware compilation.
        patch(self.kvx)
        once=target.read_text()
        patch(self.kvx)
        self.assertEqual(once,target.read_text())
        self.assertIn("mask_input = false;",once)
        self.assertEqual(once.count("mask_input = false;"),1)

    def test_ble_compact_font_is_reference_for_all_menus(self):
        script = (ROOT / 'tools/patch_ble_font.py').read_text()
        self.assertIn('static constexpr int kKvxMenuFont = 1', script)
        self.assertIn('segment = re.sub', script)
        self.assertIn('display.index("Opt_Coord listFiles(")', script)
        self.assertIn('display.index("Opt_Coord drawOptions(")', script)
        self.assertIn('uiRowH(1)', script)

if __name__ == "__main__":
    unittest.main(verbosity=2)
