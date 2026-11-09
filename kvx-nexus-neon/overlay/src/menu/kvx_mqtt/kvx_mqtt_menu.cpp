#include "kvx_mqtt_menu.h"
#include "kvx_mqtt_app.h"
#include "root/ui/display.h"

void KvxMqttMenu::optionsMenu() { kvxMqttApp(); }

void KvxMqttMenu::drawIcon(float scale) {
    clearIconArea();
    // Distinct MQTT hub: three endpoints joined by a bright triangular route.
    const int x=iconCenterX,y=iconCenterY,s=(int)(20*scale)+1;
    const int big=(int)(6*scale)+2,small=(int)(3*scale)+1;
    tft.drawLine(x,y-s,x-s,y+s/2,kvxConfig.secColor);
    tft.drawLine(x,y-s,x+s,y+s/2,kvxConfig.secColor);
    tft.drawLine(x-s,y+s/2,x+s,y+s/2,kvxConfig.priColor);
    tft.fillCircle(x,y-s,big,kvxConfig.priColor);
    tft.fillCircle(x-s,y+s/2,big,kvxConfig.secColor);
    tft.fillCircle(x+s,y+s/2,big,kvxConfig.secColor);
    tft.fillCircle(x,y-s,small,kvxConfig.bgColor);
    tft.fillCircle(x-s,y+s/2,small,kvxConfig.bgColor);
    tft.fillCircle(x+s,y+s/2,small,kvxConfig.bgColor);
}
