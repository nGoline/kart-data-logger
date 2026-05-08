#pragma once
#include <Arduino.h>

// 0 = dark (default), 1 = light
class ConfigManager {
public:
    // SD must be initialized by LogManager before calling begin().
    bool begin();
    bool save();

    uint8_t getTheme() const { return _theme; }
    void setTheme(uint8_t t) { _theme = t; }

private:
    uint8_t _theme = 0;
    bool parseFile(const String &text);
};

extern ConfigManager configManager;
