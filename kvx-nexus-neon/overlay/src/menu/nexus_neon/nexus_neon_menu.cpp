#include "nexus_neon_menu.h"
#include "root/ui/display.h"
#include "nexus_neon_app.h"

void NexusNeonMenu::optionsMenu() { nexusNeonApp(); }

void NexusNeonMenu::drawIcon(float scale) {
    clearIconArea();
    // Two compact overlapping message bubbles in the user's KVX theme.
    const int w=(int)(42*scale),h=(int)(28*scale);
    const int x=iconCenterX-w/2,y=iconCenterY-h/2;
    const int r=(int)(4*scale)+1;
    tft.fillRoundRect(x+8,y-5,w-7,h,r,kvxConfig.secColor);
    tft.fillRoundRect(x+11,y-2,w-13,h-5,r,kvxConfig.bgColor);
    tft.fillRoundRect(x,y+5,w-5,h,r,kvxConfig.priColor);
    tft.fillRoundRect(x+3,y+8,w-11,h-6,r,kvxConfig.bgColor);
    tft.fillTriangle(x+10,y+h+3,x+10,y+h+9,x+20,y+h+3,kvxConfig.priColor);
    tft.drawFastHLine(x+11,y+16,(int)(24*scale)+1,kvxConfig.secColor);
    tft.drawFastHLine(x+11,y+22,(int)(17*scale)+1,kvxConfig.secColor);
}
