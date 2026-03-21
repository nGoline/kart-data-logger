#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

class BatteryManager {
public:
    // adcPin: The GPIO used for sensing
    // latchPin: The GPIO used to commit suicide in case battery level is too low
    BatteryManager(uint8_t adcPin, uint8_t latchPin);

    void begin();
    float getVoltage();
    int getPercentage(); // 0-100%

private:
    uint8_t _pin;
    uint8_t _latchPin;
    const float _vRef = 3.3f;
};

#endif