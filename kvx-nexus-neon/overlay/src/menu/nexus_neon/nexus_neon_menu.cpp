#include "nexus_neon_menu.h"
#include "root/ui/display.h"
#include "nexus_neon_app.h"

void NexusNeonMenu::optionsMenu() { nexusNeonApp(); }

void NexusNeonMenu::drawIcon(float scale) {
    // Draw in KVXputer's own tile coordinates, preserving clip and theme colors.
    clearIconArea();
    const int w = (int)(50 * scale);
    const int h = (int)(32 * scale);
    const int x = iconCenterX - w / 2;
    const int y = iconCenterY - h / 2;
    const int r = (int)(5 * scale) + 1;
    tft.drawRoundRect(x, y, w, h, r, kvxConfig.secColor);
    tft.drawRoundRect(x + 2, y + 2, w - 4, h - 4, r, kvxConfig.priColor);
    tft.drawLine(x + w/4, y + h, x + w/4 - 5, y + h + 5, kvxConfig.secColor);
    tft.drawLine(x + w/4 - 5, y + h + 5, x + w/4 + 8, y + h, kvxConfig.secColor);
    const int cy = y + h/2;
    tft.fillCircle(x + w/4, cy, 2, kvxConfig.secColor);
    tft.fillCircle(x + w/2, cy, 2, kvxConfig.secColor);
    tft.fillCircle(x + w*3/4, cy, 2, kvxConfig.secColor);
}
