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
        self.assertIn("if(rep->op != Op::FETCH", text)

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

if __name__ == "__main__":
    unittest.main(verbosity=2)
