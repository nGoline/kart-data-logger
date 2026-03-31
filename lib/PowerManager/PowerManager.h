#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>

class PowerManager {
public:
    PowerManager(uint8_t offButtonPin, uint8_t peripheralEnablePin, uint32_t holdTimeMs, uint8_t minBatteryPercent);

    void begin();
    void update(int batteryPercentage);

private:
    void enterHibernate(const char *reason);

    uint8_t _offButtonPin;
    uint8_t _peripheralEnablePin;
    uint32_t _holdTimeMs;
    uint8_t _minBatteryPercent;
    uint32_t _buttonPressTime;
    bool _isButtonPressed;
    bool _ignoreInitialBootPress;
};

#endif