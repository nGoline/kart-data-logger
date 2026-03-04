#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

class BatteryManager {
public:
    // adcPin: The GPIO used for sensing
    // ratio: The multiplier to get back to real voltage (e.g., 2.0 for a 10k/10k divider)
    BatteryManager(uint8_t adcPin, float ratio);

    void begin();
    float getVoltage();
    int getPercentage(); // 0-100%

private:
    uint8_t _pin;
    float _ratio;
    const float _vRef = 3.3f;
};

#endif