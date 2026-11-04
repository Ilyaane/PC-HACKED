#pragma once
#include <MenuItemInterface.h>

class KvxMqttMenu : public MenuItemInterface {
public:
    KvxMqttMenu() : MenuItemInterface("MQTT") {}
    void optionsMenu() override;
    void drawIcon(float scale) override;
    bool hasTheme() override { return false; }
    const String &themePath() override {
        static const String kNoTheme;
        return kNoTheme;
    }
};
