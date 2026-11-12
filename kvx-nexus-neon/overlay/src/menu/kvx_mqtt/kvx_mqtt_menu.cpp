#include "kvx_mqtt_menu.h"
#include "kvx_mqtt_app.h"
#include "root/ui/display.h"

void KvxMqttMenu::optionsMenu() { kvxMqttApp(); }

void KvxMqttMenu::drawIcon(float scale) {
    clearIconArea();
    const int width = static_cast<int>(42 * scale);
    const int height = static_cast<int>(28 * scale);
    const int radius = static_cast<int>(6 * scale) + 1;
    const int x = iconCenterX - width / 2;
    const int y = iconCenterY - height / 2 - static_cast<int>(3 * scale);
    const int tailX = x + width / 4;
    tft.fillTriangle(tailX, y + height - 2, tailX + width / 4, y + height - 2,
                     tailX, y + height + static_cast<int>(7 * scale), kvxConfig.priColor);
    tft.fillRoundRect(x, y, width, height, radius, kvxConfig.priColor);
    for (int dot = -1; dot <= 1; ++dot) {
        tft.fillCircle(iconCenterX + dot * width / 5, y + height / 2,
                       static_cast<int>(2 * scale) + 1, kvxConfig.bgColor);
    }
}
