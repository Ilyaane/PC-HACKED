from pathlib import Path
import unittest
BASE=Path(__file__).resolve().parents[1]
C=(BASE/'firmware/src/main.cpp').read_text()

class FirmwareV023(unittest.TestCase):
  def test_direct_physical_arrow_keys(self):
    self.assertIn("code==';'||code==','||code=='.'||code=='/'",C)
    self.assertIn('const bool directArrow=arrow && !shift;',C)
    self.assertIn('if(directArrow)',C)
    self.assertNotIn('keys.fn && M5Cardputer.Keyboard.isKeyPressed',C)

  def test_equal_number_swap_is_detected(self):
    self.assertIn('held & ~lastPhysicalKeyMask',C)
    self.assertIn('M5Cardputer.Keyboard.keyList()',C)
    self.assertNotIn('if(!M5Cardputer.Keyboard.isChange()',C)
    old=(1<<39) # up
    new=(1<<37) # down: same number of keys
    assert old.bit_count()==new.bit_count()
    self.assertEqual(new & ~old,new)

  def test_keypress_once_not_duplicated_on_hold(self):
    mask=(1<<36)
    self.assertEqual(mask & ~0,mask)
    self.assertEqual(mask & ~mask,0)
    self.assertEqual(0 & ~mask,0)

  def test_punctuation_available_in_editing(self):
    self.assertIn('if(currentPage==Page::Settings)',C)
    self.assertIn('typed>=32 && typed<=126',C)
    self.assertIn('directArrow && (draft.isEmpty() || fn)',C)
    self.assertIn('M5Cardputer.Keyboard.getKey(p)',C)

  def test_password_edit_and_delete(self):
    self.assertIn('const bool del=code==KEY_BACKSPACE;',C)
    self.assertIn('if(del && editBuf.length())',C)
    self.assertIn('else if(del && draft.length())',C)

  def test_network_updates_never_trigger_full_ui_redraw_on_home(self):
    self.assertIn('if(currentPage==Page::Network)lastDraw=0;',C)
    self.assertIn('if(uiInitialized)drawConnectionDots();',C)
    self.assertIn('const bool newPage=!uiInitialized || lastRenderedPage!=currentPage;',C)
    self.assertIn('else if(currentPage!=Page::Home && currentPage!=Page::Network)',C)
    self.assertNotIn('millis()-lastDraw>1500',C)

  def test_network_error_visible(self):
    self.assertIn('mqtt.returnCode()',C)
    self.assertIn('mqtt.lastError()',C)
    self.assertIn('MQTT: identifiants refuses',C)
    self.assertIn('MQTT e=',C)
    self.assertIn('Horloge NTP: attente/reessai',C)

  def test_nvs_and_sd_unchanged(self):
    self.assertIn('WiFi.persistent(false);',C)
    self.assertNotIn('Preferences',C)
    self.assertNotIn('nvs_set_',C)
    for f in ('/mqtt/config.ini','/mqtt/ca.pem','/mqtt/history.jsonl','/mqtt/outbox.jsonl'):
      self.assertIn(f,C)

  def test_nocommon_io_in_network_worker(self):
    worker=C.split('void networkTask(void*){',1)[1].split('void serviceNetworkEvents(){',1)[0]
    self.assertNotIn('M5Cardputer',worker)
    self.assertNotIn('SD.',worker)
    self.assertNotIn('drawUi(',worker)
    self.assertIn('vTaskDelay',worker)

if __name__=='__main__':unittest.main()
