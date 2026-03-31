#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

class BatteryManager {
public:
    // adcPin: The GPIO used for sensing
    // dividerEnablePin: the GPIO that enables the voltage divider only while measuring
    BatteryManager(uint8_t adcPin, uint8_t dividerEnablePin);

    void begin();
    float getVoltage();
    int getPercentage(); // 0-100%

private:
    uint8_t _pin;
    uint8_t _dividerEnablePin;
    const float _vRef = 3.3f;
    const float _dividerRatio = 2.0f;
};

#endif