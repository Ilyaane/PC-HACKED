"""Static regression checks for the 240x135 Cardputer ADV UI.

These checks cannot prove the physical orientation without an attached device.
"""
from pathlib import Path
import unittest

CPP = (Path(__file__).resolve().parents[1] / "firmware/src/main.cpp").read_text(encoding="utf-8")

class DisplayOrientationChecks(unittest.TestCase):
    def test_landscape_dimensions(self):
        self.assertIn("display.width()==240 && display.height()==135", CPP)
        self.assertIn("configureDisplayOrientation();", CPP)

    def test_orientation_saved_and_flipped(self):
        self.assertIn('SD.open("/mqtt/display.cfg",FILE_READ)', CPP)
        self.assertIn('SD.rename("/mqtt/display.tmp","/mqtt/display.cfg")', CPP)
        self.assertNotIn('Preferences', CPP)
        self.assertIn("const uint8_t next=(activeDisplayRotation+2)&3;", CPP)
        self.assertIn("if(fn && (code=='o'||code=='O'))", CPP)

    def test_redraw_after_flip(self):
        self.assertIn("uiInitialized=false;", CPP)
        self.assertIn("lastDraw=0;", CPP)

    def test_network_status_cleared_before_update(self):
        self.assertIn("}else if(currentPage!=Page::Home){", CPP)

if __name__=="__main__":
    unittest.main()
