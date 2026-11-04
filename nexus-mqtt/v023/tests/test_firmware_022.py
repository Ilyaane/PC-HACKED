from pathlib import Path
import unittest

BASE = Path(__file__).resolve().parents[1]
C = (BASE / 'firmware/src/main.cpp').read_text(encoding='utf-8')
INI = (BASE / 'firmware/platformio.ini').read_text(encoding='utf-8')

class FirmwareV022(unittest.TestCase):
    def test_background_worker(self):
        self.assertIn('xTaskCreatePinnedToCore(networkTask', C)
        self.assertIn('void networkTask(void*)', C)
        self.assertIn('void serviceNetworkEvents()', C)

    def test_no_periodic_full_redraw(self):
        self.assertIn('if(lastDraw==0){drawUi();lastDraw=millis();}', C)
        self.assertNotIn('millis()-lastDraw>1500', C)
        self.assertNotIn('void serviceConnections()', C)

    def test_keyboard_not_blocked_by_mqtt_connect(self):
        loop = C.split('void loop(){', 1)[1]
        self.assertIn('M5Cardputer.update();keyPress();serviceNetworkEvents()', loop)
        self.assertNotIn('mqtt.connect(', loop)
        self.assertNotIn('WiFi.begin(', loop)

    def test_wifi_and_mqtt_only_worker(self):
        main = C.split('void setup(){', 1)[1]
        worker = C.split('void networkTask(void*){', 1)[1].split('void serviceNetworkEvents(){', 1)[0]
        self.assertNotIn('mqtt.connected()', main)
        self.assertNotIn('mqtt.publish(', main)
        self.assertIn('mqtt.connect(', worker)
        self.assertIn('mqtt.loop()', worker)
        self.assertIn('mqtt.publish(', worker)

    def test_no_sd_access_in_network_worker(self):
        worker = C.split('void networkTask(void*){', 1)[1].split('void serviceNetworkEvents(){', 1)[0]
        self.assertNotIn('SD.', worker)
        self.assertNotIn('drawUi()', worker)
        self.assertNotIn('M5Cardputer.', worker)
        self.assertNotIn('logHistory(', worker)

    def test_inbound_moved_to_ui_sd_thread(self):
        callback = C.split('void onIncoming(', 1)[1].split('void processIncoming(', 1)[0]
        handler = C.split('void processIncoming(', 1)[1].split('void startSettings(', 1)[0]
        self.assertIn('xQueueSend(netRxQueue', callback)
        self.assertNotIn('SD.', callback)
        self.assertIn('logHistory(m)', handler)

    def test_outbox_ack_before_deletion(self):
        send = C.split('void sendQueued(){', 1)[1].split('void onIncoming(', 1)[0]
        events = C.split('void serviceNetworkEvents(){', 1)[1].split('void setup(){', 1)[0]
        self.assertIn('txWaiting=true', send)
        self.assertNotIn('removeFirstOutboxLine()', send)
        self.assertIn('if(ack.ok){removeFirstOutboxLine()', events)
        self.assertIn('else setStatus("Envoi en attente")', events)

    def test_nvs_credentials_not_persisted(self):
        setup = C.split('void setup(){', 1)[1]
        self.assertIn('WiFi.persistent(false);', setup)
        self.assertLess(setup.index('WiFi.persistent(false);'), setup.index('xTaskCreatePinnedToCore('))
        self.assertNotIn('Preferences', C)
        self.assertNotIn('nvs_set_', C)
        self.assertNotIn('WIFI_STORAGE_FLASH', C)
        self.assertNotIn('WiFi.persistent(true)', C)

    def test_tls_ca_required(self):
        self.assertIn('tls.setCACert(caPem.c_str())', C)
        self.assertIn('else if(!caPem.length())', C)
        self.assertNotIn('setInsecure(', C)
        self.assertIn('const char *CA_FILE="/mqtt/ca.pem";', C)

    def test_history_preserved(self):
        for needle in ['/mqtt/config.ini','/mqtt/config.bak','/mqtt/outbox.jsonl','/mqtt/history.jsonl']:
            self.assertIn(needle,C)
        self.assertIn('loadHistory();', C)
        self.assertIn('saveConfig()', C)

    def test_qos_topics_unchanged(self):
        self.assertIn('"nexusmqtt/v1/"', C)
        self.assertIn('mqtt.publish(topicFor(netCfg.peer),String(outgoing.payload),false,1)', C)
        self.assertIn('mqtt.subscribe(topicFor(netCfg.id),1)', C)
        self.assertIn('mqtt.setOptions(30,false,4000)', C)

    def test_reconnect_with_backoff(self):
        worker = C.split('void networkTask(void*){',1)[1].split('void serviceNetworkEvents(){',1)[0]
        self.assertIn('now-lastWifiAttempt>10000', worker)
        self.assertIn('now-lastMqttAttempt>8000',worker)
        self.assertIn('vTaskDelay(pdMS_TO_TICKS(30))',worker)

    def test_8mb_target_library_versions(self):
        for needle in ['8MB','m5stack/M5Cardputer@1.1.1','256dpi/MQTT@2.5.2','bblanchon/ArduinoJson@7.3.0']:
            self.assertIn(needle,INI)

if __name__ == '__main__': unittest.main()
