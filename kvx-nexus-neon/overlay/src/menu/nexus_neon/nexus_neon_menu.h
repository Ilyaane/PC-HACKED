#pragma once
#include <MenuItemInterface.h>

// Nexus Neon (text-only) is a native KVXputer channel, not an ESP32 app image.
class NexusNeonMenu : public MenuItemInterface {
public:
    NexusNeonMenu() : MenuItemInterface("Nexus Neon") {}
    void optionsMenu() override;
    void drawIcon(float scale) override;
    bool hasTheme() override { return false; }
    const String &themePath() override {
        static const String kNoTheme;
        return kNoTheme;
    }
};
