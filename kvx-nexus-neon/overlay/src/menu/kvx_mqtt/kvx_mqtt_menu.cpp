#include "kvx_mqtt_menu.h"
#include "kvx_mqtt_app.h"
#include "root/ui/display.h"

void KvxMqttMenu::optionsMenu() { kvxMqttApp(); }

void KvxMqttMenu::drawIcon(float scale) {
    clearIconArea();
    const int cx=iconCenterX, cy=iconCenterY;
    const int r=(int)(21*scale);
    const int d=(int)(5*scale)+1;
    tft.drawRoundRect(cx-r, cy-r/2, 2*r, r, d, kvxConfig.secColor);
    tft.fillCircle(cx-r/2, cy, 3*scale+1, kvxConfig.priColor);
    tft.fillCircle(cx+r/2, cy, 3*scale+1, kvxConfig.priColor);
    tft.drawLine(cx-r/2, cy, cx+r/2, cy, kvxConfig.secColor);
    tft.drawLine(cx, cy-r/2+3, cx, cy+r/2-3, kvxConfig.priColor);
}
